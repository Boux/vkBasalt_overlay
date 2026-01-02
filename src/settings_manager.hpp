#ifndef SETTINGS_MANAGER_HPP_INCLUDED
#define SETTINGS_MANAGER_HPP_INCLUDED

#include <string>
#include <mutex>

#include "config_serializer.hpp"

namespace vkBasalt
{
    // Single source of truth for all vkBasalt settings.
    // Similar to EffectRegistry for effect parameters.
    //
    // Usage:
    // - Call initialize() once at startup to load from config
    // - Read/write settings directly via getters/setters
    // - Call save() to persist changes to vkBasalt.conf
    class SettingsManager
    {
    public:
        // Initialize from vkBasalt.conf (call once at startup)
        void initialize();

        // Check if already initialized
        bool isInitialized() const { return initialized; }

        // Save current settings to vkBasalt.conf
        bool save();

        // Getters
        int getMaxEffects() const { return settings.maxEffects; }
        bool getOverlayBlockInput() const { return settings.overlayBlockInput; }
        const std::string& getToggleKey() const { return settings.toggleKey; }
        const std::string& getReloadKey() const { return settings.reloadKey; }
        const std::string& getOverlayKey() const { return settings.overlayKey; }
        bool getEnableOnLaunch() const { return settings.enableOnLaunch; }
        bool getDepthCapture() const { return settings.depthCapture; }
        float getDepthMaskThreshold() const { return settings.depthMaskThreshold; }
        bool getAutoApply() const { return settings.autoApply; }
        int getAutoApplyDelay() const { return settings.autoApplyDelay; }
        bool getShowDebugWindow() const { return settings.showDebugWindow; }

        // Setters (update in-memory state, call save() to persist)
        void setMaxEffects(int value) { settings.maxEffects = value; }
        void setOverlayBlockInput(bool value) { settings.overlayBlockInput = value; }
        void setToggleKey(const std::string& value) { settings.toggleKey = value; }
        void setReloadKey(const std::string& value) { settings.reloadKey = value; }
        void setOverlayKey(const std::string& value) { settings.overlayKey = value; }
        void setEnableOnLaunch(bool value) { settings.enableOnLaunch = value; }
        void setDepthCapture(bool value) { settings.depthCapture = value; }
        void setDepthMaskThreshold(float value) { settings.depthMaskThreshold = value; }
        void setAutoApply(bool value) { settings.autoApply = value; }
        void setAutoApplyDelay(int value) { settings.autoApplyDelay = value; }
        void setShowDebugWindow(bool value) { settings.showDebugWindow = value; }

        // Get raw settings struct (for bulk operations)
        const VkBasaltSettings& getSettings() const { return settings; }

    private:
        VkBasaltSettings settings;
        bool initialized = false;
    };

    // Global settings manager instance (like effectRegistry)
    extern SettingsManager settingsManager;

} // namespace vkBasalt

#endif // SETTINGS_MANAGER_HPP_INCLUDED
