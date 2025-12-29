#ifndef CONFIG_HPP_INCLUDED
#define CONFIG_HPP_INCLUDED
#include <vector>
#include <fstream>
#include <string>
#include <iostream>
#include <vector>
#include <unordered_map>
#include <cstdlib>
#include <sys/stat.h>

#include "vulkan_include.hpp"

namespace vkBasalt
{
    class Config
    {
    public:
        Config();  // Finds and loads vkBasalt.conf
        Config(const std::string& path);  // Loads specific config file
        Config(const Config& other);

        // Set a fallback config for options not found in this config
        void setFallback(Config* fallback) { pFallback = fallback; }

        template<typename T>
        T getOption(const std::string& option, const T& defaultValue = {})
        {
            // Check overrides first (in-memory values take precedence)
            auto it = overrides.find(option);
            if (it != overrides.end())
            {
                T result = defaultValue;
                parseOverride(it->second, result);
                return result;
            }

            // Check this config's options
            if (options.find(option) != options.end())
            {
                T result = defaultValue;
                parseOption(option, result);
                return result;
            }

            // Check fallback config if set
            if (pFallback)
                return pFallback->getOption(option, defaultValue);

            return defaultValue;
        }

        // In-memory override support (does not modify config file)
        void setOverride(const std::string& option, const std::string& value);
        void clearOverrides();
        bool hasOverrides() const { return !overrides.empty(); }

        // Hot-reload support
        bool        hasConfigChanged();
        void        reload();
        std::string getConfigFilePath() const { return configFilePath; }

        // Get all effect definitions (keys whose values are .fx file paths)
        std::unordered_map<std::string, std::string> getEffectDefinitions() const;

    private:
        std::unordered_map<std::string, std::string> options;
        std::unordered_map<std::string, std::string> overrides;  // In-memory overrides
        std::string                                  configFilePath;
        time_t                                       lastModifiedTime = 0;
        Config*                                      pFallback = nullptr;

        void readConfigLine(std::string line);
        void readConfigFile(std::ifstream& stream);
        void updateLastModifiedTime();

        void parseOption(const std::string& option, int32_t& result);
        void parseOption(const std::string& option, float& result);
        void parseOption(const std::string& option, bool& result);
        void parseOption(const std::string& option, std::string& result);
        void parseOption(const std::string& option, std::vector<std::string>& result);

        // Parse override value directly from string
        void parseOverride(const std::string& value, int32_t& result);
        void parseOverride(const std::string& value, float& result);
        void parseOverride(const std::string& value, bool& result);
        void parseOverride(const std::string& value, std::string& result);
        void parseOverride(const std::string& value, std::vector<std::string>& result);
    };
} // namespace vkBasalt

#endif // CONFIG_HPP_INCLUDED
