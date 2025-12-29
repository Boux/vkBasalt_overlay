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

    struct EffectConfig
    {
        std::string name;       // Instance name: "cas", "cas.2", "Clarity", etc.
        std::string effectType; // Base type: "cas", "Clarity" (for finding shader/identifying built-in)
        std::string filePath;   // For ReShade: path to .fx file, empty for built-in
        EffectType type = EffectType::BuiltIn;
        bool enabled = true;
        std::vector<EffectParameter> parameters;
    };

} // namespace vkBasalt

#endif // EFFECT_CONFIG_HPP_INCLUDED
