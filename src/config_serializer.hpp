#ifndef CONFIG_SERIALIZER_HPP_INCLUDED
#define CONFIG_SERIALIZER_HPP_INCLUDED

#include <string>
#include <vector>
#include <map>

namespace vkBasalt
{
    struct EffectParam
    {
        std::string effectName;
        std::string paramName;
        std::string value;
    };

    class ConfigSerializer
    {
    public:
        // Save a game-specific config to ~/.config/vkBasalt/configs/<name>.conf
        // Only saves effects list and parameters that differ from defaults
        static bool saveConfig(
            const std::string& configName,
            const std::vector<std::string>& effects,
            const std::vector<EffectParam>& params);

        // Get the configs directory path
        static std::string getConfigsDir();

        // List available config files
        static std::vector<std::string> listConfigs();
    };

} // namespace vkBasalt

#endif // CONFIG_SERIALIZER_HPP_INCLUDED
