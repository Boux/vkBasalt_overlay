#include "effect_registry.hpp"

#include <algorithm>
#include <filesystem>
#include <set>

#include "reshade_parser.hpp"
#include "config_serializer.hpp"
#include "builtin/builtin_effects.hpp"
#include "logger.hpp"

namespace vkBasalt
{

    namespace
    {
        // Helper to create a float parameter
        std::unique_ptr<FloatParam> makeFloatParam(
            const std::string& effectName,
            const std::string& name,
            const std::string& label,
            float defaultVal,
            float minVal,
            float maxVal,
            Config* pConfig)
        {
            auto p = std::make_unique<FloatParam>();
            p->effectName = effectName;
            p->name = name;
            p->label = label;
            p->defaultValue = defaultVal;
            p->value = pConfig->getInstanceOption<float>(effectName, name, defaultVal);
            p->minValue = minVal;
            p->maxValue = maxVal;
            return p;
        }

        // Helper to create an int parameter
        std::unique_ptr<IntParam> makeIntParam(
            const std::string& effectName,
            const std::string& name,
            const std::string& label,
            int defaultVal,
            int minVal,
            int maxVal,
            Config* pConfig)
        {
            auto p = std::make_unique<IntParam>();
            p->effectName = effectName;
            p->name = name;
            p->label = label;
            p->defaultValue = defaultVal;
            p->value = pConfig->getInstanceOption<int32_t>(effectName, name, defaultVal);
            p->minValue = minVal;
            p->maxValue = maxVal;
            return p;
        }

        // Try to find effect file path
        std::string findEffectPath(const std::string& name, Config* pConfig)
        {
            // First check if path is directly configured
            std::string path = pConfig->getOption<std::string>(name, "");
            if (!path.empty() && std::filesystem::exists(path))
                return path;

            // Search in shader manager discovered paths
            ShaderManagerConfig shaderMgrConfig = ConfigSerializer::loadShaderManagerConfig();
            for (const auto& shaderPath : shaderMgrConfig.discoveredShaderPaths)
            {
                // Try with .fx extension
                path = shaderPath + "/" + name + ".fx";
                if (std::filesystem::exists(path))
                    return path;

                // Try without extension
                path = shaderPath + "/" + name;
                if (std::filesystem::exists(path))
                    return path;
            }

            return "";
        }
    } // anonymous namespace

    bool EffectRegistry::isBuiltInEffect(const std::string& name)
    {
        return BuiltInEffects::instance().isBuiltIn(name);
    }

    void EffectRegistry::initialize(Config* pConfig)
    {
        std::lock_guard<std::mutex> lock(mutex);
        this->pConfig = pConfig;
        effects.clear();

        std::vector<std::string> effectNames = pConfig->getOption<std::vector<std::string>>("effects");
        std::vector<std::string> disabledEffects = pConfig->getOption<std::vector<std::string>>("disabledEffects");

        // Build set for quick lookup
        std::set<std::string> disabledSet(disabledEffects.begin(), disabledEffects.end());

        for (const auto& name : effectNames)
        {
            // Check if there's a stored effect type/path for this effect
            // Format: "cas.2 = cas" (built-in) or "Clarity = /path/to/Clarity.fx" (ReShade)
            std::string storedValue = pConfig->getOption<std::string>(name, "");

            if (!storedValue.empty() && isBuiltInEffect(storedValue))
            {
                // Stored value is a built-in type name (e.g., "cas.2 = cas")
                initBuiltInEffect(name, storedValue);
            }
            else if (isBuiltInEffect(name))
            {
                // Effect name itself is a built-in (e.g., "cas")
                initBuiltInEffect(name, name);
            }
            else
            {
                // Try to find as ReShade effect
                std::string effectPath = findEffectPath(name, pConfig);
                if (effectPath.empty())
                {
                    Logger::err("EffectRegistry: could not find effect file for: " + name);
                    continue;
                }
                initReshadeEffect(name, effectPath);
            }

            // Set enabled state based on disabledEffects list
            if (!effects.empty() && disabledSet.count(name))
                effects.back().enabled = false;
        }

        Logger::debug("EffectRegistry: initialized " + std::to_string(effects.size()) + " effects");
    }

    void EffectRegistry::initBuiltInEffect(const std::string& instanceName, const std::string& effectType)
    {
        const auto* def = BuiltInEffects::instance().getDef(effectType);
        if (!def)
        {
            Logger::err("Unknown built-in effect type: " + effectType);
            return;
        }

        EffectConfig config;
        config.name = instanceName;
        config.effectType = effectType;
        config.type = EffectType::BuiltIn;
        config.enabled = true;

        // Create parameters from centralized definitions
        for (const auto& paramDef : def->params)
        {
            if (paramDef.type == ParamType::Float)
            {
                config.parameters.push_back(
                    makeFloatParam(instanceName, paramDef.name, paramDef.label,
                                   paramDef.defaultFloat, paramDef.minFloat, paramDef.maxFloat, pConfig));
            }
            else if (paramDef.type == ParamType::Int)
            {
                config.parameters.push_back(
                    makeIntParam(instanceName, paramDef.name, paramDef.label,
                                 paramDef.defaultInt, paramDef.minInt, paramDef.maxInt, pConfig));
            }
        }

        effects.push_back(std::move(config));
    }

    void EffectRegistry::initReshadeEffect(const std::string& name, const std::string& path)
    {
        EffectConfig config;
        config.name = name;
        config.filePath = path;
        config.type = EffectType::ReShade;
        config.enabled = true;

        // Extract effectType from filename (e.g., "/path/to/Clarity.fx" -> "Clarity")
        std::filesystem::path p(path);
        config.effectType = p.stem().string();

        // Test shader compilation first to catch errors
        ShaderTestResult testResult = testShaderCompilation(name, path);
        if (!testResult.success)
        {
            config.compileError = testResult.errorMessage;
            config.enabled = false;  // Disable failed effects by default
            Logger::err("EffectRegistry: failed to compile " + name + ": " + testResult.errorMessage);
        }
        else
        {
            // Only parse parameters if compilation succeeded
            config.parameters = parseReshadeEffect(name, path, pConfig);

            // Extract preprocessor definitions (user-configurable macros)
            config.preprocessorDefs = extractPreprocessorDefinitions(name, path);

            // Override default values with any saved values from config
            // Config format: effectName#MACRO = value
            for (auto& def : config.preprocessorDefs)
            {
                std::string configKey = name + "#" + def.name;
                std::string savedValue = pConfig->getOption<std::string>(configKey, "");
                if (!savedValue.empty())
                {
                    def.value = savedValue;
                    Logger::debug("EffectRegistry: loaded preprocessor def " + configKey + " = " + savedValue);
                }
            }

            Logger::debug("EffectRegistry: loaded ReShade effect " + name + " with " +
                          std::to_string(config.parameters.size()) + " parameters and " +
                          std::to_string(config.preprocessorDefs.size()) + " preprocessor defs");
        }

        effects.push_back(std::move(config));
    }

    std::vector<const EffectConfig*> EffectRegistry::getEnabledEffects() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<const EffectConfig*> enabled;

        for (const auto& effect : effects)
        {
            if (effect.enabled)
                enabled.push_back(&effect);
        }

        return enabled;
    }

    std::vector<std::unique_ptr<EffectParam>> EffectRegistry::getAllParameters() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<std::unique_ptr<EffectParam>> params;

        for (const auto& effect : effects)
        {
            for (const auto& p : effect.parameters)
                params.push_back(p->clone());
        }

        return params;
    }

    // Internal helper to find effect by name (assumes mutex is held)
    EffectConfig* EffectRegistry::findEffect(const std::string& effectName)
    {
        for (auto& effect : effects)
        {
            if (effect.name == effectName)
                return &effect;
        }
        return nullptr;
    }

    const EffectConfig* EffectRegistry::findEffect(const std::string& effectName) const
    {
        for (const auto& effect : effects)
        {
            if (effect.name == effectName)
                return &effect;
        }
        return nullptr;
    }

    // Internal helper to find parameter within an effect (assumes mutex is held)
    EffectParam* EffectRegistry::findParam(EffectConfig& effect, const std::string& paramName)
    {
        for (auto& param : effect.parameters)
        {
            if (param->name == paramName)
                return param.get();
        }
        return nullptr;
    }

    const EffectParam* EffectRegistry::findParam(const EffectConfig& effect, const std::string& paramName) const
    {
        for (const auto& param : effect.parameters)
        {
            if (param->name == paramName)
                return param.get();
        }
        return nullptr;
    }

    void EffectRegistry::setEffectEnabled(const std::string& effectName, bool enabled)
    {
        std::lock_guard<std::mutex> lock(mutex);

        EffectConfig* effect = findEffect(effectName);
        if (effect)
            effect->enabled = enabled;
    }

    bool EffectRegistry::isEffectEnabled(const std::string& effectName) const
    {
        std::lock_guard<std::mutex> lock(mutex);

        const EffectConfig* effect = findEffect(effectName);
        return effect ? effect->enabled : false;
    }

    std::map<std::string, bool> EffectRegistry::getEffectEnabledStates() const
    {
        std::lock_guard<std::mutex> lock(mutex);

        std::map<std::string, bool> states;
        for (const auto& effect : effects)
            states[effect.name] = effect.enabled;
        return states;
    }

    void EffectRegistry::setParameterValue(const std::string& effectName, const std::string& paramName, float value)
    {
        std::lock_guard<std::mutex> lock(mutex);

        EffectConfig* effect = findEffect(effectName);
        if (!effect)
            return;

        EffectParam* param = findParam(*effect, paramName);
        if (param && param->getType() == ParamType::Float)
            static_cast<FloatParam*>(param)->value = value;
    }

    void EffectRegistry::setParameterValue(const std::string& effectName, const std::string& paramName, int value)
    {
        std::lock_guard<std::mutex> lock(mutex);

        EffectConfig* effect = findEffect(effectName);
        if (!effect)
            return;

        EffectParam* param = findParam(*effect, paramName);
        if (param && param->getType() == ParamType::Int)
            static_cast<IntParam*>(param)->value = value;
    }

    void EffectRegistry::setParameterValue(const std::string& effectName, const std::string& paramName, bool value)
    {
        std::lock_guard<std::mutex> lock(mutex);

        EffectConfig* effect = findEffect(effectName);
        if (!effect)
            return;

        EffectParam* param = findParam(*effect, paramName);
        if (param && param->getType() == ParamType::Bool)
            static_cast<BoolParam*>(param)->value = value;
    }

    EffectParam* EffectRegistry::getParameter(const std::string& effectName, const std::string& paramName)
    {
        std::lock_guard<std::mutex> lock(mutex);

        EffectConfig* effect = findEffect(effectName);
        if (!effect)
            return nullptr;

        return findParam(*effect, paramName);
    }

    const EffectParam* EffectRegistry::getParameter(const std::string& effectName, const std::string& paramName) const
    {
        std::lock_guard<std::mutex> lock(mutex);

        const EffectConfig* effect = findEffect(effectName);
        if (!effect)
            return nullptr;

        return findParam(*effect, paramName);
    }

    std::vector<EffectParam*> EffectRegistry::getParametersForEffect(const std::string& effectName)
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<EffectParam*> result;

        EffectConfig* effect = findEffect(effectName);
        if (!effect)
            return result;

        for (auto& param : effect->parameters)
            result.push_back(param.get());

        return result;
    }

    bool EffectRegistry::hasEffect(const std::string& name) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return findEffect(name) != nullptr;
    }

    std::string EffectRegistry::getEffectFilePath(const std::string& name) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        const EffectConfig* effect = findEffect(name);
        return effect ? effect->filePath : "";
    }

    std::string EffectRegistry::getEffectType(const std::string& name) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        const EffectConfig* effect = findEffect(name);
        return effect ? effect->effectType : "";
    }

    bool EffectRegistry::isEffectBuiltIn(const std::string& name) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        const EffectConfig* effect = findEffect(name);
        return effect ? (effect->type == EffectType::BuiltIn) : false;
    }

    bool EffectRegistry::hasEffectFailed(const std::string& name) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        const EffectConfig* effect = findEffect(name);
        return effect ? effect->hasFailed() : false;
    }

    std::string EffectRegistry::getEffectError(const std::string& name) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        const EffectConfig* effect = findEffect(name);
        return effect ? effect->compileError : "";
    }

    void EffectRegistry::setEffectError(const std::string& name, const std::string& error)
    {
        std::lock_guard<std::mutex> lock(mutex);
        EffectConfig* effect = findEffect(name);
        if (effect)
        {
            effect->compileError = error;
            effect->enabled = false;  // Disable failed effects
        }
    }

    void EffectRegistry::ensureEffect(const std::string& instanceName, const std::string& effectType)
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (findEffect(instanceName))
                return;
        }

        // If effectType not provided, assume instanceName is the effect type
        std::string type = effectType.empty() ? instanceName : effectType;

        if (isBuiltInEffect(type))
        {
            initBuiltInEffect(instanceName, type);
            return;
        }

        // Use effectType to find the shader file
        std::string path = findEffectPath(type, pConfig);
        if (path.empty() || !std::filesystem::exists(path))
        {
            Logger::warn("EffectRegistry::ensureEffect: could not find effect file for: " + type);
            return;
        }

        initReshadeEffect(instanceName, path);
    }

    // Static empty vector for returning when effect not found
    static std::vector<PreprocessorDefinition> emptyDefs;

    std::vector<PreprocessorDefinition>& EffectRegistry::getPreprocessorDefs(const std::string& effectName)
    {
        std::lock_guard<std::mutex> lock(mutex);
        EffectConfig* effect = findEffect(effectName);
        return effect ? effect->preprocessorDefs : emptyDefs;
    }

    const std::vector<PreprocessorDefinition>& EffectRegistry::getPreprocessorDefs(const std::string& effectName) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        const EffectConfig* effect = findEffect(effectName);
        return effect ? effect->preprocessorDefs : emptyDefs;
    }

    void EffectRegistry::setPreprocessorDefValue(const std::string& effectName, const std::string& macroName, const std::string& value)
    {
        std::lock_guard<std::mutex> lock(mutex);
        EffectConfig* effect = findEffect(effectName);
        if (!effect)
            return;

        for (auto& def : effect->preprocessorDefs)
        {
            if (def.name == macroName)
            {
                def.value = value;
                return;
            }
        }
    }

    void EffectRegistry::setSelectedEffects(const std::vector<std::string>& effects)
    {
        std::lock_guard<std::mutex> lock(mutex);
        selectedEffects = effects;
    }

    void EffectRegistry::clearSelectedEffects()
    {
        std::lock_guard<std::mutex> lock(mutex);
        selectedEffects.clear();
    }

    void EffectRegistry::initializeSelectedEffectsFromConfig()
    {
        if (initializedFromConfig || !pConfig)
            return;

        // Read effects list from config
        std::vector<std::string> configEffects = pConfig->getOption<std::vector<std::string>>("effects", {});
        std::vector<std::string> disabledEffects = pConfig->getOption<std::vector<std::string>>("disabledEffects", {});

        // Build set of disabled effects for quick lookup
        std::set<std::string> disabledSet(disabledEffects.begin(), disabledEffects.end());

        // Set selected effects
        {
            std::lock_guard<std::mutex> lock(mutex);
            selectedEffects = configEffects;
        }

        // Ensure effects exist in registry before setting enabled states
        for (const auto& effectName : configEffects)
            ensureEffect(effectName);

        // Set enabled states (disabled if in disabledEffects list)
        for (const auto& effectName : configEffects)
        {
            bool enabled = (disabledSet.find(effectName) == disabledSet.end());
            setEffectEnabled(effectName, enabled);
        }

        initializedFromConfig = true;
        Logger::debug("EffectRegistry: initialized " + std::to_string(configEffects.size()) +
                      " effects from config (" + std::to_string(disabledEffects.size()) + " disabled)");
    }

} // namespace vkBasalt
