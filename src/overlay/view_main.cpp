#include "imgui_overlay.hpp"
#include "effect_registry.hpp"
#include "config_serializer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

namespace vkBasalt
{
    namespace
    {
        // Render a single preprocessor definition input, returns true if value changed
        void renderPreprocessorDef(PreprocessorDefinition& def, EffectRegistry* registry, const std::string& effectName)
        {
            char valueBuf[64];
            strncpy(valueBuf, def.value.c_str(), sizeof(valueBuf) - 1);
            valueBuf[sizeof(valueBuf) - 1] = '\0';

            ImGui::SetNextItemWidth(80);
            if (ImGui::InputText(def.name.c_str(), valueBuf, sizeof(valueBuf)))
                registry->setPreprocessorDefValue(effectName, def.name, valueBuf);

            if (def.value != def.defaultValue)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("(modified)");
            }

            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                registry->setPreprocessorDefValue(effectName, def.name, def.defaultValue);

            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Default: %s\nDouble-click to reset", def.defaultValue.c_str());
        }

        // Render a single parameter widget, returns true if value changed
        bool renderParameter(EffectParameter& param)
        {
            bool changed = false;

            switch (param.type)
            {
            case ParamType::Float:
                if (ImGui::SliderFloat(param.label.c_str(), &param.valueFloat, param.minFloat, param.maxFloat))
                {
                    if (param.step > 0.0f)
                        param.valueFloat = std::round(param.valueFloat / param.step) * param.step;
                    changed = true;
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                {
                    param.valueFloat = param.defaultFloat;
                    changed = true;
                    ImGui::ClearActiveID();
                }
                break;

            case ParamType::Int:
                if (!param.items.empty())
                {
                    std::string itemsStr;
                    for (const auto& item : param.items)
                        itemsStr += item + '\0';
                    itemsStr += '\0';
                    if (ImGui::Combo(param.label.c_str(), &param.valueInt, itemsStr.c_str()))
                        changed = true;
                }
                else
                {
                    if (ImGui::SliderInt(param.label.c_str(), &param.valueInt, param.minInt, param.maxInt))
                    {
                        if (param.step > 0.0f)
                        {
                            int step = static_cast<int>(param.step);
                            if (step > 0)
                                param.valueInt = (param.valueInt / step) * step;
                        }
                        changed = true;
                    }
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                {
                    param.valueInt = param.defaultInt;
                    changed = true;
                    ImGui::ClearActiveID();
                }
                break;

            case ParamType::Bool:
                if (ImGui::Checkbox(param.label.c_str(), &param.valueBool))
                    changed = true;
                break;
            }

            if (!param.tooltip.empty() && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", param.tooltip.c_str());

            return changed;
        }
    } // anonymous namespace

    void ImGuiOverlay::renderMainView(const KeyboardState& keyboard)
    {
        if (!pEffectRegistry)
            return;

        // Get a mutable copy of selected effects for this frame
        std::vector<std::string> selectedEffects = pEffectRegistry->getSelectedEffects();

        // Normal mode - show config and effect controls

        // Config section with title
        ImGui::Text("Config:");
        ImGui::SameLine();

        // Initialize config name once - only pre-fill for user configs from configs folder
        static bool nameInitialized = false;
        bool isUserConfig = state.configPath.find("/configs/") != std::string::npos;
        if (!nameInitialized && isUserConfig && !state.configName.empty())
        {
            std::string name = state.configName;
            if (name.ends_with(".conf"))
                name = name.substr(0, name.size() - 5);
            strncpy(saveConfigName, name.c_str(), sizeof(saveConfigName) - 1);
        }
        nameInitialized = true;

        ImGui::SetNextItemWidth(120);
        ImGui::InputText("##configname", saveConfigName, sizeof(saveConfigName));

        ImGui::SameLine();
        ImGui::BeginDisabled(saveConfigName[0] == '\0');
        if (ImGui::Button("Save"))
            saveCurrentConfig();
        ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("..."))
            inConfigManageMode = true;
        ImGui::Separator();

        // Initialize settings if not done yet (needed for key display)
        if (!settingsInitialized)
        {
            VkBasaltSettings currentSettings = ConfigSerializer::loadSettings();
            settingsMaxEffects = currentSettings.maxEffects;
            settingsBlockInput = currentSettings.overlayBlockInput;
            strncpy(settingsToggleKey, currentSettings.toggleKey.c_str(), sizeof(settingsToggleKey) - 1);
            strncpy(settingsReloadKey, currentSettings.reloadKey.c_str(), sizeof(settingsReloadKey) - 1);
            strncpy(settingsOverlayKey, currentSettings.overlayKey.c_str(), sizeof(settingsOverlayKey) - 1);
            settingsEnableOnLaunch = currentSettings.enableOnLaunch;
            settingsDepthCapture = currentSettings.depthCapture;
            settingsInitialized = true;
        }

        bool effectsOn = state.effectsEnabled;
        if (ImGui::Checkbox(effectsOn ? "Effects ON" : "Effects OFF", &effectsOn))
            toggleEffectsRequested = true;
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", settingsToggleKey);
        ImGui::Separator();

        // Add Effects button
        if (ImGui::Button("Add Effects..."))
        {
            inSelectionMode = true;
            insertPosition = -1;  // Append to end
            pendingAddEffects.clear();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(selectedEffects.empty());
        if (ImGui::Button("Clear All"))
        {
            selectedEffects.clear();
            pEffectRegistry->clearSelectedEffects();
            paramsDirty = true;
            lastChangeTime = std::chrono::steady_clock::now();
        }
        ImGui::EndDisabled();
        ImGui::Separator();

        // Scrollable effect list (reserve space for footer controls)
        float footerHeight = ImGui::GetFrameHeightWithSpacing() * 2 + ImGui::GetStyle().ItemSpacing.y;
        ImGui::BeginChild("EffectList", ImVec2(0, -footerHeight), false);

        // Show selected effects with their parameters
        bool changedThisFrame = false;
        float itemHeight = ImGui::GetFrameHeightWithSpacing();

        // Reset drag target each frame
        dragTargetIndex = -1;

        for (size_t i = 0; i < selectedEffects.size(); i++)
        {
            const std::string& effectName = selectedEffects[i];
            ImGui::PushID(static_cast<int>(i));

            // Highlight drop target
            bool isDropTarget = isDragging && dragSourceIndex != static_cast<int>(i);
            if (isDropTarget)
            {
                ImVec2 rowMin = ImGui::GetCursorScreenPos();
                ImVec2 rowMax = ImVec2(rowMin.x + ImGui::GetContentRegionAvail().x, rowMin.y + itemHeight);
                if (ImGui::IsMouseHoveringRect(rowMin, rowMax))
                {
                    dragTargetIndex = static_cast<int>(i);
                    ImGui::GetWindowDrawList()->AddRectFilled(rowMin, rowMax, IM_COL32(100, 100, 255, 50));
                }
            }

            // Check if effect failed to compile
            bool effectFailed = pEffectRegistry ? pEffectRegistry->hasEffectFailed(effectName) : false;
            std::string effectError = effectFailed && pEffectRegistry ? pEffectRegistry->getEffectError(effectName) : "";

            // Checkbox to enable/disable effect (read/write via registry)
            // Disabled for failed effects
            if (effectFailed)
                ImGui::BeginDisabled();

            bool effectEnabled = pEffectRegistry ? pEffectRegistry->isEffectEnabled(effectName) : true;
            if (ImGui::Checkbox("##enabled", &effectEnabled))
            {
                if (pEffectRegistry)
                    pEffectRegistry->setEffectEnabled(effectName, effectEnabled);
                changedThisFrame = true;
                paramsDirty = true;
                lastChangeTime = std::chrono::steady_clock::now();
            }

            if (effectFailed)
                ImGui::EndDisabled();

            ImGui::SameLine();

            // Show failed effects in red
            if (effectFailed)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));

            bool treeOpen = ImGui::TreeNode("effect", "%s%s", effectName.c_str(), effectFailed ? " (FAILED)" : "");

            if (effectFailed)
                ImGui::PopStyleColor();

            // Drag from tree node header for reordering
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0))
            {
                if (!isDragging)
                {
                    isDragging = true;
                    dragSourceIndex = static_cast<int>(i);
                }
            }

            // Right-click context menu
            if (ImGui::BeginPopupContextItem("effect_context"))
            {
                // Toggle ON/OFF
                if (ImGui::MenuItem(effectEnabled ? "Disable" : "Enable"))
                {
                    if (pEffectRegistry)
                        pEffectRegistry->setEffectEnabled(effectName, !effectEnabled);
                    changedThisFrame = true;
                    paramsDirty = true;
                    lastChangeTime = std::chrono::steady_clock::now();
                }

                // Reset to defaults
                if (ImGui::MenuItem("Reset to Defaults"))
                {
                    for (auto& param : editableParams)
                    {
                        if (param.effectName != effectName)
                            continue;
                        switch (param.type)
                        {
                        case ParamType::Float:
                            param.valueFloat = param.defaultFloat;
                            break;
                        case ParamType::Int:
                            param.valueInt = param.defaultInt;
                            break;
                        case ParamType::Bool:
                            param.valueBool = param.defaultBool;
                            break;
                        }
                    }
                    changedThisFrame = true;
                    paramsDirty = true;
                    lastChangeTime = std::chrono::steady_clock::now();
                }

                ImGui::Separator();

                // Insert effects here
                if (ImGui::MenuItem("Insert effects here..."))
                {
                    insertPosition = static_cast<int>(i);
                    inSelectionMode = true;
                    pendingAddEffects.clear();
                }

                // Remove effect
                if (ImGui::MenuItem("Remove"))
                {
                    selectedEffects.erase(selectedEffects.begin() + i);
                    pEffectRegistry->setSelectedEffects(selectedEffects);
                    changedThisFrame = true;
                    paramsDirty = true;
                    lastChangeTime = std::chrono::steady_clock::now();
                }

                ImGui::EndPopup();
            }

            ImGui::PopID();

            if (!treeOpen)
                continue;

            // Show error for failed effects
            if (effectFailed)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                ImGui::TextWrapped("Error: %s", effectError.c_str());
                ImGui::PopStyleColor();
                ImGui::TreePop();
                continue;
            }

            // Show preprocessor definitions first (ReShade effects only)
            if (pEffectRegistry)
            {
                auto& defs = pEffectRegistry->getPreprocessorDefs(effectName);
                if (!defs.empty() && ImGui::TreeNode("preprocessor", "Preprocessor (%zu)", defs.size()))
                {
                    ImGui::TextDisabled("Click Apply or press %s to recompile", settingsReloadKey);

                    for (size_t defIdx = 0; defIdx < defs.size(); defIdx++)
                    {
                        ImGui::PushID(static_cast<int>(defIdx + 1000));
                        renderPreprocessorDef(defs[defIdx], pEffectRegistry, effectName);
                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }
            }

            // Show parameters for this effect
            int paramIndex = 0;
            for (auto& param : editableParams)
            {
                if (param.effectName != effectName)
                    continue;

                ImGui::PushID(paramIndex++);
                if (renderParameter(param))
                {
                    paramsDirty = true;
                    changedThisFrame = true;
                    lastChangeTime = std::chrono::steady_clock::now();
                }
                ImGui::PopID();
            }

            ImGui::TreePop();
        }

        // Handle drag end and reorder
        if (isDragging)
        {
            // Show floating tooltip with dragged effect name
            ImGui::SetTooltip("Moving: %s", selectedEffects[dragSourceIndex].c_str());

            // Check if mouse released
            if (!ImGui::IsMouseDown(0))
            {
                if (dragTargetIndex >= 0 && dragTargetIndex != dragSourceIndex)
                {
                    // Move the effect from source to target
                    std::string moving = selectedEffects[dragSourceIndex];
                    selectedEffects.erase(selectedEffects.begin() + dragSourceIndex);
                    selectedEffects.insert(selectedEffects.begin() + dragTargetIndex, moving);
                    pEffectRegistry->setSelectedEffects(selectedEffects);
                    changedThisFrame = true;
                    paramsDirty = true;
                    lastChangeTime = std::chrono::steady_clock::now();
                }
                isDragging = false;
                dragSourceIndex = -1;
                dragTargetIndex = -1;
            }
        }

        ImGui::EndChild();

        ImGui::Separator();
        bool prevAutoApply = autoApply;
        ImGui::Checkbox("Apply automatically", &autoApply);
        if (autoApply != prevAutoApply)
            saveToPersistentState();
        float applyWidth = ImGui::CalcTextSize("Apply").x + ImGui::GetStyle().FramePadding.x * 2;
        ImGui::SameLine(ImGui::GetWindowWidth() - applyWidth - ImGui::GetStyle().WindowPadding.x);

        // Apply button is always clickable
        if (ImGui::Button("Apply"))
        {
            applyRequested = true;
            paramsDirty = false;
            saveToPersistentState();
        }

        // Auto-apply with debounce (200ms after last change)
        if (autoApply && paramsDirty && !changedThisFrame)
        {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastChangeTime).count();
            if (elapsed >= 200)
            {
                applyRequested = true;
                paramsDirty = false;
                saveToPersistentState();
            }
        }

        // Save state when effects/params change
        if (changedThisFrame)
            saveToPersistentState();
    }

} // namespace vkBasalt
