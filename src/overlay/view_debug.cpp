#include "imgui_overlay.hpp"
#include "effects/effect_registry.hpp"
#include "settings_manager.hpp"
#include "logger.hpp"

#include <cctype>
#include <cstring>

#include "imgui/imgui.h"

namespace vkBasalt
{
    void ImGuiOverlay::renderDebugWindow()
    {
        if (!settingsManager.getShowDebugWindow())
            return;

        // Local bool for ImGui window close button
        bool showDebugWindow = true;
        ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Debug Window", &showDebugWindow))
        {
            ImGui::End();
            // User closed the window
            if (!showDebugWindow)
            {
                settingsManager.setShowDebugWindow(false);
                settingsManager.save();
            }
            return;
        }

        // Check if user closed via X button
        if (!showDebugWindow)
        {
            settingsManager.setShowDebugWindow(false);
            settingsManager.save();
        }

        // Tab bar for different debug views
        if (ImGui::BeginTabBar("DebugTabs"))
        {
            // Effects tab
            if (ImGui::BeginTabItem("Effects"))
            {
                debugWindowTab = 0;

                if (!pEffectRegistry)
                {
                    ImGui::TextDisabled("Effect registry not available");
                    ImGui::EndTabItem();
                    ImGui::EndTabBar();
                    ImGui::End();
                    return;
                }

                const auto& effects = pEffectRegistry->getAllEffects();
                ImGui::Text("Total Effects: %zu", effects.size());
                ImGui::Separator();

                for (const auto& effect : effects)
                {
                    // Effect header
                    bool open = ImGui::TreeNode(effect.name.c_str(), "[%s] %s",
                        effect.type == EffectType::BuiltIn ? "BuiltIn" : "ReShade",
                        effect.name.c_str());

                    if (open)
                    {
                        ImGui::TextDisabled("Type: %s", effect.effectType.c_str());
                        ImGui::TextDisabled("Enabled: %s", effect.enabled ? "true" : "false");

                        if (!effect.filePath.empty())
                            ImGui::TextDisabled("Path: %s", effect.filePath.c_str());

                        if (effect.hasFailed())
                        {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                            ImGui::TextWrapped("Error: %s", effect.compileError.c_str());
                            ImGui::PopStyleColor();
                        }

                        // Parameters
                        if (!effect.parameters.empty())
                        {
                            if (ImGui::TreeNode("Parameters", "Parameters (%zu)", effect.parameters.size()))
                            {
                                for (const auto& param : effect.parameters)
                                {
                                    const char* typeName = param->getTypeName();

                                    // Format serialized value(s)
                                    auto serialized = param->serialize();
                                    std::string valueStr;
                                    for (size_t i = 0; i < serialized.size(); i++)
                                    {
                                        if (i > 0) valueStr += ", ";
                                        if (!serialized[i].first.empty())
                                            valueStr += serialized[i].first + "=";
                                        valueStr += serialized[i].second;
                                    }

                                    ImGui::BulletText("[%s] %s = %s",
                                        typeName,
                                        param->name.c_str(),
                                        valueStr.c_str());
                                }
                                ImGui::TreePop();
                            }
                        }

                        // Preprocessor definitions
                        if (!effect.preprocessorDefs.empty())
                        {
                            if (ImGui::TreeNode("Preprocessor", "Preprocessor Defs (%zu)", effect.preprocessorDefs.size()))
                            {
                                for (const auto& def : effect.preprocessorDefs)
                                {
                                    ImGui::BulletText("%s = %s (default: %s)",
                                        def.name.c_str(),
                                        def.value.c_str(),
                                        def.defaultValue.c_str());
                                }
                                ImGui::TreePop();
                            }
                        }

                        ImGui::TreePop();
                    }
                }

                ImGui::EndTabItem();
            }

            // Depth tab (only visible when depth masking is enabled)
            if (settingsManager.getDepthCapture())
            {
                if (ImGui::BeginTabItem("Depth"))
                {
                    debugWindowTab = 2;

                    ImGui::Text("Depth Masking Status");
                    ImGui::Separator();

                    // Show current settings
                    bool depthEnabled = settingsManager.getDepthCapture();
                    float threshold = settingsManager.getDepthMaskThreshold();
                    bool reversed = settingsManager.getDepthMaskReversed();

                    ImGui::BulletText("Depth Masking: %s", depthEnabled ? "Enabled" : "Disabled");
                    ImGui::BulletText("Threshold: %.4f", threshold);
                    ImGui::BulletText("Reversed Depth (DXVK): %s", reversed ? "Yes" : "No");

                    ImGui::Spacing();
                    ImGui::Text("Depth Mode:");
                    ImGui::Separator();
                    if (reversed)
                    {
                        ImGui::TextDisabled("Using DXVK reversed depth:");
                        ImGui::TextDisabled("  Near plane = 1.0");
                        ImGui::TextDisabled("  Far plane = 0.0");
                        ImGui::TextDisabled("  UI detection: depth < %.4f", 1.0f - threshold);
                    }
                    else
                    {
                        ImGui::TextDisabled("Using standard Vulkan depth:");
                        ImGui::TextDisabled("  Near plane = 0.0");
                        ImGui::TextDisabled("  Far plane = 1.0");
                        ImGui::TextDisabled("  UI detection: depth > %.4f", threshold);
                    }

                    ImGui::Spacing();
                    ImGui::Text("Debug Visualization");
                    ImGui::Separator();
                    ImGui::TextWrapped("To visualize the depth buffer, set threshold below 0.5 in Settings.");
                    ImGui::Spacing();
                    ImGui::TextDisabled("When threshold < 0.5:");
                    ImGui::TextDisabled("  Black = near objects (close to camera)");
                    ImGui::TextDisabled("  White = far objects (background/UI)");

                    ImGui::Spacing();
                    ImGui::Text("Troubleshooting");
                    ImGui::Separator();
                    ImGui::TextWrapped("If effects apply to UI elements:");
                    ImGui::BulletText("Try enabling 'Reversed Depth' for DXVK/D3D games");
                    ImGui::BulletText("Lower the threshold (e.g., 0.999 or 0.99)");
                    ImGui::BulletText("Use debug visualization to see depth values");
                    ImGui::Spacing();
                    ImGui::TextWrapped("If effects never apply to 3D world:");
                    ImGui::BulletText("Increase threshold closer to 1.0");
                    ImGui::BulletText("Game may not expose depth buffer to vkBasalt");

                    ImGui::EndTabItem();
                }
            }

            // Log tab
            if (ImGui::BeginTabItem("Log"))
            {
                debugWindowTab = 1;

                // Handle ESC to clear search
                if (ImGui::IsKeyPressed(ImGuiKey_Escape) && debugLogSearch[0] != '\0')
                    debugLogSearch[0] = '\0';

                // Capture keyboard input for seamless search (only when no widget is active)
                if (!ImGui::IsAnyItemActive())
                {
                    ImGuiIO& io = ImGui::GetIO();
                    for (int i = 0; i < io.InputQueueCharacters.Size; i++)
                    {
                        ImWchar c = io.InputQueueCharacters[i];
                        if (c >= 32 && c < 127)  // Printable ASCII
                        {
                            size_t len = strlen(debugLogSearch);
                            if (len < sizeof(debugLogSearch) - 1)
                            {
                                debugLogSearch[len] = static_cast<char>(c);
                                debugLogSearch[len + 1] = '\0';
                            }
                        }
                    }
                    // Handle backspace
                    if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && debugLogSearch[0] != '\0')
                    {
                        size_t len = strlen(debugLogSearch);
                        if (len > 0)
                            debugLogSearch[len - 1] = '\0';
                    }
                }

                bool hasSearch = debugLogSearch[0] != '\0';

                // Show search bar only when searching
                if (hasSearch)
                {
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.2f, 0.3f, 1.0f));
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 30);
                    ImGui::InputText("##logsearch", debugLogSearch, sizeof(debugLogSearch), ImGuiInputTextFlags_AutoSelectAll);
                    ImGui::PopStyleColor();
                    ImGui::SameLine();
                    if (ImGui::Button("x"))
                        debugLogSearch[0] = '\0';
                    ImGui::Separator();
                }

                // Filter checkboxes
                ImGui::Text("Filters:");
                ImGui::SameLine();
                ImGui::Checkbox("Trace", &debugLogFilters[0]);
                ImGui::SameLine();
                ImGui::Checkbox("Debug", &debugLogFilters[1]);
                ImGui::SameLine();
                ImGui::Checkbox("Info", &debugLogFilters[2]);
                ImGui::SameLine();
                ImGui::Checkbox("Warn", &debugLogFilters[3]);
                ImGui::SameLine();
                ImGui::Checkbox("Error", &debugLogFilters[4]);
                ImGui::SameLine();
                if (ImGui::Button("Clear Log"))
                    Logger::clearHistory();

                if (!hasSearch)
                    ImGui::TextDisabled("Type to search...");

                ImGui::Separator();

                // Log output in scrolling region
                ImGui::BeginChild("LogScrollRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

                auto history = Logger::getHistory();

                // Color mapping for log levels
                static ImVec4 levelColors[5] = {
                    ImVec4(0.5f, 0.5f, 0.5f, 1.0f),  // Trace - gray
                    ImVec4(0.4f, 0.7f, 1.0f, 1.0f),  // Debug - light blue
                    ImVec4(0.8f, 0.8f, 0.8f, 1.0f),  // Info - white-ish
                    ImVec4(1.0f, 0.8f, 0.3f, 1.0f),  // Warn - yellow
                    ImVec4(1.0f, 0.3f, 0.3f, 1.0f),  // Error - red
                };

                // Case-insensitive search helper
                auto containsIgnoreCase = [](const std::string& haystack, const char* needle) {
                    if (!needle || needle[0] == '\0')
                        return true;
                    std::string lowerHaystack = haystack;
                    std::string lowerNeedle = needle;
                    for (auto& c : lowerHaystack) c = std::tolower(c);
                    for (auto& c : lowerNeedle) c = std::tolower(c);
                    return lowerHaystack.find(lowerNeedle) != std::string::npos;
                };

                for (const auto& entry : history)
                {
                    uint32_t levelIdx = static_cast<uint32_t>(entry.level);
                    if (levelIdx >= 5)
                        continue;

                    // Check level filter
                    if (!debugLogFilters[levelIdx])
                        continue;

                    // Check search filter
                    if (hasSearch && !containsIgnoreCase(entry.message, debugLogSearch))
                        continue;

                    ImGui::PushStyleColor(ImGuiCol_Text, levelColors[levelIdx]);
                    ImGui::TextUnformatted(("[" + std::string(Logger::levelName(entry.level)) + "] " + entry.message).c_str());
                    ImGui::PopStyleColor();
                }

                // Auto-scroll to bottom (only when not searching)
                if (!hasSearch && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                    ImGui::SetScrollHereY(1.0f);

                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::End();
    }

} // namespace vkBasalt
