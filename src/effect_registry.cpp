#include "effect_registry.hpp"

#include <algorithm>
#include <filesystem>

#include "reshade_parser.hpp"
#include "logger.hpp"

namespace vkBasalt
{
    static const std::vector<std::string> builtInEffects = {"cas", "dls", "fxaa", "smaa", "deband", "lut"};

    bool EffectRegistry::isBuiltInEffect(const std::string& name)
    {
        return std::find(builtInEffects.begin(), builtInEffects.end(), name) != builtInEffects.end();
    }

    void EffectRegistry::initialize(Config* pConfig)
    {
        std::lock_guard<std::mutex> lock(mutex);
        this->pConfig = pConfig;
        effects.clear();

        // Get list of effects from config
        std::vector<std::string> effectNames = pConfig->getOption<std::vector<std::string>>("effects");

        for (const auto& name : effectNames)
        {
            if (isBuiltInEffect(name))
            {
                initBuiltInEffect(name);
            }
            else
            {
                // ReShade effect - find path
                std::string effectPath = pConfig->getOption<std::string>(name, "");
                if (effectPath.empty())
                {
                    // Try constructing from reshadeIncludePath
                    std::string includePath = pConfig->getOption<std::string>("reshadeIncludePath");
                    if (!includePath.empty())
                    {
                        effectPath = includePath + "/" + name + ".fx";
                        if (!std::filesystem::exists(effectPath))
                            effectPath = includePath + "/" + name;
                    }
                }

                if (!effectPath.empty() && std::filesystem::exists(effectPath))
                {
                    initReshadeEffect(name, effectPath);
                }
                else
                {
                    Logger::err("EffectRegistry: could not find effect file for: " + name);
                }
            }
        }

        Logger::debug("EffectRegistry: initialized " + std::to_string(effects.size()) + " effects");
    }

    void EffectRegistry::initBuiltInEffect(const std::string& name)
    {
        EffectConfig config;
        config.name = name;
        config.type = EffectType::BuiltIn;
        config.enabled = true;

        // Populate parameters based on effect type (same as effect_params.cpp)
        if (name == "cas")
        {
            EffectParameter p;
            p.effectName = "cas";
            p.name = "casSharpness";
            p.label = "Sharpness";
            p.type = ParamType::Float;
            p.defaultFloat = 0.4f;
            p.valueFloat = pConfig->getOption<float>("casSharpness", p.defaultFloat);
            p.minFloat = 0.0f;
            p.maxFloat = 1.0f;
            config.parameters.push_back(p);
        }
        else if (name == "dls")
        {
            EffectParameter p1;
            p1.effectName = "dls";
            p1.name = "dlsSharpness";
            p1.label = "Sharpness";
            p1.type = ParamType::Float;
            p1.defaultFloat = 0.5f;
            p1.valueFloat = pConfig->getOption<float>("dlsSharpness", p1.defaultFloat);
            p1.minFloat = 0.0f;
            p1.maxFloat = 1.0f;
            config.parameters.push_back(p1);

            EffectParameter p2;
            p2.effectName = "dls";
            p2.name = "dlsDenoise";
            p2.label = "Denoise";
            p2.type = ParamType::Float;
            p2.defaultFloat = 0.17f;
            p2.valueFloat = pConfig->getOption<float>("dlsDenoise", p2.defaultFloat);
            p2.minFloat = 0.0f;
            p2.maxFloat = 1.0f;
            config.parameters.push_back(p2);
        }
        else if (name == "fxaa")
        {
            EffectParameter p1;
            p1.effectName = "fxaa";
            p1.name = "fxaaQualitySubpix";
            p1.label = "Quality Subpix";
            p1.type = ParamType::Float;
            p1.defaultFloat = 0.75f;
            p1.valueFloat = pConfig->getOption<float>("fxaaQualitySubpix", p1.defaultFloat);
            p1.minFloat = 0.0f;
            p1.maxFloat = 1.0f;
            config.parameters.push_back(p1);

            EffectParameter p2;
            p2.effectName = "fxaa";
            p2.name = "fxaaQualityEdgeThreshold";
            p2.label = "Edge Threshold";
            p2.type = ParamType::Float;
            p2.defaultFloat = 0.125f;
            p2.valueFloat = pConfig->getOption<float>("fxaaQualityEdgeThreshold", p2.defaultFloat);
            p2.minFloat = 0.0f;
            p2.maxFloat = 0.5f;
            config.parameters.push_back(p2);

            EffectParameter p3;
            p3.effectName = "fxaa";
            p3.name = "fxaaQualityEdgeThresholdMin";
            p3.label = "Edge Threshold Min";
            p3.type = ParamType::Float;
            p3.defaultFloat = 0.0312f;
            p3.valueFloat = pConfig->getOption<float>("fxaaQualityEdgeThresholdMin", p3.defaultFloat);
            p3.minFloat = 0.0f;
            p3.maxFloat = 0.1f;
            config.parameters.push_back(p3);
        }
        else if (name == "smaa")
        {
            EffectParameter p1;
            p1.effectName = "smaa";
            p1.name = "smaaThreshold";
            p1.label = "Threshold";
            p1.type = ParamType::Float;
            p1.defaultFloat = 0.05f;
            p1.valueFloat = pConfig->getOption<float>("smaaThreshold", p1.defaultFloat);
            p1.minFloat = 0.0f;
            p1.maxFloat = 0.5f;
            config.parameters.push_back(p1);

            EffectParameter p2;
            p2.effectName = "smaa";
            p2.name = "smaaMaxSearchSteps";
            p2.label = "Max Search Steps";
            p2.type = ParamType::Int;
            p2.defaultInt = 32;
            p2.valueInt = pConfig->getOption<int32_t>("smaaMaxSearchSteps", p2.defaultInt);
            p2.minInt = 0;
            p2.maxInt = 112;
            config.parameters.push_back(p2);

            EffectParameter p3;
            p3.effectName = "smaa";
            p3.name = "smaaMaxSearchStepsDiag";
            p3.label = "Max Search Steps Diag";
            p3.type = ParamType::Int;
            p3.defaultInt = 16;
            p3.valueInt = pConfig->getOption<int32_t>("smaaMaxSearchStepsDiag", p3.defaultInt);
            p3.minInt = 0;
            p3.maxInt = 20;
            config.parameters.push_back(p3);

            EffectParameter p4;
            p4.effectName = "smaa";
            p4.name = "smaaCornerRounding";
            p4.label = "Corner Rounding";
            p4.type = ParamType::Int;
            p4.defaultInt = 25;
            p4.valueInt = pConfig->getOption<int32_t>("smaaCornerRounding", p4.defaultInt);
            p4.minInt = 0;
            p4.maxInt = 100;
            config.parameters.push_back(p4);
        }
        else if (name == "deband")
        {
            EffectParameter p1;
            p1.effectName = "deband";
            p1.name = "debandAvgdiff";
            p1.label = "Avg Diff";
            p1.type = ParamType::Float;
            p1.defaultFloat = 3.4f;
            p1.valueFloat = pConfig->getOption<float>("debandAvgdiff", p1.defaultFloat);
            p1.minFloat = 0.0f;
            p1.maxFloat = 255.0f;
            config.parameters.push_back(p1);

            EffectParameter p2;
            p2.effectName = "deband";
            p2.name = "debandMaxdiff";
            p2.label = "Max Diff";
            p2.type = ParamType::Float;
            p2.defaultFloat = 6.8f;
            p2.valueFloat = pConfig->getOption<float>("debandMaxdiff", p2.defaultFloat);
            p2.minFloat = 0.0f;
            p2.maxFloat = 255.0f;
            config.parameters.push_back(p2);

            EffectParameter p3;
            p3.effectName = "deband";
            p3.name = "debandMiddiff";
            p3.label = "Mid Diff";
            p3.type = ParamType::Float;
            p3.defaultFloat = 3.3f;
            p3.valueFloat = pConfig->getOption<float>("debandMiddiff", p3.defaultFloat);
            p3.minFloat = 0.0f;
            p3.maxFloat = 255.0f;
            config.parameters.push_back(p3);

            EffectParameter p4;
            p4.effectName = "deband";
            p4.name = "debandRange";
            p4.label = "Range";
            p4.type = ParamType::Float;
            p4.defaultFloat = 16.0f;
            p4.valueFloat = pConfig->getOption<float>("debandRange", p4.defaultFloat);
            p4.minFloat = 1.0f;
            p4.maxFloat = 64.0f;
            config.parameters.push_back(p4);

            EffectParameter p5;
            p5.effectName = "deband";
            p5.name = "debandIterations";
            p5.label = "Iterations";
            p5.type = ParamType::Int;
            p5.defaultInt = 4;
            p5.valueInt = pConfig->getOption<int32_t>("debandIterations", p5.defaultInt);
            p5.minInt = 1;
            p5.maxInt = 16;
            config.parameters.push_back(p5);
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

        // Parse the .fx file to get parameters
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
            for (const auto& param : effect.parameters)
            {
                params.push_back(param);
            }
        }
        return params;
    }

    void EffectRegistry::setEffectEnabled(const std::string& effectName, bool enabled)
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& effect : effects)
        {
            if (effect.name == effectName)
            {
                effect.enabled = enabled;
                return;
            }
        }
    }

    void EffectRegistry::setParameterValue(const std::string& effectName, const std::string& paramName, float value)
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& effect : effects)
        {
            if (effect.name == effectName)
            {
                for (auto& param : effect.parameters)
                {
                    if (param.name == paramName)
                    {
                        param.valueFloat = value;
                        return;
                    }
                }
            }
        }
    }

    void EffectRegistry::setParameterValue(const std::string& effectName, const std::string& paramName, int value)
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& effect : effects)
        {
            if (effect.name == effectName)
            {
                for (auto& param : effect.parameters)
                {
                    if (param.name == paramName)
                    {
                        param.valueInt = value;
                        return;
                    }
                }
            }
        }
    }

    void EffectRegistry::setParameterValue(const std::string& effectName, const std::string& paramName, bool value)
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& effect : effects)
        {
            if (effect.name == effectName)
            {
                for (auto& param : effect.parameters)
                {
                    if (param.name == paramName)
                    {
                        param.valueBool = value;
                        return;
                    }
                }
            }
        }
    }

    EffectParameter* EffectRegistry::getParameter(const std::string& effectName, const std::string& paramName)
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& effect : effects)
        {
            if (effect.name == effectName)
            {
                for (auto& param : effect.parameters)
                {
                    if (param.name == paramName)
                        return &param;
                }
            }
        }
        return nullptr;
    }

    const EffectParameter* EffectRegistry::getParameter(const std::string& effectName, const std::string& paramName) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& effect : effects)
        {
            if (effect.name == effectName)
            {
                for (const auto& param : effect.parameters)
                {
                    if (param.name == paramName)
                        return &param;
                }
            }
        }
        return nullptr;
    }

    bool EffectRegistry::hasEffect(const std::string& name) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& effect : effects)
        {
            if (effect.name == name)
                return true;
        }
        return false;
    }

    void EffectRegistry::ensureEffect(const std::string& name, const std::string& effectPath)
    {
        // Check without lock first to avoid unnecessary locking
        {
            std::lock_guard<std::mutex> lock(mutex);
            for (const auto& effect : effects)
            {
                if (effect.name == name)
                    return;  // Already exists
            }
        }

        // Effect not found, add it
        if (isBuiltInEffect(name))
        {
            initBuiltInEffect(name);
        }
        else
        {
            // ReShade effect - find path if not provided
            std::string path = effectPath;
            if (path.empty())
            {
                path = pConfig->getOption<std::string>(name, "");
                if (path.empty())
                {
                    std::string includePath = pConfig->getOption<std::string>("reshadeIncludePath");
                    if (!includePath.empty())
                    {
                        path = includePath + "/" + name + ".fx";
                        if (!std::filesystem::exists(path))
                            path = includePath + "/" + name;
                    }
                }
            }

            if (!path.empty() && std::filesystem::exists(path))
            {
                initReshadeEffect(name, path);
            }
            else
            {
                Logger::warn("EffectRegistry::ensureEffect: could not find effect file for: " + name);
            }
        }
    }

} // namespace vkBasalt
