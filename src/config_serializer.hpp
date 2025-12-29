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
        // effects: all effects in the list (enabled + disabled)
        // disabledEffects: effects that are unchecked (won't be rendered)
        // params: all effect parameters
        static bool saveConfig(
            const std::string& configName,
            const std::vector<std::string>& effects,
            const std::vector<std::string>& disabledEffects,
            const std::vector<EffectParam>& params);

        // Get the configs directory path
        static std::string getConfigsDir();

        // List available config files
        static std::vector<std::string> listConfigs();

        // Delete a config file
        static bool deleteConfig(const std::string& configName);

        // Default config management
        static bool setDefaultConfig(const std::string& configName);
        static std::string getDefaultConfig();
        static std::string getDefaultConfigPath();
    };

} // namespace vkBasalt

#endif // CONFIG_SERIALIZER_HPP_INCLUDED
