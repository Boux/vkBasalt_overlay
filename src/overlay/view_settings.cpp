#include "imgui_overlay.hpp"
#include "config_serializer.hpp"

#include <cstring>

#include "imgui/imgui.h"

namespace vkBasalt
{
    void ImGuiOverlay::renderSettingsView(const KeyboardState& keyboard)
    {
        // Settings mode
        ImGui::Text("vkBasalt Settings");
        ImGui::Separator();

        ImGui::BeginChild("SettingsContent", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), false);

        // Paths section
        ImGui::Text("Paths");
        ImGui::Separator();

        ImGui::Text("ReShade Textures (requires restart):");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Directory containing ReShade texture files (.png, .jpg, etc.)\nChanges require restarting the application.");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##texturePath", settingsTexturePath, sizeof(settingsTexturePath));

        ImGui::Text("ReShade Shaders (requires restart):");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Directory containing ReShade shader files (.fx, .fxh)\nChanges require restarting the application.");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##includePath", settingsIncludePath, sizeof(settingsIncludePath));

        ImGui::Spacing();
        ImGui::Text("Overlay Options");
        ImGui::Separator();

        ImGui::Checkbox("Block Input When Overlay Open", &settingsBlockInput);
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
            ImGui::SetTooltip("Maximum number of effects that can be active simultaneously.\nChanges require restarting the application.");
        ImGui::SetNextItemWidth(100);
        ImGui::InputInt("##maxEffects", &settingsMaxEffects);
        if (settingsMaxEffects < 1) settingsMaxEffects = 1;
        if (settingsMaxEffects > 50) settingsMaxEffects = 50;

        ImGui::Spacing();
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
            {
                listeningForKey = isListening ? 0 : bindingId;  // Toggle listening
            }

            if (isListening)
                ImGui::PopStyleColor();

            // Capture key if listening
            if (isListening && !keyboard.lastKeyName.empty())
            {
                strncpy(keyBuffer, keyboard.lastKeyName.c_str(), bufSize - 1);
                keyBuffer[bufSize - 1] = '\0';
                listeningForKey = 0;
            }
        };

        renderKeyBind("Toggle Effects:", "Key to enable/disable all effects",
                      settingsToggleKey, sizeof(settingsToggleKey), 1);
        renderKeyBind("Reload Config:", "Key to reload the configuration file",
                      settingsReloadKey, sizeof(settingsReloadKey), 2);
        renderKeyBind("Toggle Overlay:", "Key to show/hide this overlay",
                      settingsOverlayKey, sizeof(settingsOverlayKey), 3);

        ImGui::Spacing();
        ImGui::Text("Startup Behavior");
        ImGui::Separator();

        ImGui::Checkbox("Enable Effects on Launch", &settingsEnableOnLaunch);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("If enabled, effects are active when the game starts.\nIf disabled, effects start off and must be toggled on.");

        ImGui::Checkbox("Depth Capture (requires restart)", &settingsDepthCapture);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Enable depth buffer capture for effects that use depth.\nMay impact performance. Most effects don't need this.\nChanges require restarting the application.");

        ImGui::EndChild();

        // Footer buttons
        if (ImGui::Button("Save"))
        {
            VkBasaltSettings newSettings;
            newSettings.reshadeTexturePath = settingsTexturePath;
            newSettings.reshadeIncludePath = settingsIncludePath;
            newSettings.maxEffects = settingsMaxEffects;
            newSettings.overlayBlockInput = settingsBlockInput;
            newSettings.toggleKey = settingsToggleKey;
            newSettings.reloadKey = settingsReloadKey;
            newSettings.overlayKey = settingsOverlayKey;
            newSettings.enableOnLaunch = settingsEnableOnLaunch;
            newSettings.depthCapture = settingsDepthCapture;
            ConfigSerializer::saveSettings(newSettings);
            settingsSaved = true;  // Signal basalt.cpp to reload keybindings
            inSettingsMode = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            // Reload settings to discard changes
            settingsInitialized = false;
            inSettingsMode = false;
        }
    }

} // namespace vkBasalt
