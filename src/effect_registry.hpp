#ifndef EFFECT_REGISTRY_HPP_INCLUDED
#define EFFECT_REGISTRY_HPP_INCLUDED

#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include <memory>
#include <mutex>

#include "effect_config.hpp"
#include "config.hpp"

namespace vkBasalt
{
    // EffectRegistry is the single source of truth for all effect configurations.
    // UI reads/writes here, rendering reads from here.
    class EffectRegistry
    {
    public:
        // Initialize registry from config file
        void initialize(Config* pConfig);

        // Get all effect configs (enabled + disabled)
        const std::vector<EffectConfig>& getAllEffects() const { return effects; }

        // Get only enabled effects (for rendering)
        std::vector<EffectConfig> getEnabledEffects() const;

        // Get all parameters from all effects (for UI)
        std::vector<EffectParameter> getAllParameters() const;

        // Toggle effect enabled state
        void setEffectEnabled(const std::string& effectName, bool enabled);

        // Get enabled state for a specific effect
        bool isEffectEnabled(const std::string& effectName) const;

        // Get all effect enabled states as a map
        std::map<std::string, bool> getEffectEnabledStates() const;

        // Update a parameter value (UI -> registry)
        void setParameterValue(const std::string& effectName, const std::string& paramName, float value);
        void setParameterValue(const std::string& effectName, const std::string& paramName, int value);
        void setParameterValue(const std::string& effectName, const std::string& paramName, bool value);

        // Get parameter by name
        EffectParameter* getParameter(const std::string& effectName, const std::string& paramName);
        const EffectParameter* getParameter(const std::string& effectName, const std::string& paramName) const;

        // Get config reference for effects to read values
        Config* getConfig() const { return pConfig; }

        // Check if an effect is a built-in effect
        static bool isBuiltInEffect(const std::string& name);

        // Add an effect if not already present (for dynamically added effects)
        void ensureEffect(const std::string& name, const std::string& effectPath = "");

        // Check if effect exists in registry
        bool hasEffect(const std::string& name) const;

        // Get the file path for an effect (for ReShade effects)
        std::string getEffectFilePath(const std::string& name) const;

        // Get the effect type for an effect (base type name, e.g., "cas" for "cas.2")
        std::string getEffectType(const std::string& name) const;

        // Check if an effect is a built-in effect (by instance name)
        bool isEffectBuiltIn(const std::string& name) const;

    private:
        std::vector<EffectConfig> effects;
        Config* pConfig = nullptr;
        mutable std::mutex mutex;

        // Initialize built-in effect configs
        void initBuiltInEffect(const std::string& instanceName, const std::string& effectType);

        // Initialize ReShade effect config
        void initReshadeEffect(const std::string& name, const std::string& path);

        // Internal helpers (assume mutex is held)
        EffectConfig* findEffect(const std::string& effectName);
        const EffectConfig* findEffect(const std::string& effectName) const;
        EffectParameter* findParam(EffectConfig& effect, const std::string& paramName);
        const EffectParameter* findParam(const EffectConfig& effect, const std::string& paramName) const;
    };

} // namespace vkBasalt

#endif // EFFECT_REGISTRY_HPP_INCLUDED
