#include "imgui_overlay.hpp"
#include "config_serializer.hpp"
#include "logger.hpp"

#include <cstring>

#include "imgui/imgui.h"

namespace vkBasalt
{
    void ImGuiOverlay::renderSettingsView(const KeyboardState& keyboard)
    {
        // Helper to save settings (auto-save on any change)
        auto saveSettings = [&]() {
            VkBasaltSettings newSettings;
            newSettings.maxEffects = settingsMaxEffects;
            newSettings.overlayBlockInput = settingsBlockInput;
            newSettings.toggleKey = settingsToggleKey;
            newSettings.reloadKey = settingsReloadKey;
            newSettings.overlayKey = settingsOverlayKey;
            newSettings.enableOnLaunch = settingsEnableOnLaunch;
            newSettings.depthCapture = settingsDepthCapture;
            newSettings.autoApplyDelay = settingsAutoApplyDelay;
            newSettings.showDebugWindow = settingsShowDebugWindow;
            ConfigSerializer::saveSettings(newSettings);
            settingsSaved = true;
        };

        ImGui::BeginChild("SettingsContent", ImVec2(0, 0), false);

        ImGui::Text("Key Bindings");
        ImGui::Separator();
        ImGui::TextDisabled("Click a button and press any key to set binding");

        // Helper lambda to render a keybind button
        auto renderKeyBind = [&](const char* label, const char* tooltip, char* keyBuffer, size_t bufSize, int bindingId) {
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
                listeningForKey = 0;
                saveSettings();
            }
        };

        renderKeyBind("Toggle Effects:", "Key to enable/disable all effects",
                      settingsToggleKey, sizeof(settingsToggleKey), 1);
        renderKeyBind("Reload Config:", "Key to reload the configuration file",
                      settingsReloadKey, sizeof(settingsReloadKey), 2);
        renderKeyBind("Toggle Overlay:", "Key to show/hide this overlay",
                      settingsOverlayKey, sizeof(settingsOverlayKey), 3);

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
        if (ImGui::InputInt("##maxEffects", &settingsMaxEffects))
        {
            if (settingsMaxEffects < 1) settingsMaxEffects = 1;
            if (settingsMaxEffects > 200) settingsMaxEffects = 200;
            maxEffects = static_cast<size_t>(settingsMaxEffects);
            saveSettings();
        }
        if (settingsMaxEffects < 1) settingsMaxEffects = 1;
        if (settingsMaxEffects > 200) settingsMaxEffects = 200;

        // Show VRAM estimate based on current resolution (2 images per slot, 4 bytes per pixel)
        float bytesPerSlot = 2.0f * currentWidth * currentHeight * 4.0f;
        int estimatedVramMB = static_cast<int>((settingsMaxEffects * bytesPerSlot) / (1024.0f * 1024.0f));
        ImGui::SameLine();
        if (settingsMaxEffects > 20)
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "~%d MB @ %ux%u", estimatedVramMB, currentWidth, currentHeight);
        else
            ImGui::TextDisabled("~%d MB @ %ux%u", estimatedVramMB, currentWidth, currentHeight);

        ImGui::Text("Auto-apply Delay:");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Delay before automatically applying parameter changes.\nLower values feel more responsive, higher values reduce stutter.");
        ImGui::SetNextItemWidth(150);
        ImGui::SliderInt("##autoApplyDelay", &settingsAutoApplyDelay, 20, 1000, "%d ms");
        if (ImGui::IsItemDeactivatedAfterEdit())
            saveSettings();

        ImGui::Spacing();
        ImGui::Text("Startup Behavior");
        ImGui::Separator();

        if (ImGui::Checkbox("Enable Effects on Launch", &settingsEnableOnLaunch))
            saveSettings();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("If enabled, effects are active when the game starts.\nIf disabled, effects start off and must be toggled on.");

        ImGui::Spacing();
        ImGui::Text("Advanced Options");
        ImGui::Separator();

        if (ImGui::Checkbox("Block Input When Overlay Open", &settingsBlockInput))
            saveSettings();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("When enabled, keyboard and mouse input is blocked\nfrom reaching the game while the overlay is open.");

        if (ImGui::Checkbox("Depth Masking (experimental, requires restart)", &settingsDepthCapture))
            saveSettings();
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("Apply effects only to 3D world, preserving UI/HUD.");
            ImGui::Spacing();
            ImGui::TextDisabled("Captures the game's depth buffer and uses it to");
            ImGui::TextDisabled("skip effect processing on UI elements (depth = 1.0).");
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "May not work with all games.");
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "Changes require restarting the application.");
            ImGui::EndTooltip();
        }

        if (ImGui::Checkbox("Show Debug Window", &settingsShowDebugWindow))
        {
            Logger::setHistoryEnabled(settingsShowDebugWindow);
            saveSettings();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Show debug window with effect registry data and log output.");

        ImGui::EndChild();
    }

} // namespace vkBasalt
