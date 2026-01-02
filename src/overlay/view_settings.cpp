#include "imgui_overlay.hpp"
#include "settings_manager.hpp"
#include "logger.hpp"

#include <algorithm>
#include <cstring>
#include <functional>

#include "imgui/imgui.h"

namespace vkBasalt
{
    void ImGuiOverlay::renderSettingsView(const KeyboardState& keyboard)
    {
        // Helper to save settings to file
        auto saveSettings = [&]() {
            settingsManager.save();
            settingsSaved = true;
        };

        ImGui::BeginChild("SettingsContent", ImVec2(0, 0), false);

        ImGui::Text("Key Bindings");
        ImGui::Separator();
        ImGui::TextDisabled("Click a button and press any key to set binding");

        // Helper lambda to render a keybind button
        // Uses local char buffer for display, updates settingsManager on change
        auto renderKeyBind = [&](const char* label, const char* tooltip,
                                 const std::string& currentKey,
                                 std::function<void(const std::string&)> setter,
                                 int bindingId) {
            ImGui::Text("%s", label);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", tooltip);
            ImGui::SameLine(150);

            bool isListening = (listeningForKey == bindingId);
            const char* buttonText = isListening ? "Press a key..." : currentKey.c_str();

            if (isListening)
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.1f, 1.0f));

            if (ImGui::Button(buttonText, ImVec2(100, 0)))
                listeningForKey = isListening ? 0 : bindingId;

            if (isListening)
                ImGui::PopStyleColor();

            // Capture key if listening
            if (isListening && !keyboard.lastKeyName.empty())
            {
                setter(keyboard.lastKeyName);
                listeningForKey = 0;
                saveSettings();
            }
        };

        renderKeyBind("Toggle Effects:", "Key to enable/disable all effects",
                      settingsManager.getToggleKey(),
                      [](const std::string& key) { settingsManager.setToggleKey(key); }, 1);
        renderKeyBind("Reload Config:", "Key to reload the configuration file",
                      settingsManager.getReloadKey(),
                      [](const std::string& key) { settingsManager.setReloadKey(key); }, 2);
        renderKeyBind("Toggle Overlay:", "Key to show/hide this overlay",
                      settingsManager.getOverlayKey(),
                      [](const std::string& key) { settingsManager.setOverlayKey(key); }, 3);

        ImGui::Spacing();
        ImGui::Text("Overlay Options");
        ImGui::Separator();

        bool blockInput = settingsManager.getOverlayBlockInput();
        if (ImGui::Checkbox("Block Input When Overlay Open", &blockInput))
        {
            settingsManager.setOverlayBlockInput(blockInput);
            saveSettings();
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("When enabled, keyboard and mouse input is captured by the overlay.");
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Warning: Experimental feature! May cause some games to freeze.");
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Also blocks ALL input system-wide, even outside the game window!");
            ImGui::EndTooltip();
        }

        ImGui::Text("Max Effects (requires restart):");
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("Maximum number of effects that can be active simultaneously.");
            ImGui::Text("Changes require restarting the application.");
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "Warning: High values use significant VRAM");
            ImGui::EndTooltip();
        }
        ImGui::SetNextItemWidth(100);
        int maxEffectsVal = settingsManager.getMaxEffects();
        if (ImGui::InputInt("##maxEffects", &maxEffectsVal))
        {
            maxEffectsVal = std::clamp(maxEffectsVal, 1, 200);
            settingsManager.setMaxEffects(maxEffectsVal);
            maxEffects = static_cast<size_t>(maxEffectsVal);
            saveSettings();
        }

        // Show VRAM estimate based on current resolution (2 images per slot, 4 bytes per pixel)
        float bytesPerSlot = 2.0f * currentWidth * currentHeight * 4.0f;
        int estimatedVramMB = static_cast<int>((maxEffectsVal * bytesPerSlot) / (1024.0f * 1024.0f));
        ImGui::SameLine();
        if (maxEffectsVal > 20)
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "~%d MB @ %ux%u", estimatedVramMB, currentWidth, currentHeight);
        else
            ImGui::TextDisabled("~%d MB @ %ux%u", estimatedVramMB, currentWidth, currentHeight);

        bool autoApply = settingsManager.getAutoApply();
        if (ImGui::Checkbox("Auto-apply Changes", &autoApply))
        {
            settingsManager.setAutoApply(autoApply);
            saveSettings();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Automatically apply parameter changes after a short delay.\nDisable to require manual Apply button clicks.");

        if (autoApply)
        {
            ImGui::Indent();
            ImGui::Text("Delay:");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Delay before automatically applying changes.\nLower values feel more responsive, higher values reduce stutter.");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120);
            int delayVal = settingsManager.getAutoApplyDelay();
            if (ImGui::SliderInt("##autoApplyDelay", &delayVal, 20, 1000, "%d ms"))
                settingsManager.setAutoApplyDelay(delayVal);
            if (ImGui::IsItemDeactivatedAfterEdit())
                saveSettings();
            ImGui::Unindent();
        }

        ImGui::Spacing();
        ImGui::Text("Startup Behavior");
        ImGui::Separator();

        bool enableOnLaunch = settingsManager.getEnableOnLaunch();
        if (ImGui::Checkbox("Enable Effects on Launch", &enableOnLaunch))
        {
            settingsManager.setEnableOnLaunch(enableOnLaunch);
            saveSettings();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("If enabled, effects are active when the game starts.\nIf disabled, effects start off and must be toggled on.");

        bool depthCapture = settingsManager.getDepthCapture();
        if (ImGui::Checkbox("Depth Capture (requires restart)", &depthCapture))
        {
            settingsManager.setDepthCapture(depthCapture);
            saveSettings();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Enable depth buffer capture for effects that use depth.\nMay impact performance. Most effects don't need this.\nChanges require restarting the application.");

        ImGui::Spacing();
        ImGui::Text("Debug");
        ImGui::Separator();

        bool showDebugWindow = settingsManager.getShowDebugWindow();
        if (ImGui::Checkbox("Show Debug Window", &showDebugWindow))
        {
            settingsManager.setShowDebugWindow(showDebugWindow);
            Logger::setHistoryEnabled(showDebugWindow);
            saveSettings();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Show debug window with effect registry data and log output.");

        ImGui::EndChild();
    }

} // namespace vkBasalt
