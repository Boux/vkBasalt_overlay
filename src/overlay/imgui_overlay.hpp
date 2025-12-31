#ifndef IMGUI_OVERLAY_HPP_INCLUDED
#define IMGUI_OVERLAY_HPP_INCLUDED

#include <vector>
#include <memory>
#include <string>
#include <chrono>
#include <map>
#include <set>

#include "vulkan_include.hpp"
#include "logical_device.hpp"
#include "keyboard_input.hpp"

namespace vkBasalt
{
    class Effect;
    class EffectRegistry;

    enum class ParamType
    {
        Float,
        Int,
        Bool
    };

    struct EffectParameter
    {
        std::string effectName;   // Which effect this belongs to (e.g., "cas", "Clarity.fx")
        std::string name;         // Parameter name (e.g., "casSharpness")
        std::string label;        // Display label (from ui_label or name)
        ParamType type = ParamType::Float;
        float valueFloat = 0.0f;
        int valueInt = 0;
        bool valueBool = false;
        float defaultFloat = 0.0f;
        int defaultInt = 0;
        bool defaultBool = false;
        float minFloat = 0.0f;
        float maxFloat = 1.0f;
        int minInt = 0;
        int maxInt = 100;
        float step = 0.0f;                    // ui_step - increment step for sliders
        std::string uiType;                   // ui_type - "slider", "drag", "combo", etc.
        std::vector<std::string> items;       // ui_items - combo box options
        std::string tooltip;                  // ui_tooltip - hover description
    };

    struct OverlayState
    {
        std::vector<std::string> effectNames;           // Effects in current config
        std::vector<std::string> disabledEffects;       // Effects that are unchecked (in list but not rendered)
        std::vector<std::string> currentConfigEffects;  // ReShade effects from current config (e.g., tunic.conf)
        std::vector<std::string> defaultConfigEffects;  // ReShade effects from default vkBasalt.conf (no duplicates)
        std::map<std::string, std::string> effectPaths; // Effect name -> file path (for reshade effects)
        std::string configPath;
        std::string configName;  // Just the filename (e.g., "tunic.conf")
        bool effectsEnabled = true;
        std::vector<EffectParameter> parameters;
    };

    // Persistent state that survives swapchain recreation
    struct OverlayPersistentState
    {
        std::vector<std::string> selectedEffects;
        std::vector<EffectParameter> editableParams;
        bool autoApply = true;
        bool visible = false;
        bool initialized = false;  // True once user has interacted with overlay
    };

    class ImGuiOverlay
    {
    public:
        ImGuiOverlay(LogicalDevice* device, VkFormat swapchainFormat, uint32_t imageCount, OverlayPersistentState* persistentState);
        ~ImGuiOverlay();

        void toggle();
        bool isVisible() const { return visible; }

        void updateState(const OverlayState& state);

        // Returns modified parameters when Apply is clicked, empty otherwise
        std::vector<EffectParameter> getModifiedParams();
        bool hasModifiedParams() const { return applyRequested; }
        void clearApplyRequest() { applyRequested = false; }

        // Config switching
        bool hasPendingConfig() const { return !pendingConfigPath.empty(); }
        std::string getPendingConfigPath() const { return pendingConfigPath; }
        void clearPendingConfig() { pendingConfigPath.clear(); }

        // Effects toggle (global on/off)
        bool hasToggleEffectsRequest() const { return toggleEffectsRequested; }
        void clearToggleEffectsRequest() { toggleEffectsRequested = false; }

        // Set the effect registry (single source of truth for enabled states)
        void setEffectRegistry(EffectRegistry* registry) { pEffectRegistry = registry; }

        // Trigger debounced reload (for config switch)
        void markDirty() { paramsDirty = true; lastChangeTime = std::chrono::steady_clock::now(); }

        // Settings were saved (keybindings need reload)
        bool hasSettingsSaved() const { return settingsSaved; }
        void clearSettingsSaved() { settingsSaved = false; }

        // Shader paths were changed (effect list needs refresh)
        bool hasShaderPathsChanged() const { return shaderPathsChanged; }
        void clearShaderPathsChanged() { shaderPathsChanged = false; }

        // Returns list of effects that should be active (enabled, for reloading)
        std::vector<std::string> getActiveEffects() const;

        // Returns all selected effects (enabled + disabled, for parameter collection)
        const std::vector<std::string>& getSelectedEffects() const { return selectedEffects; }

        // Set effects list (when loading a different config)
        // disabledEffects: effects that should be unchecked (in list but not rendered)
        void setSelectedEffects(const std::vector<std::string>& effects,
                                const std::vector<std::string>& disabledEffects = {});

        VkCommandBuffer recordFrame(uint32_t imageIndex, VkImageView imageView, uint32_t width, uint32_t height);

    private:
        void initVulkanBackend(VkFormat swapchainFormat, uint32_t imageCount);
        void saveToPersistentState();
        void saveCurrentConfig();

        // View rendering methods (implemented in separate files)
        void renderAddEffectsView();
        void renderConfigManagerView();
        void renderSettingsView(const KeyboardState& keyboard);
        void renderShaderManagerView();
        void renderShaderTestSection();  // Shader test UI (part of shader manager)
        void renderMainView(const KeyboardState& keyboard);

        LogicalDevice* pLogicalDevice;
        OverlayPersistentState* pPersistentState;
        EffectRegistry* pEffectRegistry = nullptr;  // Single source of truth for enabled states
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> commandBuffers;
        VkFormat swapchainFormat = VK_FORMAT_UNDEFINED;
        uint32_t imageCount = 0;
        OverlayState state;
        std::vector<EffectParameter> editableParams;  // Persistent editable values
        std::vector<std::string> selectedEffects;           // Effects user has selected (ordered)
        std::vector<std::pair<std::string, std::string>> pendingAddEffects;  // {instanceName, effectType} to add
        bool inSelectionMode = false;
        int insertPosition = -1;  // Position to insert effects (-1 = append to end)
        char addEffectsSearch[64] = "";  // Search filter for add effects view
        bool inConfigManageMode = false;
        int currentTab = 0;  // 0=Effects, 1=Shaders, 2=Settings
        std::vector<std::string> configList;

        // Shader Manager state
        std::vector<std::string> shaderMgrParentDirs;
        std::vector<std::string> shaderMgrShaderPaths;
        std::vector<std::string> shaderMgrTexturePaths;
        bool shaderMgrInitialized = false;

        // Shader test state
        bool shaderTestRunning = false;
        bool shaderTestComplete = false;
        size_t shaderTestCurrentIndex = 0;
        int shaderTestDuplicateCount = 0;  // Number of duplicate shaders skipped
        std::vector<std::pair<std::string, std::string>> shaderTestQueue;  // {effectName, filePath}
        std::vector<std::tuple<std::string, std::string, bool, std::string>> shaderTestResults;  // {name, path, success, error}

        // Settings state (editable copies of config values)
        int settingsMaxEffects = 10;
        bool settingsBlockInput = false;
        char settingsToggleKey[32] = "Home";
        char settingsReloadKey[32] = "F10";
        char settingsOverlayKey[32] = "End";
        bool settingsEnableOnLaunch = true;
        bool settingsDepthCapture = false;
        bool settingsInitialized = false;
        int listeningForKey = 0;  // 0=none, 1=toggle, 2=reload, 3=overlay
        bool settingsSaved = false;  // True when settings saved, cleared by basalt.cpp
        bool shaderPathsChanged = false;  // True when shader manager saved, cleared by basalt.cpp
        size_t maxEffects = 10;
        int dragSourceIndex = -1;   // Index of effect being dragged, -1 if none
        int dragTargetIndex = -1;   // Index where effect will be dropped
        bool isDragging = false;    // True while actively dragging
        bool applyRequested = false;
        bool toggleEffectsRequested = false;
        bool autoApply = true;
        bool paramsDirty = false;  // True when params changed, waiting for debounce
        std::chrono::steady_clock::time_point lastChangeTime;
        bool visible = false;
        bool initialized = false;
        bool backendInitialized = false;
        char saveConfigName[64] = "";
        std::string pendingConfigPath;
    };

} // namespace vkBasalt

#endif // IMGUI_OVERLAY_HPP_INCLUDED
