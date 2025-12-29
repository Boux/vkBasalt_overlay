#include "config.hpp"
#include "config_paths.hpp"

#include <sstream>
#include <locale>
#include <array>

namespace vkBasalt
{
    Config::Config()
    {
        // Find vkBasalt.conf in standard locations
        const char* tmpHomeEnv     = std::getenv("XDG_DATA_HOME");
        std::string userConfigFile = tmpHomeEnv ? std::string(tmpHomeEnv) + "/vkBasalt/vkBasalt.conf"
                                                : std::string(std::getenv("HOME")) + "/.local/share/vkBasalt/vkBasalt.conf";

        const char* tmpConfigEnv      = std::getenv("XDG_CONFIG_HOME");
        std::string userXdgConfigFile = tmpConfigEnv ? std::string(tmpConfigEnv) + "/vkBasalt/vkBasalt.conf"
                                                     : std::string(std::getenv("HOME")) + "/.config/vkBasalt/vkBasalt.conf";

        const std::array<std::string, 5> configPaths = {
            userXdgConfigFile,
            userConfigFile,
            std::string(SYSCONFDIR) + "/vkBasalt.conf",
            std::string(SYSCONFDIR) + "/vkBasalt/vkBasalt.conf",
            std::string(DATADIR) + "/vkBasalt/vkBasalt.conf",
        };

        for (const auto& path : configPaths)
        {
            std::ifstream file(path);
            if (file.good())
            {
                Logger::info("base config: " + path);
                configFilePath = path;
                readConfigFile(file);
                updateLastModifiedTime();
                return;
            }
        }

        Logger::err("no vkBasalt.conf found");
    }

    Config::Config(const std::string& path)
    {
        std::ifstream file(path);
        if (!file.good())
        {
            Logger::err("failed to load config: " + path);
            return;
        }

        Logger::info("config: " + path);
        configFilePath = path;
        readConfigFile(file);
        updateLastModifiedTime();
    }

    Config::Config(const Config& other)
    {
        this->options          = other.options;
        this->overrides        = other.overrides;
        this->configFilePath   = other.configFilePath;
        this->lastModifiedTime = other.lastModifiedTime;
    }

    void Config::updateLastModifiedTime()
    {
        if (configFilePath.empty())
            return;

        struct stat fileStat;
        if (stat(configFilePath.c_str(), &fileStat) == 0)
            lastModifiedTime = fileStat.st_mtime;
    }

    bool Config::hasConfigChanged()
    {
        if (configFilePath.empty())
            return false;

        struct stat fileStat;
        if (stat(configFilePath.c_str(), &fileStat) != 0)
            return false;

        return fileStat.st_mtime != lastModifiedTime;
    }

    void Config::reload()
    {
        if (configFilePath.empty())
            return;

        std::ifstream file(configFilePath);
        if (!file.good())
        {
            Logger::err("failed to reload config: " + configFilePath);
            return;
        }

        Logger::info("reloading config: " + configFilePath);
        options.clear();
        readConfigFile(file);
        updateLastModifiedTime();
    }

    void Config::readConfigFile(std::ifstream& stream)
    {
        std::string line;
        while (std::getline(stream, line))
            readConfigLine(line);
    }

    void Config::readConfigLine(std::string line)
    {
        std::string key;
        std::string value;
        bool inQuotes    = false;
        bool foundEquals = false;

        auto appendChar = [&key, &value, &foundEquals](const char& c) {
            if (foundEquals)
                value += c;
            else
                key += c;
        };

        for (const char& c : line)
        {
            if (inQuotes)
            {
                if (c == '"')
                    inQuotes = false;
                else
                    appendChar(c);
                continue;
            }
            switch (c)
            {
                case '#': goto DONE;
                case '"': inQuotes = true; break;
                case '\t':
                case ' ': break;
                case '=': foundEquals = true; break;
                default: appendChar(c); break;
            }
        }

    DONE:
        if (!key.empty() && !value.empty())
        {
            Logger::info(key + " = " + value);
            options[key] = value;
        }
    }

    void Config::parseOption(const std::string& option, int32_t& result)
    {
        auto found = options.find(option);
        if (found != options.end())
        {
            try { result = std::stoi(found->second); }
            catch (...) { Logger::warn("invalid int32_t value for: " + option); }
        }
    }

    void Config::parseOption(const std::string& option, float& result)
    {
        auto found = options.find(option);
        if (found != options.end())
        {
            std::stringstream ss(found->second);
            ss.imbue(std::locale("C"));
            float value;
            ss >> value;

            if (ss.fail())
            {
                Logger::warn("invalid float value for: " + option);
                return;
            }

            // Check for trailing content (allow optional 'f' suffix)
            std::string rest;
            ss >> rest;
            if (!rest.empty() && rest != "f")
                Logger::warn("invalid float value for: " + option);
            else
                result = value;
        }
    }

    void Config::parseOption(const std::string& option, bool& result)
    {
        auto found = options.find(option);
        if (found != options.end())
        {
            if (found->second == "True" || found->second == "true" || found->second == "1")
                result = true;
            else if (found->second == "False" || found->second == "false" || found->second == "0")
                result = false;
            else
                Logger::warn("invalid bool value for: " + option);
        }
    }

    void Config::parseOption(const std::string& option, std::string& result)
    {
        auto found = options.find(option);
        if (found != options.end())
            result = found->second;
    }

    void Config::parseOption(const std::string& option, std::vector<std::string>& result)
    {
        auto found = options.find(option);
        if (found != options.end())
        {
            result = {};
            std::stringstream ss(found->second);
            std::string item;
            while (std::getline(ss, item, ':'))
                result.push_back(item);
        }
    }

    void Config::setOverride(const std::string& option, const std::string& value)
    {
        overrides[option] = value;
    }

    void Config::clearOverrides()
    {
        overrides.clear();
    }

    void Config::parseOverride(const std::string& value, int32_t& result)
    {
        try { result = std::stoi(value); }
        catch (...) { Logger::warn("invalid int32_t override value"); }
    }

    void Config::parseOverride(const std::string& value, float& result)
    {
        std::stringstream ss(value);
        ss.imbue(std::locale("C"));
        float parsed;
        ss >> parsed;
        if (!ss.fail())
            result = parsed;
        else
            Logger::warn("invalid float override value");
    }

    void Config::parseOverride(const std::string& value, bool& result)
    {
        if (value == "True" || value == "true" || value == "1")
            result = true;
        else if (value == "False" || value == "false" || value == "0")
            result = false;
        else
            Logger::warn("invalid bool override value");
    }

    void Config::parseOverride(const std::string& value, std::string& result)
    {
        result = value;
    }

    void Config::parseOverride(const std::string& value, std::vector<std::string>& result)
    {
        result = {};
        std::stringstream ss(value);
        std::string item;
        while (std::getline(ss, item, ':'))
            result.push_back(item);
    }

    std::unordered_map<std::string, std::string> Config::getEffectDefinitions() const
    {
        std::unordered_map<std::string, std::string> effects;
        for (const auto& [key, value] : options)
        {
            if (value.size() >= 3 && value.substr(value.size() - 3) == ".fx")
                effects[key] = value;
        }
        return effects;
    }

} // namespace vkBasalt
