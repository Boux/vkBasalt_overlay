#include "imgui_overlay.hpp"
#include "settings_manager.hpp"
#include "logger.hpp"

#include <cstring>

#include "imgui/imgui.h"

namespace vkBasalt
{
    void ImGuiOverlay::renderSettingsView(const KeyboardState& keyboard)
    {
        // Helper to save settings to config file
        auto saveSettings = [&]() {
            settingsManager.save();
            settingsSaved = true;
        };

        // Sync local key buffers from settings manager (for ImGui text editing)
        // Only on first frame or when not listening for keys
        if (!settingsInitialized)
        {
            strncpy(settingsToggleKey, settingsManager.getToggleKey().c_str(), sizeof(settingsToggleKey) - 1);
            strncpy(settingsReloadKey, settingsManager.getReloadKey().c_str(), sizeof(settingsReloadKey) - 1);
            strncpy(settingsOverlayKey, settingsManager.getOverlayKey().c_str(), sizeof(settingsOverlayKey) - 1);
            settingsInitialized = true;
        }

        ImGui::BeginChild("SettingsContent", ImVec2(0, 0), false);

        ImGui::Text("Key Bindings");
        ImGui::Separator();
        ImGui::TextDisabled("Click a button and press any key to set binding");

        // Helper lambda to render a keybind button
        auto renderKeyBind = [&](const char* label, const char* tooltip, char* keyBuffer, size_t bufSize, int bindingId,
                                 void (SettingsManager::*setter)(const std::string&)) {
            ImGui::Text("%s", label);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", tooltip);
            ImGui::SameLine(150);

            bool isListening = (listeningForKey == bindingId);
            const char* buttonText = isListening ? "Press a key..." : keyBuffer;

            if (isListening)
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.1f, 1.0f));

            if (ImGui::Button(buttonText, ImVec2(100, 0)))
                listeningForKey = isListening ? 0 : bindingId;

            if (isListening)
                ImGui::PopStyleColor();

            // Capture key if listening
            if (isListening && !keyboard.lastKeyName.empty())
            {
                strncpy(keyBuffer, keyboard.lastKeyName.c_str(), bufSize - 1);
                keyBuffer[bufSize - 1] = '\0';
                (settingsManager.*setter)(keyBuffer);
                listeningForKey = 0;
                saveSettings();
            }
        };

        renderKeyBind("Toggle Effects:", "Key to enable/disable all effects",
                      settingsToggleKey, sizeof(settingsToggleKey), 1, &SettingsManager::setToggleKey);
        renderKeyBind("Reload Config:", "Key to reload the configuration file",
                      settingsReloadKey, sizeof(settingsReloadKey), 2, &SettingsManager::setReloadKey);
        renderKeyBind("Toggle Overlay:", "Key to show/hide this overlay",
                      settingsOverlayKey, sizeof(settingsOverlayKey), 3, &SettingsManager::setOverlayKey);

        ImGui::Spacing();
        ImGui::Text("Overlay Options");
        ImGui::Separator();

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
        int maxEffectsValue = settingsManager.getMaxEffects();
        if (ImGui::InputInt("##maxEffects", &maxEffectsValue))
        {
            if (maxEffectsValue < 1) maxEffectsValue = 1;
            if (maxEffectsValue > 200) maxEffectsValue = 200;
            settingsManager.setMaxEffects(maxEffectsValue);
            maxEffects = static_cast<size_t>(maxEffectsValue);
            saveSettings();
        }

        // Show VRAM estimate based on current resolution (2 images per slot, 4 bytes per pixel)
        float bytesPerSlot = 2.0f * currentWidth * currentHeight * 4.0f;
        int estimatedVramMB = static_cast<int>((settingsManager.getMaxEffects() * bytesPerSlot) / (1024.0f * 1024.0f));
        ImGui::SameLine();
        if (settingsManager.getMaxEffects() > 20)
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "~%d MB @ %ux%u", estimatedVramMB, currentWidth, currentHeight);
        else
            ImGui::TextDisabled("~%d MB @ %ux%u", estimatedVramMB, currentWidth, currentHeight);

        ImGui::Text("Auto-apply Delay:");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Delay before automatically applying parameter changes.\nLower values feel more responsive, higher values reduce stutter.");
        ImGui::SetNextItemWidth(150);
        int autoApplyDelayValue = settingsManager.getAutoApplyDelay();
        if (ImGui::SliderInt("##autoApplyDelay", &autoApplyDelayValue, 20, 1000, "%d ms"))
            settingsManager.setAutoApplyDelay(autoApplyDelayValue);  // Update immediately while dragging
        if (ImGui::IsItemDeactivatedAfterEdit())
            saveSettings();  // Save to file only on release

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

        ImGui::Spacing();
        ImGui::Text("Advanced Options");
        ImGui::Separator();

        bool blockInput = settingsManager.getOverlayBlockInput();
        if (ImGui::Checkbox("Block Input When Overlay Open", &blockInput))
        {
            settingsManager.setOverlayBlockInput(blockInput);
            saveSettings();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("When enabled, keyboard and mouse input is blocked\nfrom reaching the game while the overlay is open.");

        bool depthCapture = settingsManager.getDepthCapture();
        if (ImGui::Checkbox("Depth Masking (experimental)", &depthCapture))
        {
            settingsManager.setDepthCapture(depthCapture);
            saveSettings();
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("Apply effects only to 3D world, preserving UI/HUD.");
            ImGui::Spacing();
            ImGui::TextDisabled("Captures the game's depth buffer and uses it to");
            ImGui::TextDisabled("skip effect processing on UI elements (depth = 1.0).");
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "May not work with all games.");
            ImGui::EndTooltip();
        }

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
