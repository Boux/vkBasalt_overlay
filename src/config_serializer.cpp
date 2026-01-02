#include "config_serializer.hpp"
#include "logger.hpp"

#include <fstream>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <sys/stat.h>
#include <dirent.h>
#include <algorithm>

namespace vkBasalt
{
    std::string ConfigSerializer::getBaseConfigDir()
    {
        const char* xdgConfig = std::getenv("XDG_CONFIG_HOME");
        if (xdgConfig)
            return std::string(xdgConfig) + "/vkBasalt-overlay";

        const char* home = std::getenv("HOME");
        if (home)
            return std::string(home) + "/.config/vkBasalt-overlay";

        return "";
    }

    std::string ConfigSerializer::getConfigsDir()
    {
        std::string baseDir = getBaseConfigDir();
        if (baseDir.empty())
            return "";
        return baseDir + "/configs";
    }

    std::vector<std::string> ConfigSerializer::listConfigs()
    {
        std::vector<std::string> configs;
        std::string dir = getConfigsDir();

        DIR* d = opendir(dir.c_str());
        if (!d)
            return configs;

        struct dirent* entry;
        while ((entry = readdir(d)) != nullptr)
        {
            std::string name = entry->d_name;
            if (name.size() > 5 && name.substr(name.size() - 5) == ".conf")
                configs.push_back(name.substr(0, name.size() - 5));
        }
        closedir(d);

        std::sort(configs.begin(), configs.end());
        return configs;
    }

    static std::string joinEffects(const std::vector<std::string>& effects)
    {
        std::string result;
        for (size_t i = 0; i < effects.size(); i++)
        {
            if (i > 0)
                result += ":";
            result += effects[i];
        }
        return result;
    }

    bool ConfigSerializer::saveConfig(
        const std::string& configName,
        const std::vector<std::string>& effects,
        const std::vector<std::string>& disabledEffects,
        const std::vector<ConfigParam>& params,
        const std::map<std::string, std::string>& effectPaths,
        const std::vector<PreprocessorDefinition>& preprocessorDefs)
    {
        std::string configsDir = getConfigsDir();
        if (configsDir.empty())
        {
            Logger::err("Could not determine configs directory");
            return false;
        }

        mkdir(configsDir.c_str(), 0755);

        std::string filePath = configsDir + "/" + configName + ".conf";
        std::ofstream file(filePath);
        if (!file.is_open())
        {
            Logger::err("Could not open config file for writing: " + filePath);
            return false;
        }

        // Group params by effect
        std::map<std::string, std::vector<const ConfigParam*>> paramsByEffect;
        for (const auto& param : params)
            paramsByEffect[param.effectName].push_back(&param);

        // Group preprocessor defs by effect
        std::map<std::string, std::vector<const PreprocessorDefinition*>> defsByEffect;
        for (const auto& def : preprocessorDefs)
            defsByEffect[def.effectName].push_back(&def);

        // Write params grouped by effect (always prefix with effectName.paramName)
        // Also write effect path before params for each effect
        for (const auto& [effectName, effectParams] : paramsByEffect)
        {
            file << "# " << effectName << "\n";
            // Write effect path if available (for ReShade effects)
            auto pathIt = effectPaths.find(effectName);
            if (pathIt != effectPaths.end() && !pathIt->second.empty())
                file << effectName << " = " << pathIt->second << "\n";
            for (const auto* param : effectParams)
                file << param->effectName << "." << param->paramName << " = " << param->value << "\n";
            // Write preprocessor definitions for this effect (format: effectName#MACRO = value)
            auto defsIt = defsByEffect.find(effectName);
            if (defsIt != defsByEffect.end())
            {
                for (const auto* def : defsIt->second)
                    file << def->effectName << "#" << def->name << " = " << def->value << "\n";
            }
            file << "\n";
        }

        // Write preprocessor defs for effects that have defs but no params
        for (const auto& [effectName, effectDefs] : defsByEffect)
        {
            if (paramsByEffect.find(effectName) != paramsByEffect.end())
                continue;  // Already written with params
            file << "# " << effectName << "\n";
            auto pathIt = effectPaths.find(effectName);
            if (pathIt != effectPaths.end() && !pathIt->second.empty())
                file << effectName << " = " << pathIt->second << "\n";
            for (const auto* def : effectDefs)
                file << def->effectName << "#" << def->name << " = " << def->value << "\n";
            file << "\n";
        }

        // Write paths for effects that have no params or defs but do have paths
        for (const auto& [effectName, path] : effectPaths)
        {
            if (!path.empty() &&
                paramsByEffect.find(effectName) == paramsByEffect.end() &&
                defsByEffect.find(effectName) == defsByEffect.end())
            {
                file << "# " << effectName << "\n";
                file << effectName << " = " << path << "\n\n";
            }
        }

        // Write effects list (all effects, enabled + disabled)
        file << "effects = " << joinEffects(effects) << "\n";

        // Write disabled effects if any
        if (!disabledEffects.empty())
            file << "disabledEffects = " << joinEffects(disabledEffects) << "\n";

        file.close();
        Logger::info("Saved config to: " + filePath);
        return true;
    }

    bool ConfigSerializer::deleteConfig(const std::string& configName)
    {
        std::string configsDir = getConfigsDir();
        if (configsDir.empty())
            return false;

        std::string filePath = configsDir + "/" + configName + ".conf";
        if (std::remove(filePath.c_str()) == 0)
        {
            Logger::info("Deleted config: " + filePath);
            return true;
        }
        Logger::err("Failed to delete config: " + filePath);
        return false;
    }

    std::string ConfigSerializer::getDefaultConfigPath()
    {
        const char* home = std::getenv("HOME");
        if (home)
            return std::string(home) + "/.config/vkBasalt-overlay/default_config";
        return "";
    }

    bool ConfigSerializer::setDefaultConfig(const std::string& configName)
    {
        std::string path = getDefaultConfigPath();
        if (path.empty())
            return false;

        std::ofstream file(path);
        if (!file.is_open())
        {
            Logger::err("Could not write default config file: " + path);
            return false;
        }

        file << configName;
        file.close();
        Logger::info("Set default config: " + configName);
        return true;
    }

    std::string ConfigSerializer::getDefaultConfig()
    {
        std::string path = getDefaultConfigPath();
        if (path.empty())
            return "";

        std::ifstream file(path);
        if (!file.is_open())
            return "";

        std::string configName;
        std::getline(file, configName);
        return configName;
    }

    VkBasaltSettings ConfigSerializer::loadSettings()
    {
        VkBasaltSettings settings;
        std::string configPath = getBaseConfigDir() + "/vkBasalt.conf";

        std::ifstream file(configPath);
        if (!file.is_open())
            return settings;

        std::string line;
        while (std::getline(file, line))
        {
            // Skip comments and empty lines
            size_t start = line.find_first_not_of(" \t");
            if (start == std::string::npos || line[start] == '#')
                continue;

            size_t eq = line.find('=');
            if (eq == std::string::npos)
                continue;

            std::string key = line.substr(0, eq);
            std::string value = line.substr(eq + 1);

            // Trim whitespace
            auto trimWs = [](std::string& s) {
                size_t start = s.find_first_not_of(" \t");
                size_t end = s.find_last_not_of(" \t");
                s = (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
            };
            trimWs(key);
            trimWs(value);

            if (key == "maxEffects")
                settings.maxEffects = std::stoi(value);
            else if (key == "overlayBlockInput")
                settings.overlayBlockInput = (value == "true" || value == "1");
            else if (key == "toggleKey")
                settings.toggleKey = value;
            else if (key == "reloadKey")
                settings.reloadKey = value;
            else if (key == "overlayKey")
                settings.overlayKey = value;
            else if (key == "enableOnLaunch")
                settings.enableOnLaunch = (value == "true" || value == "1");
            else if (key == "depthCapture")
                settings.depthCapture = (value == "on");
            else if (key == "depthMaskThreshold")
                settings.depthMaskThreshold = std::stof(value);
            else if (key == "autoApply")
                settings.autoApply = (value == "true" || value == "1");
            else if (key == "autoApplyDelay")
                settings.autoApplyDelay = std::stoi(value);
            else if (key == "showDebugWindow")
                settings.showDebugWindow = (value == "true" || value == "1");
        }

        return settings;
    }

    bool ConfigSerializer::saveSettings(const VkBasaltSettings& settings)
    {
        std::string baseDir = getBaseConfigDir();
        if (baseDir.empty())
        {
            Logger::err("Could not determine config directory");
            return false;
        }

        mkdir(baseDir.c_str(), 0755);

        std::string configPath = baseDir + "/vkBasalt.conf";
        std::ofstream file(configPath);
        if (!file.is_open())
        {
            Logger::err("Could not open vkBasalt.conf for writing: " + configPath);
            return false;
        }

        // Write settings with comments
        file << "# vkBasalt configuration\n\n";

        file << "# Overlay settings\n";
        file << "overlayBlockInput = " << (settings.overlayBlockInput ? "true" : "false") << "\n";
        file << "maxEffects = " << settings.maxEffects << "\n";
        file << "autoApply = " << (settings.autoApply ? "true" : "false") << "\n";
        file << "autoApplyDelay = " << settings.autoApplyDelay << "\n";

        file << "\n# Key bindings\n";
        file << "toggleKey = " << settings.toggleKey << "\n";
        file << "reloadKey = " << settings.reloadKey << "\n";
        file << "overlayKey = " << settings.overlayKey << "\n";

        file << "\n# Startup behavior\n";
        file << "enableOnLaunch = " << (settings.enableOnLaunch ? "true" : "false") << "\n";

        file << "\n# Depth masking\n";
        file << "depthCapture = " << (settings.depthCapture ? "on" : "off") << "\n";
        file << "depthMaskThreshold = " << settings.depthMaskThreshold << "\n";

        file << "\n# Debug\n";
        file << "showDebugWindow = " << (settings.showDebugWindow ? "true" : "false") << "\n";

        file.close();
        Logger::info("Saved settings to: " + configPath);
        return true;
    }

    void ConfigSerializer::ensureConfigExists()
    {
        std::string baseDir = getBaseConfigDir();
        if (baseDir.empty())
            return;

        // Create directory if needed
        mkdir(baseDir.c_str(), 0755);

        std::string configPath = baseDir + "/vkBasalt.conf";

        // Check if file exists
        struct stat st;
        if (stat(configPath.c_str(), &st) == 0)
            return;  // File exists

        // Create with defaults
        VkBasaltSettings defaults;
        saveSettings(defaults);
        Logger::info("Created default vkBasalt.conf");
    }

    // Case-insensitive string comparison helper
    static bool equalsIgnoreCaseLocal(const std::string& a, const std::string& b)
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); i++)
        {
            if (std::tolower(static_cast<unsigned char>(a[i])) !=
                std::tolower(static_cast<unsigned char>(b[i])))
                return false;
        }
        return true;
    }

    // Scan a directory recursively for Shaders/ and Textures/ subdirectories
    static void scanDirectoryForShaders(
        const std::string& dir,
        std::vector<std::string>& shaderPaths,
        std::vector<std::string>& texturePaths)
    {
        try
        {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(
                dir, std::filesystem::directory_options::skip_permission_denied))
            {
                if (!entry.is_directory())
                    continue;

                std::string dirName = entry.path().filename().string();
                if (equalsIgnoreCaseLocal(dirName, "Shaders"))
                    shaderPaths.push_back(entry.path().string());
                else if (equalsIgnoreCaseLocal(dirName, "Textures"))
                    texturePaths.push_back(entry.path().string());
            }
        }
        catch (const std::filesystem::filesystem_error& e)
        {
            Logger::err("Error scanning directory " + dir + ": " + e.what());
        }
    }

    ShaderManagerConfig ConfigSerializer::loadShaderManagerConfig()
    {
        ShaderManagerConfig config;
        std::string configPath = getBaseConfigDir() + "/shader_manager.conf";

        std::ifstream file(configPath);
        if (!file.is_open())
        {
            // Config file doesn't exist - set up defaults
            std::string defaultReshadeDir = getBaseConfigDir() + "/reshade";

            // Create directories if they don't exist
            mkdir(defaultReshadeDir.c_str(), 0755);
            mkdir((defaultReshadeDir + "/Shaders").c_str(), 0755);
            mkdir((defaultReshadeDir + "/Textures").c_str(), 0755);

            config.parentDirectories.push_back(defaultReshadeDir);

            // Auto-scan to discover paths
            scanDirectoryForShaders(defaultReshadeDir,
                config.discoveredShaderPaths, config.discoveredTexturePaths);

            // Save the config so it persists
            saveShaderManagerConfig(config);
            Logger::info("Created default shader manager config with reshade directory");
            return config;
        }

        // File exists - parse it (respect user's choices, even if empty)
        std::string line;
        while (std::getline(file, line))
        {
            // Skip comments and empty lines
            size_t start = line.find_first_not_of(" \t");
            if (start == std::string::npos || line[start] == '#')
                continue;

            size_t eq = line.find('=');
            if (eq == std::string::npos)
                continue;

            std::string key = line.substr(0, eq);
            std::string value = line.substr(eq + 1);

            // Trim whitespace
            auto trimWs = [](std::string& s) {
                size_t start = s.find_first_not_of(" \t");
                size_t end = s.find_last_not_of(" \t");
                s = (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
            };
            trimWs(key);
            trimWs(value);

            if (key == "parentDir" && !value.empty())
                config.parentDirectories.push_back(value);
            else if (key == "shaderPath" && !value.empty())
                config.discoveredShaderPaths.push_back(value);
            else if (key == "texturePath" && !value.empty())
                config.discoveredTexturePaths.push_back(value);
        }

        return config;
    }

    bool ConfigSerializer::saveShaderManagerConfig(const ShaderManagerConfig& config)
    {
        std::string baseDir = getBaseConfigDir();
        if (baseDir.empty())
        {
            Logger::err("Could not determine config directory");
            return false;
        }

        mkdir(baseDir.c_str(), 0755);

        std::string configPath = baseDir + "/shader_manager.conf";
        std::ofstream file(configPath);
        if (!file.is_open())
        {
            Logger::err("Could not open shader_manager.conf for writing: " + configPath);
            return false;
        }

        file << "# Shader Manager configuration\n";
        file << "# Parent directories are scanned recursively for Shaders/ and Textures/ subdirs\n\n";

        file << "# Parent directories (user-added)\n";
        for (const auto& dir : config.parentDirectories)
            file << "parentDir = " << dir << "\n";

        file << "\n# Discovered shader paths (auto-generated on scan)\n";
        for (const auto& path : config.discoveredShaderPaths)
            file << "shaderPath = " << path << "\n";

        file << "\n# Discovered texture paths (auto-generated on scan)\n";
        for (const auto& path : config.discoveredTexturePaths)
            file << "texturePath = " << path << "\n";

        file.close();
        Logger::info("Saved shader manager config to: " + configPath);
        return true;
    }

} // namespace vkBasalt
