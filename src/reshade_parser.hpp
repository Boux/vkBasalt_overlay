#ifndef RESHADE_PARSER_HPP_INCLUDED
#define RESHADE_PARSER_HPP_INCLUDED

#include <string>
#include <vector>

#include "imgui_overlay.hpp"
#include "effect_config.hpp"
#include "config.hpp"

namespace vkBasalt
{
    // Result of testing a shader for compilation errors
    struct ShaderTestResult
    {
        std::string effectName;     // Effect name (filename without extension)
        std::string filePath;       // Full path to .fx file
        bool success = false;       // True if shader compiled without errors
        std::string errorMessage;   // Error message if failed
    };

    // Parse a ReShade .fx file and extract its parameters without creating Vulkan resources.
    // effectName: display name for the effect (used in EffectParameter.effectName)
    // effectPath: full path to the .fx file
    // pConfig: config for getting includePath and current param values
    std::vector<EffectParameter> parseReshadeEffect(
        const std::string& effectName,
        const std::string& effectPath,
        Config* pConfig);

    // Test a ReShade .fx shader for compilation errors without creating Vulkan resources.
    // Returns a ShaderTestResult with success status and any error messages.
    ShaderTestResult testShaderCompilation(
        const std::string& effectName,
        const std::string& effectPath);

    // Extract user-configurable preprocessor definitions from a ReShade shader.
    // These are macros used via #ifndef/#ifdef that aren't built-in (like __RESHADE__).
    // Returns empty vector for built-in effects or if no user macros are found.
    std::vector<PreprocessorDefinition> extractPreprocessorDefinitions(
        const std::string& effectName,
        const std::string& effectPath);

} // namespace vkBasalt

#endif // RESHADE_PARSER_HPP_INCLUDED
