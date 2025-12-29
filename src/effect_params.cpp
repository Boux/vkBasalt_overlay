#include "effect_params.hpp"

namespace vkBasalt
{
    std::vector<EffectParameter> collectEffectParameters(
        std::shared_ptr<Config> pConfig,
        const std::vector<std::string>& effectNames,
        const std::vector<std::shared_ptr<Effect>>& effects)
    {
        std::vector<EffectParameter> parameters;

        for (const auto& effectName : effectNames)
        {
            if (effectName == "cas")
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
                parameters.push_back(p);
            }
            else if (effectName == "dls")
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
                parameters.push_back(p1);

                EffectParameter p2;
                p2.effectName = "dls";
                p2.name = "dlsDenoise";
                p2.label = "Denoise";
                p2.type = ParamType::Float;
                p2.defaultFloat = 0.17f;
                p2.valueFloat = pConfig->getOption<float>("dlsDenoise", p2.defaultFloat);
                p2.minFloat = 0.0f;
                p2.maxFloat = 1.0f;
                parameters.push_back(p2);
            }
            else if (effectName == "fxaa")
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
                parameters.push_back(p1);

                EffectParameter p2;
                p2.effectName = "fxaa";
                p2.name = "fxaaQualityEdgeThreshold";
                p2.label = "Edge Threshold";
                p2.type = ParamType::Float;
                p2.defaultFloat = 0.125f;
                p2.valueFloat = pConfig->getOption<float>("fxaaQualityEdgeThreshold", p2.defaultFloat);
                p2.minFloat = 0.0f;
                p2.maxFloat = 0.5f;
                parameters.push_back(p2);

                EffectParameter p3;
                p3.effectName = "fxaa";
                p3.name = "fxaaQualityEdgeThresholdMin";
                p3.label = "Edge Threshold Min";
                p3.type = ParamType::Float;
                p3.defaultFloat = 0.0312f;
                p3.valueFloat = pConfig->getOption<float>("fxaaQualityEdgeThresholdMin", p3.defaultFloat);
                p3.minFloat = 0.0f;
                p3.maxFloat = 0.1f;
                parameters.push_back(p3);
            }
            else if (effectName == "smaa")
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
                parameters.push_back(p1);

                EffectParameter p2;
                p2.effectName = "smaa";
                p2.name = "smaaMaxSearchSteps";
                p2.label = "Max Search Steps";
                p2.type = ParamType::Int;
                p2.defaultInt = 32;
                p2.valueInt = pConfig->getOption<int32_t>("smaaMaxSearchSteps", p2.defaultInt);
                p2.minInt = 0;
                p2.maxInt = 112;
                parameters.push_back(p2);

                EffectParameter p3;
                p3.effectName = "smaa";
                p3.name = "smaaMaxSearchStepsDiag";
                p3.label = "Max Search Steps Diag";
                p3.type = ParamType::Int;
                p3.defaultInt = 16;
                p3.valueInt = pConfig->getOption<int32_t>("smaaMaxSearchStepsDiag", p3.defaultInt);
                p3.minInt = 0;
                p3.maxInt = 20;
                parameters.push_back(p3);

                EffectParameter p4;
                p4.effectName = "smaa";
                p4.name = "smaaCornerRounding";
                p4.label = "Corner Rounding";
                p4.type = ParamType::Int;
                p4.defaultInt = 25;
                p4.valueInt = pConfig->getOption<int32_t>("smaaCornerRounding", p4.defaultInt);
                p4.minInt = 0;
                p4.maxInt = 100;
                parameters.push_back(p4);
            }
            else if (effectName == "deband")
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
                parameters.push_back(p1);

                EffectParameter p2;
                p2.effectName = "deband";
                p2.name = "debandMaxdiff";
                p2.label = "Max Diff";
                p2.type = ParamType::Float;
                p2.defaultFloat = 6.8f;
                p2.valueFloat = pConfig->getOption<float>("debandMaxdiff", p2.defaultFloat);
                p2.minFloat = 0.0f;
                p2.maxFloat = 255.0f;
                parameters.push_back(p2);

                EffectParameter p3;
                p3.effectName = "deband";
                p3.name = "debandMiddiff";
                p3.label = "Mid Diff";
                p3.type = ParamType::Float;
                p3.defaultFloat = 3.3f;
                p3.valueFloat = pConfig->getOption<float>("debandMiddiff", p3.defaultFloat);
                p3.minFloat = 0.0f;
                p3.maxFloat = 255.0f;
                parameters.push_back(p3);

                EffectParameter p4;
                p4.effectName = "deband";
                p4.name = "debandRange";
                p4.label = "Range";
                p4.type = ParamType::Float;
                p4.defaultFloat = 16.0f;
                p4.valueFloat = pConfig->getOption<float>("debandRange", p4.defaultFloat);
                p4.minFloat = 1.0f;
                p4.maxFloat = 64.0f;
                parameters.push_back(p4);

                EffectParameter p5;
                p5.effectName = "deband";
                p5.name = "debandIterations";
                p5.label = "Iterations";
                p5.type = ParamType::Int;
                p5.defaultInt = 4;
                p5.valueInt = pConfig->getOption<int32_t>("debandIterations", p5.defaultInt);
                p5.minInt = 1;
                p5.maxInt = 16;
                parameters.push_back(p5);
            }
            else if (effectName == "lut")
            {
                EffectParameter p;
                p.effectName = "lut";
                p.name = "lutFile";
                p.label = "LUT File";
                p.type = ParamType::Float;
                p.valueFloat = 0;
                parameters.push_back(p);
            }
            else
            {
                // ReShade effect - get parameters from the effect itself
                for (const auto& effect : effects)
                {
                    auto params = effect->getParameters();
                    for (const auto& param : params)
                    {
                        if (param.effectName == effectName)
                            parameters.push_back(param);
                    }
                }
            }
        }

        return parameters;
    }

} // namespace vkBasalt
