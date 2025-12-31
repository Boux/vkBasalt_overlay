#ifndef EFFECT_CONFIG_HPP_INCLUDED
#define EFFECT_CONFIG_HPP_INCLUDED

#include <string>
#include <vector>

#include "imgui_overlay.hpp"

namespace vkBasalt
{
    enum class EffectType
    {
        BuiltIn,  // cas, dls, fxaa, smaa, deband, lut
        ReShade   // .fx files
    };

    // Preprocessor definition extracted from ReShade shader
    // These are user-configurable compile-time constants (#define macros)
    struct PreprocessorDefinition
    {
        std::string name;           // Macro name, e.g., "ENABLE_SCANLINES"
        std::string value;          // Current value (will be passed to compiler)
        std::string defaultValue;   // Default from shader or "1"
        std::string effectName;     // Which effect this belongs to
    };

    struct EffectConfig
    {
        std::string name;       // Instance name: "cas", "cas.2", "Clarity", etc.
        std::string effectType; // Base type: "cas", "Clarity" (for finding shader/identifying built-in)
        std::string filePath;   // For ReShade: path to .fx file, empty for built-in
        EffectType type = EffectType::BuiltIn;
        bool enabled = true;
        std::vector<EffectParameter> parameters;
        std::vector<PreprocessorDefinition> preprocessorDefs;  // ReShade: user-configurable macros
        std::string compileError;  // Empty if compiled successfully, error message if failed
        bool hasFailed() const { return !compileError.empty(); }
    };

} // namespace vkBasalt

#endif // EFFECT_CONFIG_HPP_INCLUDED
