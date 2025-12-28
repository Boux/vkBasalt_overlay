#include "config_serializer.hpp"
#include "logger.hpp"

#include <fstream>
#include <cstdlib>
#include <sys/stat.h>
#include <dirent.h>
#include <algorithm>

namespace vkBasalt
{
    std::string ConfigSerializer::getConfigsDir()
    {
        std::string configDir;
        const char* home = std::getenv("HOME");
        if (home)
            configDir = std::string(home) + "/.config/vkBasalt/configs";
        return configDir;
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

    bool ConfigSerializer::saveConfig(
        const std::string& configName,
        const std::vector<std::string>& effects,
        const std::vector<EffectParam>& params)
    {
        std::string configsDir = getConfigsDir();
        if (configsDir.empty())
        {
            Logger::err("Could not determine configs directory");
            return false;
        }

        // Ensure configs directory exists
        mkdir(configsDir.c_str(), 0755);

        std::string filePath = configsDir + "/" + configName + ".conf";
        std::ofstream file(filePath);
        if (!file.is_open())
        {
            Logger::err("Could not open config file for writing: " + filePath);
            return false;
        }

        // Group params by effect
        std::map<std::string, std::vector<const EffectParam*>> paramsByEffect;
        for (const auto& param : params)
            paramsByEffect[param.effectName].push_back(&param);

        // Write params grouped by effect with comments
        for (const auto& [effectName, effectParams] : paramsByEffect)
        {
            file << "# " << effectName << "\n";
            for (const auto* param : effectParams)
                file << param->paramName << " = " << param->value << "\n";
            file << "\n";
        }

        // Write effects list
        std::string effectsList;
        for (size_t i = 0; i < effects.size(); i++)
        {
            if (i > 0)
                effectsList += ":";
            effectsList += effects[i];
        }
        file << "effects = " << effectsList << "\n";

        file.close();
        Logger::info("Saved config to: " + filePath);
        return true;
    }

} // namespace vkBasalt
