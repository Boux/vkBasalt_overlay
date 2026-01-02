#ifndef CONFIG_SERIALIZER_HPP_INCLUDED
#define CONFIG_SERIALIZER_HPP_INCLUDED

#include <string>
#include <vector>
#include <map>

#include "effects/effect_config.hpp"

namespace vkBasalt
{
    // Serialized parameter format for config files
    struct ConfigParam
    {
        std::string effectName;
        std::string paramName;
        std::string value;
    };

    // Global vkBasalt settings (from vkBasalt.conf)
    struct VkBasaltSettings
    {
        int maxEffects = 10;
        bool overlayBlockInput = false;
        std::string toggleKey = "Home";
        std::string reloadKey = "F10";
        std::string overlayKey = "End";
        bool enableOnLaunch = true;
        bool depthCapture = false;
        bool autoApply = true;  // Auto-apply changes without clicking Apply
        int autoApplyDelay = 200;  // ms delay before auto-applying changes
        bool showDebugWindow = false;  // Show debug window with raw effect registry data
    };

    // Shader Manager configuration (from shader_manager.conf)
    struct ShaderManagerConfig
    {
        std::vector<std::string> parentDirectories;       // User-added parent dirs to scan
        std::vector<std::string> discoveredShaderPaths;   // Auto-discovered Shaders/ dirs
        std::vector<std::string> discoveredTexturePaths;  // Auto-discovered Textures/ dirs
    };

    class ConfigSerializer
    {
    public:
        // Save a game-specific config to ~/.config/vkBasalt-overlay/configs/<name>.conf
        // effects: all effects in the list (enabled + disabled)
        // disabledEffects: effects that are unchecked (won't be rendered)
        // params: all effect parameters
        // effectPaths: map of effect name to shader file path (for ReShade effects with custom names)
        // preprocessorDefs: preprocessor definitions to save (format: effectName#MACRO = value)
        static bool saveConfig(
            const std::string& configName,
            const std::vector<std::string>& effects,
            const std::vector<std::string>& disabledEffects,
            const std::vector<ConfigParam>& params,
            const std::map<std::string, std::string>& effectPaths = {},
            const std::vector<PreprocessorDefinition>& preprocessorDefs = {});

        // Get the base config directory path (~/.config/vkBasalt-overlay/)
        static std::string getBaseConfigDir();

        // Get the configs directory path (~/.config/vkBasalt-overlay/configs/)
        static std::string getConfigsDir();

        // List available config files
        static std::vector<std::string> listConfigs();

        // Delete a config file
        static bool deleteConfig(const std::string& configName);

        // Default config management
        static bool setDefaultConfig(const std::string& configName);
        static std::string getDefaultConfig();
        static std::string getDefaultConfigPath();

        // Global settings management (vkBasalt.conf)
        static VkBasaltSettings loadSettings();
        static bool saveSettings(const VkBasaltSettings& settings);

        // Shader Manager config (shader_manager.conf)
        static ShaderManagerConfig loadShaderManagerConfig();
        static bool saveShaderManagerConfig(const ShaderManagerConfig& config);

        // Ensure vkBasalt.conf exists with defaults (call early at startup)
        static void ensureConfigExists();
    };

} // namespace vkBasalt

#endif // CONFIG_SERIALIZER_HPP_INCLUDED
