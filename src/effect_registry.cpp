#include "effect_registry.hpp"

#include <algorithm>
#include <filesystem>
#include <set>

#include "reshade_parser.hpp"
#include "logger.hpp"

namespace vkBasalt
{
    static const std::vector<std::string> builtInEffects = {"cas", "dls", "fxaa", "smaa", "deband", "lut"};

    namespace
    {
        // Helper to create a float parameter
        EffectParameter makeFloatParam(
            const std::string& effectName,
            const std::string& name,
            const std::string& label,
            float defaultVal,
            float minVal,
            float maxVal,
            Config* pConfig)
        {
            EffectParameter p;
            p.effectName = effectName;
            p.name = name;
            p.label = label;
            p.type = ParamType::Float;
            p.defaultFloat = defaultVal;
            p.valueFloat = pConfig->getInstanceOption<float>(effectName, name, defaultVal);
            p.minFloat = minVal;
            p.maxFloat = maxVal;
            return p;
        }

        // Helper to create an int parameter
        EffectParameter makeIntParam(
            const std::string& effectName,
            const std::string& name,
            const std::string& label,
            int defaultVal,
            int minVal,
            int maxVal,
            Config* pConfig)
        {
            EffectParameter p;
            p.effectName = effectName;
            p.name = name;
            p.label = label;
            p.type = ParamType::Int;
            p.defaultInt = defaultVal;
            p.valueInt = pConfig->getInstanceOption<int32_t>(effectName, name, defaultVal);
            p.minInt = minVal;
            p.maxInt = maxVal;
            return p;
        }

        // Try to find effect file path
        std::string findEffectPath(const std::string& name, Config* pConfig)
        {
            // First check if path is directly configured
            std::string path = pConfig->getOption<std::string>(name, "");
            if (!path.empty() && std::filesystem::exists(path))
                return path;

            // Try constructing from reshadeIncludePath
            std::string includePath = pConfig->getOption<std::string>("reshadeIncludePath");
            if (includePath.empty())
                return "";

            // Try with .fx extension
            path = includePath + "/" + name + ".fx";
            if (std::filesystem::exists(path))
                return path;

            // Try without extension
            path = includePath + "/" + name;
            if (std::filesystem::exists(path))
                return path;

            return "";
        }
    } // anonymous namespace

    bool EffectRegistry::isBuiltInEffect(const std::string& name)
    {
        return std::find(builtInEffects.begin(), builtInEffects.end(), name) != builtInEffects.end();
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
        EffectConfig config;
        config.name = instanceName;
        config.effectType = effectType;
        config.type = EffectType::BuiltIn;
        config.enabled = true;

        // Use effectType to determine params, but instanceName for param lookups
        if (effectType == "cas")
        {
            config.parameters.push_back(
                makeFloatParam(instanceName, "casSharpness", "Sharpness", 0.4f, 0.0f, 1.0f, pConfig));
        }
        else if (effectType == "dls")
        {
            config.parameters.push_back(
                makeFloatParam(instanceName, "dlsSharpness", "Sharpness", 0.5f, 0.0f, 1.0f, pConfig));
            config.parameters.push_back(
                makeFloatParam(instanceName, "dlsDenoise", "Denoise", 0.17f, 0.0f, 1.0f, pConfig));
        }
        else if (effectType == "fxaa")
        {
            config.parameters.push_back(
                makeFloatParam(instanceName, "fxaaQualitySubpix", "Quality Subpix", 0.75f, 0.0f, 1.0f, pConfig));
            config.parameters.push_back(
                makeFloatParam(instanceName, "fxaaQualityEdgeThreshold", "Edge Threshold", 0.125f, 0.0f, 0.5f, pConfig));
            config.parameters.push_back(
                makeFloatParam(instanceName, "fxaaQualityEdgeThresholdMin", "Edge Threshold Min", 0.0312f, 0.0f, 0.1f, pConfig));
        }
        else if (effectType == "smaa")
        {
            config.parameters.push_back(
                makeFloatParam(instanceName, "smaaThreshold", "Threshold", 0.05f, 0.0f, 0.5f, pConfig));
            config.parameters.push_back(
                makeIntParam(instanceName, "smaaMaxSearchSteps", "Max Search Steps", 32, 0, 112, pConfig));
            config.parameters.push_back(
                makeIntParam(instanceName, "smaaMaxSearchStepsDiag", "Max Search Steps Diag", 16, 0, 20, pConfig));
            config.parameters.push_back(
                makeIntParam(instanceName, "smaaCornerRounding", "Corner Rounding", 25, 0, 100, pConfig));
        }
        else if (effectType == "deband")
        {
            config.parameters.push_back(
                makeFloatParam(instanceName, "debandAvgdiff", "Avg Diff", 3.4f, 0.0f, 255.0f, pConfig));
            config.parameters.push_back(
                makeFloatParam(instanceName, "debandMaxdiff", "Max Diff", 6.8f, 0.0f, 255.0f, pConfig));
            config.parameters.push_back(
                makeFloatParam(instanceName, "debandMiddiff", "Mid Diff", 3.3f, 0.0f, 255.0f, pConfig));
            config.parameters.push_back(
                makeFloatParam(instanceName, "debandRange", "Range", 16.0f, 1.0f, 64.0f, pConfig));
            config.parameters.push_back(
                makeIntParam(instanceName, "debandIterations", "Iterations", 4, 1, 16, pConfig));
        }
        else if (effectType == "lut")
        {
            EffectParameter p;
            p.effectName = instanceName;
            p.name = "lutFile";
            p.label = "LUT File";
            p.type = ParamType::Float;
            p.valueFloat = 0;
            config.parameters.push_back(p);
        }

        effects.push_back(config);
    }

    void EffectRegistry::initReshadeEffect(const std::string& name, const std::string& path)
    {
        EffectConfig config;
        config.name = name;
        config.filePath = path;
        config.type = EffectType::ReShade;
        config.enabled = true;
        config.parameters = parseReshadeEffect(name, path, pConfig);

        // Extract effectType from filename (e.g., "/path/to/Clarity.fx" -> "Clarity")
        std::filesystem::path p(path);
        config.effectType = p.stem().string();

        effects.push_back(config);
        Logger::debug("EffectRegistry: loaded ReShade effect " + name + " with " +
                      std::to_string(config.parameters.size()) + " parameters");
    }

    std::vector<EffectConfig> EffectRegistry::getEnabledEffects() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<EffectConfig> enabled;

        for (const auto& effect : effects)
        {
            if (effect.enabled)
                enabled.push_back(effect);
        }

        return enabled;
    }

    std::vector<EffectParameter> EffectRegistry::getAllParameters() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<EffectParameter> params;

        for (const auto& effect : effects)
        {
            params.insert(params.end(), effect.parameters.begin(), effect.parameters.end());
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
    EffectParameter* EffectRegistry::findParam(EffectConfig& effect, const std::string& paramName)
    {
        for (auto& param : effect.parameters)
        {
            if (param.name == paramName)
                return &param;
        }
        return nullptr;
    }

    const EffectParameter* EffectRegistry::findParam(const EffectConfig& effect, const std::string& paramName) const
    {
        for (const auto& param : effect.parameters)
        {
            if (param.name == paramName)
                return &param;
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

        EffectParameter* param = findParam(*effect, paramName);
        if (param)
            param->valueFloat = value;
    }

    void EffectRegistry::setParameterValue(const std::string& effectName, const std::string& paramName, int value)
    {
        std::lock_guard<std::mutex> lock(mutex);

        EffectConfig* effect = findEffect(effectName);
        if (!effect)
            return;

        EffectParameter* param = findParam(*effect, paramName);
        if (param)
            param->valueInt = value;
    }

    void EffectRegistry::setParameterValue(const std::string& effectName, const std::string& paramName, bool value)
    {
        std::lock_guard<std::mutex> lock(mutex);

        EffectConfig* effect = findEffect(effectName);
        if (!effect)
            return;

        EffectParameter* param = findParam(*effect, paramName);
        if (param)
            param->valueBool = value;
    }

    EffectParameter* EffectRegistry::getParameter(const std::string& effectName, const std::string& paramName)
    {
        std::lock_guard<std::mutex> lock(mutex);

        EffectConfig* effect = findEffect(effectName);
        if (!effect)
            return nullptr;

        return findParam(*effect, paramName);
    }

    const EffectParameter* EffectRegistry::getParameter(const std::string& effectName, const std::string& paramName) const
    {
        std::lock_guard<std::mutex> lock(mutex);

        const EffectConfig* effect = findEffect(effectName);
        if (!effect)
            return nullptr;

        return findParam(*effect, paramName);
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

} // namespace vkBasalt
