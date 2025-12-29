#ifndef RESHADE_PARSER_HPP_INCLUDED
#define RESHADE_PARSER_HPP_INCLUDED

#include <string>
#include <vector>

#include "imgui_overlay.hpp"
#include "config.hpp"

namespace vkBasalt
{
    // Parse a ReShade .fx file and extract its parameters without creating Vulkan resources.
    // effectName: display name for the effect (used in EffectParameter.effectName)
    // effectPath: full path to the .fx file
    // pConfig: config for getting includePath and current param values
    std::vector<EffectParameter> parseReshadeEffect(
        const std::string& effectName,
        const std::string& effectPath,
        Config* pConfig);

} // namespace vkBasalt

#endif // RESHADE_PARSER_HPP_INCLUDED
