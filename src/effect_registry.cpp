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
            p.valueFloat = pConfig->getOption<float>(name, defaultVal);
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
            p.valueInt = pConfig->getOption<int32_t>(name, defaultVal);
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
            if (isBuiltInEffect(name))
            {
                initBuiltInEffect(name);
            }
            else
            {
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

    void EffectRegistry::initBuiltInEffect(const std::string& name)
    {
        EffectConfig config;
        config.name = name;
        config.type = EffectType::BuiltIn;
        config.enabled = true;

        if (name == "cas")
        {
            config.parameters.push_back(
                makeFloatParam("cas", "casSharpness", "Sharpness", 0.4f, 0.0f, 1.0f, pConfig));
        }
        else if (name == "dls")
        {
            config.parameters.push_back(
                makeFloatParam("dls", "dlsSharpness", "Sharpness", 0.5f, 0.0f, 1.0f, pConfig));
            config.parameters.push_back(
                makeFloatParam("dls", "dlsDenoise", "Denoise", 0.17f, 0.0f, 1.0f, pConfig));
        }
        else if (name == "fxaa")
        {
            config.parameters.push_back(
                makeFloatParam("fxaa", "fxaaQualitySubpix", "Quality Subpix", 0.75f, 0.0f, 1.0f, pConfig));
            config.parameters.push_back(
                makeFloatParam("fxaa", "fxaaQualityEdgeThreshold", "Edge Threshold", 0.125f, 0.0f, 0.5f, pConfig));
            config.parameters.push_back(
                makeFloatParam("fxaa", "fxaaQualityEdgeThresholdMin", "Edge Threshold Min", 0.0312f, 0.0f, 0.1f, pConfig));
        }
        else if (name == "smaa")
        {
            config.parameters.push_back(
                makeFloatParam("smaa", "smaaThreshold", "Threshold", 0.05f, 0.0f, 0.5f, pConfig));
            config.parameters.push_back(
                makeIntParam("smaa", "smaaMaxSearchSteps", "Max Search Steps", 32, 0, 112, pConfig));
            config.parameters.push_back(
                makeIntParam("smaa", "smaaMaxSearchStepsDiag", "Max Search Steps Diag", 16, 0, 20, pConfig));
            config.parameters.push_back(
                makeIntParam("smaa", "smaaCornerRounding", "Corner Rounding", 25, 0, 100, pConfig));
        }
        else if (name == "deband")
        {
            config.parameters.push_back(
                makeFloatParam("deband", "debandAvgdiff", "Avg Diff", 3.4f, 0.0f, 255.0f, pConfig));
            config.parameters.push_back(
                makeFloatParam("deband", "debandMaxdiff", "Max Diff", 6.8f, 0.0f, 255.0f, pConfig));
            config.parameters.push_back(
                makeFloatParam("deband", "debandMiddiff", "Mid Diff", 3.3f, 0.0f, 255.0f, pConfig));
            config.parameters.push_back(
                makeFloatParam("deband", "debandRange", "Range", 16.0f, 1.0f, 64.0f, pConfig));
            config.parameters.push_back(
                makeIntParam("deband", "debandIterations", "Iterations", 4, 1, 16, pConfig));
        }
        else if (name == "lut")
        {
            EffectParameter p;
            p.effectName = "lut";
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

    void EffectRegistry::ensureEffect(const std::string& name, const std::string& effectPath)
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (findEffect(name))
                return;
        }

        if (isBuiltInEffect(name))
        {
            initBuiltInEffect(name);
            return;
        }

        std::string path = effectPath.empty() ? findEffectPath(name, pConfig) : effectPath;
        if (path.empty() || !std::filesystem::exists(path))
        {
            Logger::warn("EffectRegistry::ensureEffect: could not find effect file for: " + name);
            return;
        }

        initReshadeEffect(name, path);
    }

} // namespace vkBasalt
