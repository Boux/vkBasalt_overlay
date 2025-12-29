#include "reshade_parser.hpp"

#include <climits>
#include <algorithm>
#include <filesystem>

#include "reshade/effect_parser.hpp"
#include "reshade/effect_codegen.hpp"
#include "reshade/effect_preprocessor.hpp"

#include "logger.hpp"

namespace vkBasalt
{
    std::vector<EffectParameter> parseReshadeEffect(
        const std::string& effectName,
        const std::string& effectPath,
        Config* pConfig)
    {
        std::vector<EffectParameter> params;

        reshadefx::preprocessor preprocessor;
        preprocessor.add_macro_definition("__RESHADE__", std::to_string(INT_MAX));
        preprocessor.add_macro_definition("__RESHADE_PERFORMANCE_MODE__", "1");
        preprocessor.add_macro_definition("__RENDERER__", "0x20000");

        // Use placeholder values - these don't affect parameter metadata
        preprocessor.add_macro_definition("BUFFER_WIDTH", "1920");
        preprocessor.add_macro_definition("BUFFER_HEIGHT", "1080");
        preprocessor.add_macro_definition("BUFFER_RCP_WIDTH", "(1.0 / BUFFER_WIDTH)");
        preprocessor.add_macro_definition("BUFFER_RCP_HEIGHT", "(1.0 / BUFFER_HEIGHT)");
        preprocessor.add_macro_definition("BUFFER_COLOR_DEPTH", "8");

        std::string includePath = pConfig->getOption<std::string>("reshadeIncludePath");
        preprocessor.add_include_path(includePath);

        if (!preprocessor.append_file(effectPath))
        {
            Logger::err("reshade_parser: failed to load shader file: " + effectPath);
            return params;
        }

        std::string errors = preprocessor.errors();
        if (!errors.empty())
        {
            Logger::err("reshade_parser preprocessor errors: " + errors);
        }

        reshadefx::parser parser;
        std::unique_ptr<reshadefx::codegen> codegen(reshadefx::create_codegen_spirv(
            true /* vulkan semantics */, true /* debug info */, true /* uniforms to spec constants */, true /*flip vertex shader*/));

        if (!parser.parse(std::move(preprocessor.output()), codegen.get()))
        {
            errors = parser.errors();
            if (!errors.empty())
            {
                Logger::err("reshade_parser parse errors: " + errors);
            }
            return params;
        }

        errors = parser.errors();
        if (!errors.empty())
        {
            Logger::err("reshade_parser parse errors: " + errors);
        }

        reshadefx::module module;
        codegen->write_result(module);

        // Extract parameters from spec_constants (same logic as ReshadeEffect::getParameters())
        for (const auto& spec : module.spec_constants)
        {
            // Skip uniforms with "source" annotation (auto-updated like frametime)
            auto sourceIt = std::find_if(spec.annotations.begin(), spec.annotations.end(),
                [](const auto& a) { return a.name == "source"; });
            if (sourceIt != spec.annotations.end())
                continue;

            // Skip if no name (can't be configured)
            if (spec.name.empty())
                continue;

            EffectParameter p;
            p.effectName = effectName;
            p.name = spec.name;

            // Get ui_label or use name
            auto labelIt = std::find_if(spec.annotations.begin(), spec.annotations.end(),
                [](const auto& a) { return a.name == "ui_label"; });
            p.label = (labelIt != spec.annotations.end()) ? labelIt->value.string_data : spec.name;

            // Check if value is configured, otherwise use default
            std::string configVal = pConfig->getOption<std::string>(spec.name);
            bool hasConfig = !configVal.empty();

            // Determine type and get value/range
            if (spec.type.is_floating_point())
            {
                p.type = ParamType::Float;
                p.defaultFloat = spec.initializer_value.as_float[0];
                p.valueFloat = hasConfig ? pConfig->getOption<float>(spec.name) : p.defaultFloat;

                auto minIt = std::find_if(spec.annotations.begin(), spec.annotations.end(),
                    [](const auto& a) { return a.name == "ui_min"; });
                auto maxIt = std::find_if(spec.annotations.begin(), spec.annotations.end(),
                    [](const auto& a) { return a.name == "ui_max"; });

                if (minIt != spec.annotations.end())
                    p.minFloat = minIt->type.is_floating_point() ? minIt->value.as_float[0] : (float)minIt->value.as_int[0];
                if (maxIt != spec.annotations.end())
                    p.maxFloat = maxIt->type.is_floating_point() ? maxIt->value.as_float[0] : (float)maxIt->value.as_int[0];
            }
            else if (spec.type.is_integral())
            {
                if (spec.type.is_boolean())
                {
                    p.type = ParamType::Bool;
                    p.defaultBool = (spec.initializer_value.as_uint[0] != 0);
                    p.valueBool = hasConfig ? pConfig->getOption<bool>(spec.name) : p.defaultBool;
                }
                else
                {
                    p.type = ParamType::Int;
                    p.defaultInt = spec.initializer_value.as_int[0];
                    p.valueInt = hasConfig ? pConfig->getOption<int32_t>(spec.name) : p.defaultInt;

                    auto minIt = std::find_if(spec.annotations.begin(), spec.annotations.end(),
                        [](const auto& a) { return a.name == "ui_min"; });
                    auto maxIt = std::find_if(spec.annotations.begin(), spec.annotations.end(),
                        [](const auto& a) { return a.name == "ui_max"; });

                    if (minIt != spec.annotations.end())
                        p.minInt = minIt->type.is_integral() ? minIt->value.as_int[0] : (int)minIt->value.as_float[0];
                    if (maxIt != spec.annotations.end())
                        p.maxInt = maxIt->type.is_integral() ? maxIt->value.as_int[0] : (int)maxIt->value.as_float[0];
                }
            }

            // Get ui_step
            auto stepIt = std::find_if(spec.annotations.begin(), spec.annotations.end(),
                [](const auto& a) { return a.name == "ui_step"; });
            if (stepIt != spec.annotations.end())
                p.step = stepIt->type.is_floating_point() ? stepIt->value.as_float[0] : (float)stepIt->value.as_int[0];

            // Get ui_type
            auto typeIt = std::find_if(spec.annotations.begin(), spec.annotations.end(),
                [](const auto& a) { return a.name == "ui_type"; });
            if (typeIt != spec.annotations.end())
                p.uiType = typeIt->value.string_data;

            // Get ui_items (null-separated list for combo boxes)
            auto itemsIt = std::find_if(spec.annotations.begin(), spec.annotations.end(),
                [](const auto& a) { return a.name == "ui_items"; });
            if (itemsIt != spec.annotations.end())
            {
                std::string itemsStr = itemsIt->value.string_data;
                size_t start = 0;
                for (size_t i = 0; i <= itemsStr.size(); i++)
                {
                    if (i == itemsStr.size() || itemsStr[i] == '\0')
                    {
                        if (i > start)
                            p.items.push_back(itemsStr.substr(start, i - start));
                        start = i + 1;
                    }
                }
            }

            params.push_back(p);
        }

        return params;
    }

} // namespace vkBasalt
