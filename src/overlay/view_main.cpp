#include "imgui_overlay.hpp"
#include "effects/effect_registry.hpp"
#include "settings_manager.hpp"
#include "config_serializer.hpp"
#include "params/field_editor.hpp"
#include "logger.hpp"

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

        bool effectsOn = state.effectsEnabled;
        if (ImGui::Checkbox(effectsOn ? "Effects ON" : "Effects OFF", &effectsOn))
            toggleEffectsRequested = true;
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", settingsManager.getToggleKey().c_str());
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
                    {
                        pEffectRegistry->setEffectEnabled(effectName, !effectEnabled);
                        paramsDirty = true;
                        lastChangeTime = std::chrono::steady_clock::now();
                    }
                }

                // Reset to defaults
                if (ImGui::MenuItem("Reset to Defaults"))
                {
                    for (auto* param : pEffectRegistry->getParametersForEffect(effectName))
                    {
                        FieldEditor* editor = FieldEditorFactory::instance().getEditor(param->getType());
                        if (editor)
                            editor->resetToDefault(*param);
                    }
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
                if (!defs.empty())
                {
                    // Draw background rect behind preprocessor section using channels
                    ImVec2 startPos = ImGui::GetCursorScreenPos();
                    float contentWidth = ImGui::GetContentRegionAvail().x;
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    drawList->ChannelsSplit(2);
                    drawList->ChannelsSetCurrent(1);  // Foreground for content

                    if (ImGui::TreeNode("preprocessor", "Preprocessor (%zu)", defs.size()))
                    {
                        ImGui::TextDisabled("Click Apply or press %s to recompile", settingsManager.getReloadKey().c_str());

                        for (size_t defIdx = 0; defIdx < defs.size(); defIdx++)
                        {
                            ImGui::PushID(static_cast<int>(defIdx + 1000));
                            renderPreprocessorDef(defs[defIdx], pEffectRegistry, effectName);
                            ImGui::PopID();
                        }
                        ImGui::TreePop();
                    }

                    // Draw background rect on channel 0 (behind content)
                    ImVec2 endPos = ImGui::GetCursorScreenPos();
                    drawList->ChannelsSetCurrent(0);  // Background
                    drawList->AddRectFilled(
                        startPos,
                        ImVec2(startPos.x + contentWidth, endPos.y),
                        IM_COL32(0, 0, 0, 128),  // 50% opacity black
                        0.0f);
                    drawList->ChannelsMerge();
                }
            }

            // Show parameters for this effect
            auto effectParams = pEffectRegistry->getParametersForEffect(effectName);
            for (size_t paramIdx = 0; paramIdx < effectParams.size(); paramIdx++)
            {
                ImGui::PushID(static_cast<int>(paramIdx));
                if (renderFieldEditor(*effectParams[paramIdx]))
                {
                    paramsDirty = true;
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
        bool autoApplyVal = settingsManager.getAutoApply();
        if (ImGui::Checkbox("Apply automatically", &autoApplyVal))
        {
            settingsManager.setAutoApply(autoApplyVal);
            settingsManager.save();
        }
        float applyWidth = ImGui::CalcTextSize("Apply").x + ImGui::GetStyle().FramePadding.x * 2;
        ImGui::SameLine(ImGui::GetWindowWidth() - applyWidth - ImGui::GetStyle().WindowPadding.x);

        // Apply button is always clickable
        if (ImGui::Button("Apply"))
        {
            applyRequested = true;
            paramsDirty = false;
        }
        // Note: Auto-apply is handled globally in imgui_overlay.cpp
    }

} // namespace vkBasalt
