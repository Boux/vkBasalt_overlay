#include "imgui_overlay.hpp"
#include "effect_registry.hpp"

#include <algorithm>
#include <cstring>
#include <cctype>

#include "imgui/imgui.h"

namespace vkBasalt
{
    // Case-insensitive substring match
    static bool matchesSearch(const std::string& text, const char* search)
    {
        if (!search || !search[0])
            return true;
        std::string lowerText = text;
        std::string lowerSearch = search;
        std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(), ::tolower);
        std::transform(lowerSearch.begin(), lowerSearch.end(), lowerSearch.begin(), ::tolower);
        return lowerText.find(lowerSearch) != std::string::npos;
    }

    void ImGuiOverlay::renderAddEffectsView()
    {
        if (!pEffectRegistry)
            return;

        // Get a mutable copy of selected effects
        std::vector<std::string> selectedEffects = pEffectRegistry->getSelectedEffects();

        // Handle ESC to clear search
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) && addEffectsSearch[0] != '\0')
        {
            addEffectsSearch[0] = '\0';
        }

        // Capture keyboard input for seamless search (only when no widget is active)
        if (!ImGui::IsAnyItemActive())
        {
            ImGuiIO& io = ImGui::GetIO();
            for (int i = 0; i < io.InputQueueCharacters.Size; i++)
            {
                ImWchar c = io.InputQueueCharacters[i];
                if (c >= 32 && c < 127)  // Printable ASCII
                {
                    size_t len = strlen(addEffectsSearch);
                    if (len < sizeof(addEffectsSearch) - 1)
                    {
                        addEffectsSearch[len] = static_cast<char>(c);
                        addEffectsSearch[len + 1] = '\0';
                    }
                }
            }
            // Handle backspace
            if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && addEffectsSearch[0] != '\0')
            {
                size_t len = strlen(addEffectsSearch);
                if (len > 0)
                    addEffectsSearch[len - 1] = '\0';
            }
        }

        // Add Effects mode - two column layout
        if (insertPosition >= 0)
            ImGui::Text("Insert Effects at position %d (max %zu)", insertPosition, maxEffects);
        else
            ImGui::Text("Add Effects (max %zu)", maxEffects);
        ImGui::Separator();

        size_t currentCount = selectedEffects.size();
        size_t pendingCount = pendingAddEffects.size();
        size_t totalCount = currentCount + pendingCount;

        // Built-in effects
        std::vector<std::string> builtinEffects = {"cas", "dls", "fxaa", "smaa", "deband", "lut"};

        // Helper to check if instance name is used
        auto isNameUsed = [&](const std::string& name) {
            if (std::find(selectedEffects.begin(), selectedEffects.end(), name) != selectedEffects.end())
                return true;
            for (const auto& p : pendingAddEffects)
                if (p.first == name)
                    return true;
            return false;
        };

        // Helper to get next instance name for an effect type
        auto getNextInstanceName = [&](const std::string& effectType) -> std::string {
            if (!isNameUsed(effectType))
                return effectType;
            for (int n = 2; n <= 99; n++)
            {
                std::string candidate = effectType + "." + std::to_string(n);
                if (!isNameUsed(candidate))
                    return candidate;
            }
            return effectType + ".99";
        };

        // Helper to render add button for an effect
        auto renderAddButton = [&](const std::string& effectType, const std::string& tooltip = "") {
            bool atLimit = totalCount >= maxEffects;
            if (atLimit)
                ImGui::BeginDisabled();

            if (ImGui::Button(effectType.c_str(), ImVec2(-1, 0)))
            {
                std::string instanceName = getNextInstanceName(effectType);
                pendingAddEffects.push_back({instanceName, effectType});
            }

            // Show tooltip with shader path on hover
            if (!tooltip.empty() && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", tooltip.c_str());

            if (atLimit)
                ImGui::EndDisabled();
        };

        // Two column layout
        float footerHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
        float contentHeight = -footerHeight;
        float columnWidth = ImGui::GetContentRegionAvail().x * 0.5f - ImGui::GetStyle().ItemSpacing.x * 0.5f;

        // Left column: Available effects
        ImGui::BeginChild("EffectList", ImVec2(columnWidth, contentHeight), true);

        bool hasSearch = addEffectsSearch[0] != '\0';

        // Show search bar only when searching
        if (hasSearch)
        {
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.2f, 0.3f, 1.0f));
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##search", addEffectsSearch, sizeof(addEffectsSearch), ImGuiInputTextFlags_AutoSelectAll);
            ImGui::PopStyleColor();
            ImGui::TextDisabled("ESC to clear");
            ImGui::Separator();
        }
        else
        {
            ImGui::Text("Available:");
            ImGui::TextDisabled("(type to search)");
            ImGui::Separator();
        }

        // Sort effects for each category
        std::vector<std::string> sortedCurrentConfig = state.currentConfigEffects;
        std::vector<std::string> sortedDefaultConfig = state.defaultConfigEffects;
        std::sort(sortedCurrentConfig.begin(), sortedCurrentConfig.end());
        std::sort(sortedDefaultConfig.begin(), sortedDefaultConfig.end());

        // Built-in effects (filtered)
        bool hasBuiltinMatches = false;
        for (const auto& effectType : builtinEffects)
        {
            if (matchesSearch(effectType, addEffectsSearch))
            {
                hasBuiltinMatches = true;
                break;
            }
        }
        if (hasBuiltinMatches)
        {
            if (!hasSearch)
                ImGui::Text("Built-in:");
            for (const auto& effectType : builtinEffects)
            {
                if (matchesSearch(effectType, addEffectsSearch))
                    renderAddButton(effectType);
            }
        }

        // ReShade effects from current config (filtered)
        bool hasCurrentMatches = false;
        for (const auto& effectType : sortedCurrentConfig)
        {
            if (matchesSearch(effectType, addEffectsSearch))
            {
                hasCurrentMatches = true;
                break;
            }
        }
        if (hasCurrentMatches)
        {
            if (hasBuiltinMatches || !hasSearch)
                ImGui::Separator();
            if (!hasSearch)
                ImGui::Text("ReShade (%s):", state.configName.c_str());
            for (const auto& effectType : sortedCurrentConfig)
            {
                if (!matchesSearch(effectType, addEffectsSearch))
                    continue;
                auto it = state.effectPaths.find(effectType);
                std::string path = (it != state.effectPaths.end()) ? it->second : "";
                renderAddButton(effectType, path);
            }
        }

        // ReShade effects from default config (filtered)
        bool hasDefaultMatches = false;
        for (const auto& effectType : sortedDefaultConfig)
        {
            if (matchesSearch(effectType, addEffectsSearch))
            {
                hasDefaultMatches = true;
                break;
            }
        }
        if (hasDefaultMatches)
        {
            if (hasCurrentMatches || hasBuiltinMatches || !hasSearch)
                ImGui::Separator();
            if (!hasSearch)
                ImGui::Text("ReShade (all):");
            for (const auto& effectType : sortedDefaultConfig)
            {
                if (!matchesSearch(effectType, addEffectsSearch))
                    continue;
                auto it = state.effectPaths.find(effectType);
                std::string path = (it != state.effectPaths.end()) ? it->second : "";
                renderAddButton(effectType, path);
            }
        }

        // Show "no results" if searching and nothing matches
        if (hasSearch && !hasBuiltinMatches && !hasCurrentMatches && !hasDefaultMatches)
            ImGui::TextDisabled("No effects match '%s'", addEffectsSearch);

        ImGui::EndChild();

        ImGui::SameLine();

        // Right column: Pending effects
        ImGui::BeginChild("PendingList", ImVec2(columnWidth, contentHeight), true);
        ImGui::Text("Will add (%zu):", pendingCount);
        ImGui::Separator();

        for (size_t i = 0; i < pendingAddEffects.size(); i++)
        {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::SmallButton("x"))
            {
                pendingAddEffects.erase(pendingAddEffects.begin() + i);
                ImGui::PopID();
                continue;
            }
            ImGui::SameLine();
            // Show instanceName (effectType) if they differ
            const auto& [instanceName, effectType] = pendingAddEffects[i];
            if (instanceName != effectType)
                ImGui::Text("%s (%s)", instanceName.c_str(), effectType.c_str());
            else
                ImGui::Text("%s", instanceName.c_str());
            ImGui::PopID();
        }

        if (pendingAddEffects.empty())
            ImGui::TextDisabled("Click effects to add...");

        ImGui::EndChild();

        ImGui::Separator();

        if (ImGui::Button("Done"))
        {
            // Apply pending effects - insert at position or append
            int pos = (insertPosition >= 0 && insertPosition <= static_cast<int>(selectedEffects.size()))
                      ? insertPosition : static_cast<int>(selectedEffects.size());
            for (const auto& [instanceName, effectType] : pendingAddEffects)
            {
                selectedEffects.insert(selectedEffects.begin() + pos, instanceName);
                pos++;  // Insert subsequent effects after the previous one
                pEffectRegistry->ensureEffect(instanceName, effectType);
                pEffectRegistry->setEffectEnabled(instanceName, true);
            }
            if (!pendingAddEffects.empty())
            {
                pEffectRegistry->setSelectedEffects(selectedEffects);
                applyRequested = true;
            }
            pendingAddEffects.clear();
            insertPosition = -1;
            inSelectionMode = false;
            addEffectsSearch[0] = '\0';
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            pendingAddEffects.clear();
            insertPosition = -1;
            inSelectionMode = false;
            addEffectsSearch[0] = '\0';
        }
    }

} // namespace vkBasalt
