#include "imgui_overlay.hpp"
#include "config_serializer.hpp"
#include "logger.hpp"

#include <cstdlib>
#include <filesystem>
#include <algorithm>
#include <set>

#include "imgui/imgui.h"
#include "imgui/imfilebrowser.h"

namespace vkBasalt
{
    // Static file browser for adding directories
    static ImGui::FileBrowser dirBrowser(
        ImGuiFileBrowserFlags_SelectDirectory |
        ImGuiFileBrowserFlags_HideRegularFiles |
        ImGuiFileBrowserFlags_CloseOnEsc |
        ImGuiFileBrowserFlags_CreateNewDir);

    // Case-insensitive string comparison
    static bool equalsIgnoreCase(const std::string& a, const std::string& b)
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); i++)
        {
            if (std::tolower(static_cast<unsigned char>(a[i])) !=
                std::tolower(static_cast<unsigned char>(b[i])))
                return false;
        }
        return true;
    }

    // Recursively scan directory for Shaders/ and Textures/ subdirectories
    static void scanDirectory(
        const std::filesystem::path& dir,
        std::set<std::string>& shaderPaths,
        std::set<std::string>& texturePaths)
    {
        try
        {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(
                dir, std::filesystem::directory_options::skip_permission_denied))
            {
                if (!entry.is_directory())
                    continue;

                std::string dirName = entry.path().filename().string();
                if (equalsIgnoreCase(dirName, "Shaders"))
                    shaderPaths.insert(entry.path().string());
                else if (equalsIgnoreCase(dirName, "Textures"))
                    texturePaths.insert(entry.path().string());
            }
        }
        catch (const std::filesystem::filesystem_error& e)
        {
            Logger::err("Shader Manager: Error scanning " + dir.string() + ": " + e.what());
        }
    }

    void ImGuiOverlay::renderShaderManagerView()
    {
        // Load config on first open
        if (!shaderMgrInitialized)
        {
            ShaderManagerConfig config = ConfigSerializer::loadShaderManagerConfig();
            shaderMgrParentDirs = config.parentDirectories;
            shaderMgrShaderPaths = config.discoveredShaderPaths;
            shaderMgrTexturePaths = config.discoveredTexturePaths;
            shaderMgrInitialized = true;
        }

        ImGui::BeginChild("ShaderMgrContent", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), false);

        // Parent Directories section
        ImGui::Text("Parent Directories");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Add directories containing ReShade shader packs.\nThey will be scanned for Shaders/ and Textures/ subdirectories.");

        if (ImGui::Button("Browse..."))
        {
            dirBrowser.SetTitle("Select Parent Directory");
            const char* home = std::getenv("HOME");
            dirBrowser.SetPwd(home ? home : "/");
            dirBrowser.Open();
        }

        // List parent directories with remove buttons
        ImGui::BeginChild("ParentDirList", ImVec2(0, 120), true);
        int removeIdx = -1;
        for (size_t i = 0; i < shaderMgrParentDirs.size(); i++)
        {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Button("X"))
                removeIdx = static_cast<int>(i);
            ImGui::SameLine();
            ImGui::TextUnformatted(shaderMgrParentDirs[i].c_str());
            ImGui::PopID();
        }
        if (shaderMgrParentDirs.empty())
            ImGui::TextDisabled("No directories added");
        ImGui::EndChild();

        if (removeIdx >= 0)
            shaderMgrParentDirs.erase(shaderMgrParentDirs.begin() + removeIdx);

        // Rescan button and stats
        ImGui::Spacing();
        if (ImGui::Button("Rescan All"))
        {
            std::set<std::string> shaderSet, textureSet;
            for (const auto& parentDir : shaderMgrParentDirs)
            {
                if (std::filesystem::exists(parentDir) && std::filesystem::is_directory(parentDir))
                    scanDirectory(parentDir, shaderSet, textureSet);
            }
            shaderMgrShaderPaths.assign(shaderSet.begin(), shaderSet.end());
            shaderMgrTexturePaths.assign(textureSet.begin(), textureSet.end());
            Logger::info("Shader Manager: Found " + std::to_string(shaderMgrShaderPaths.size()) +
                " shader paths, " + std::to_string(shaderMgrTexturePaths.size()) + " texture paths");
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(%zu shader paths, %zu texture paths)",
            shaderMgrShaderPaths.size(), shaderMgrTexturePaths.size());

        ImGui::Separator();

        // Discovered Shader Paths (collapsible)
        if (ImGui::TreeNode("Discovered Shader Paths"))
        {
            if (shaderMgrShaderPaths.empty())
                ImGui::TextDisabled("None - click Rescan All");
            else
            {
                for (const auto& path : shaderMgrShaderPaths)
                    ImGui::BulletText("%s", path.c_str());
            }
            ImGui::TreePop();
        }

        // Discovered Texture Paths (collapsible)
        if (ImGui::TreeNode("Discovered Texture Paths"))
        {
            if (shaderMgrTexturePaths.empty())
                ImGui::TextDisabled("None - click Rescan All");
            else
            {
                for (const auto& path : shaderMgrTexturePaths)
                    ImGui::BulletText("%s", path.c_str());
            }
            ImGui::TreePop();
        }

        ImGui::EndChild();

        // Display file browser (must be called every frame when open)
        dirBrowser.Display();
        if (dirBrowser.HasSelected())
        {
            std::string selectedPath = dirBrowser.GetSelected().string();
            // Avoid duplicates
            bool exists = false;
            for (const auto& dir : shaderMgrParentDirs)
            {
                if (dir == selectedPath)
                {
                    exists = true;
                    break;
                }
            }
            if (!exists)
                shaderMgrParentDirs.push_back(selectedPath);
            dirBrowser.ClearSelected();
        }

        // Footer button
        if (ImGui::Button("Save"))
        {
            ShaderManagerConfig config;
            config.parentDirectories = shaderMgrParentDirs;
            config.discoveredShaderPaths = shaderMgrShaderPaths;
            config.discoveredTexturePaths = shaderMgrTexturePaths;
            ConfigSerializer::saveShaderManagerConfig(config);
            shaderPathsChanged = true;  // Signal basalt.cpp to refresh effect list
        }
    }

} // namespace vkBasalt
