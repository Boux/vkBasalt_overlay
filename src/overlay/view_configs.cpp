#include "imgui_overlay.hpp"
#include "config_serializer.hpp"

#include <cstring>

#include "imgui/imgui.h"

namespace vkBasalt
{
    void ImGuiOverlay::renderConfigManagerView()
    {
        // Config management mode
        ImGui::Text("Manage Configs");
        ImGui::Separator();

        // Refresh config list and get current default
        configList = ConfigSerializer::listConfigs();
        std::string currentDefault = ConfigSerializer::getDefaultConfig();

        // Calculate button group width once
        float setDefaultWidth = ImGui::CalcTextSize("Set Default").x + ImGui::GetStyle().FramePadding.x * 2;
        float deleteWidth = ImGui::CalcTextSize("Delete").x + ImGui::GetStyle().FramePadding.x * 2;
        float buttonGroupWidth = setDefaultWidth + deleteWidth + ImGui::GetStyle().ItemSpacing.x;

        ImGui::BeginChild("ConfigList", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), false);
        float buttonGroupX = ImGui::GetContentRegionAvail().x - buttonGroupWidth;
        for (size_t i = 0; i < configList.size(); i++)
        {
            ImGui::PushID(static_cast<int>(i));
            const std::string& cfg = configList[i];

            // Selectable config name - click to load
            float nameWidth = buttonGroupX - ImGui::GetStyle().ItemSpacing.x;
            if (ImGui::Selectable(cfg.c_str(), false, 0, ImVec2(nameWidth, 0)))
            {
                // Signal to basalt.cpp to load this config
                pendingConfigPath = ConfigSerializer::getConfigsDir() + "/" + cfg + ".conf";
                strncpy(saveConfigName, cfg.c_str(), sizeof(saveConfigName) - 1);
                applyRequested = true;
                inConfigManageMode = false;
            }
            ImGui::SameLine(buttonGroupX);

            bool isDefault = (cfg == currentDefault);
            if (isDefault)
                ImGui::BeginDisabled();
            if (ImGui::SmallButton("Set Default"))
            {
                ConfigSerializer::setDefaultConfig(cfg);
            }
            if (isDefault)
                ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete"))
            {
                ConfigSerializer::deleteConfig(cfg);
            }
            ImGui::PopID();
        }
        if (configList.empty())
        {
            ImGui::Text("No saved configs");
        }
        ImGui::EndChild();

        if (ImGui::Button("Back"))
        {
            inConfigManageMode = false;
        }
    }

} // namespace vkBasalt
