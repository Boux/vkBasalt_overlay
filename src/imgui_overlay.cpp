#include "imgui_overlay.hpp"
#include "effect_registry.hpp"
#include "logger.hpp"
#include "mouse_input.hpp"
#include "keyboard_input.hpp"
#include "config_serializer.hpp"

#include <algorithm>
#include <cmath>

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/backends/imgui_impl_vulkan.h"

namespace vkBasalt
{
    // Dummy function for Vulkan functions not in vkBasalt's dispatch
    static void dummyVulkanFunc() {}

    // Function loader using vkBasalt's dispatch tables
    static PFN_vkVoidFunction imguiVulkanLoaderDummy(const char* function_name, void* user_data)
    {
        LogicalDevice* device = static_cast<LogicalDevice*>(user_data);

        // Device functions from vkBasalt's dispatch table
        #define CHECK_FUNC(name) if (strcmp(function_name, "vk" #name) == 0) return (PFN_vkVoidFunction)device->vkd.name

        CHECK_FUNC(AllocateCommandBuffers);
        CHECK_FUNC(AllocateDescriptorSets);
        CHECK_FUNC(AllocateMemory);
        CHECK_FUNC(BeginCommandBuffer);
        CHECK_FUNC(BindBufferMemory);
        CHECK_FUNC(BindImageMemory);
        CHECK_FUNC(CmdBeginRenderPass);
        CHECK_FUNC(CmdBindDescriptorSets);
        CHECK_FUNC(CmdBindIndexBuffer);
        CHECK_FUNC(CmdBindPipeline);
        CHECK_FUNC(CmdBindVertexBuffers);
        CHECK_FUNC(CmdCopyBufferToImage);
        CHECK_FUNC(CmdDrawIndexed);
        CHECK_FUNC(CmdEndRenderPass);
        CHECK_FUNC(CmdPipelineBarrier);
        CHECK_FUNC(CmdPushConstants);
        CHECK_FUNC(CmdSetScissor);
        CHECK_FUNC(CmdSetViewport);
        CHECK_FUNC(CreateBuffer);
        CHECK_FUNC(CreateCommandPool);
        CHECK_FUNC(CreateDescriptorPool);
        CHECK_FUNC(CreateDescriptorSetLayout);
        CHECK_FUNC(CreateFence);
        CHECK_FUNC(CreateFramebuffer);
        CHECK_FUNC(CreateGraphicsPipelines);
        CHECK_FUNC(CreateImage);
        CHECK_FUNC(CreateImageView);
        CHECK_FUNC(CreatePipelineLayout);
        CHECK_FUNC(CreateRenderPass);
        CHECK_FUNC(CreateSampler);
        CHECK_FUNC(CreateSemaphore);
        CHECK_FUNC(CreateShaderModule);
        CHECK_FUNC(CreateSwapchainKHR);
        CHECK_FUNC(DestroyBuffer);
        CHECK_FUNC(DestroyCommandPool);
        CHECK_FUNC(DestroyDescriptorPool);
        CHECK_FUNC(DestroyDescriptorSetLayout);
        CHECK_FUNC(DestroyFence);
        CHECK_FUNC(DestroyFramebuffer);
        CHECK_FUNC(DestroyImage);
        CHECK_FUNC(DestroyImageView);
        CHECK_FUNC(DestroyPipeline);
        CHECK_FUNC(DestroyPipelineLayout);
        CHECK_FUNC(DestroyRenderPass);
        CHECK_FUNC(DestroySampler);
        CHECK_FUNC(DestroySemaphore);
        CHECK_FUNC(DestroyShaderModule);
        CHECK_FUNC(DestroySwapchainKHR);
        CHECK_FUNC(EndCommandBuffer);
        CHECK_FUNC(FlushMappedMemoryRanges);
        CHECK_FUNC(FreeCommandBuffers);
        CHECK_FUNC(FreeDescriptorSets);
        CHECK_FUNC(FreeMemory);
        CHECK_FUNC(GetBufferMemoryRequirements);
        CHECK_FUNC(GetDeviceQueue);
        CHECK_FUNC(GetImageMemoryRequirements);
        CHECK_FUNC(GetSwapchainImagesKHR);
        CHECK_FUNC(MapMemory);
        CHECK_FUNC(QueueSubmit);
        CHECK_FUNC(QueueWaitIdle);
        CHECK_FUNC(ResetCommandPool);
        CHECK_FUNC(UnmapMemory);
        CHECK_FUNC(UpdateDescriptorSets);

        #undef CHECK_FUNC

        // Instance functions from vkBasalt's dispatch
        #define CHECK_IFUNC(name) if (strcmp(function_name, "vk" #name) == 0) return (PFN_vkVoidFunction)device->vki.name
        CHECK_IFUNC(GetPhysicalDeviceMemoryProperties);
        CHECK_IFUNC(GetPhysicalDeviceProperties);
        CHECK_IFUNC(GetPhysicalDeviceQueueFamilyProperties);
        #undef CHECK_IFUNC

        // Return dummy for all remaining functions - don't use GetInstanceProcAddr
        // (GetInstanceProcAddr causes rendering issues)
        return (PFN_vkVoidFunction)dummyVulkanFunc;
    }

    ImGuiOverlay::ImGuiOverlay(LogicalDevice* device, VkFormat swapchainFormat, uint32_t imageCount, OverlayPersistentState* persistentState)
        : pLogicalDevice(device), pPersistentState(persistentState)
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;

        std::string iniPath = ConfigSerializer::getBaseConfigDir() + "/imgui.ini";
        ImGui::LoadIniSettingsFromDisk(iniPath.c_str());

        ImGui::StyleColorsDark();

        // Make it semi-transparent
        ImGuiStyle& style = ImGui::GetStyle();
        style.Alpha = 0.9f;
        style.WindowRounding = 5.0f;

        initVulkanBackend(swapchainFormat, imageCount);

        // Restore state from persistent state if available
        if (pPersistentState && pPersistentState->initialized)
        {
            selectedEffects = pPersistentState->selectedEffects;
            // Note: effectEnabledStates now live in EffectRegistry (single source of truth)
            editableParams = pPersistentState->editableParams;
            autoApply = pPersistentState->autoApply;
            visible = pPersistentState->visible;
            applyRequested = true;  // Rebuild effect chain with restored state
        }

        initialized = true;
        Logger::info("ImGui overlay initialized");
    }

    ImGuiOverlay::~ImGuiOverlay()
    {
        if (!initialized) return;

        pLogicalDevice->vkd.QueueWaitIdle(pLogicalDevice->queue);

        std::string iniPath = ConfigSerializer::getBaseConfigDir() + "/imgui.ini";
        ImGui::SaveIniSettingsToDisk(iniPath.c_str());

        if (backendInitialized)
            ImGui_ImplVulkan_Shutdown();
        ImGui::DestroyContext();

        if (commandPool != VK_NULL_HANDLE)
            pLogicalDevice->vkd.DestroyCommandPool(pLogicalDevice->device, commandPool, nullptr);
        if (renderPass != VK_NULL_HANDLE)
            pLogicalDevice->vkd.DestroyRenderPass(pLogicalDevice->device, renderPass, nullptr);
        if (descriptorPool != VK_NULL_HANDLE)
            pLogicalDevice->vkd.DestroyDescriptorPool(pLogicalDevice->device, descriptorPool, nullptr);

        Logger::info("ImGui overlay destroyed");
    }

    void ImGuiOverlay::saveToPersistentState()
    {
        if (!pPersistentState)
            return;

        pPersistentState->selectedEffects = selectedEffects;
        // Note: effectEnabledStates now live in EffectRegistry (single source of truth)
        pPersistentState->editableParams = editableParams;
        pPersistentState->autoApply = autoApply;
        pPersistentState->visible = visible;
        pPersistentState->initialized = true;
    }

    void ImGuiOverlay::updateState(const OverlayState& newState)
    {
        state = newState;

        // Initialize selectedEffects with config effects on first call
        if (selectedEffects.empty())
        {
            for (const auto& effectName : state.effectNames)
                selectedEffects.push_back(effectName);

            // Set enabled states from config's disabledEffects (via registry)
            if (pEffectRegistry)
            {
                for (const auto& effectName : selectedEffects)
                {
                    bool isDisabled = std::find(state.disabledEffects.begin(), state.disabledEffects.end(), effectName)
                                      != state.disabledEffects.end();
                    pEffectRegistry->setEffectEnabled(effectName, !isDisabled);
                }
            }
        }

        // Initialize enabled state for new effects (default to enabled, via registry)
        if (pEffectRegistry)
        {
            for (const auto& effectName : selectedEffects)
            {
                if (!pEffectRegistry->hasEffect(effectName))
                    pEffectRegistry->ensureEffect(effectName);
            }
        }

        // Merge new parameters with existing ones
        // Keep existing params for effects that are still selected but not in new params
        // (happens when ReShade effects are disabled - they're not loaded so no params returned)
        for (const auto& newParam : state.parameters)
        {
            // Find existing param with same effect and name
            bool found = false;
            for (auto& existingParam : editableParams)
            {
                if (existingParam.effectName != newParam.effectName || existingParam.name != newParam.name)
                    continue;
                // Update min/max but keep user-edited value
                existingParam.minFloat = newParam.minFloat;
                existingParam.maxFloat = newParam.maxFloat;
                existingParam.minInt = newParam.minInt;
                existingParam.maxInt = newParam.maxInt;
                found = true;
                break;
            }
            if (!found)
                editableParams.push_back(newParam);
        }

        // Remove params for effects that are no longer selected
        editableParams.erase(
            std::remove_if(editableParams.begin(), editableParams.end(),
                [this](const EffectParameter& p) {
                    return std::find(selectedEffects.begin(), selectedEffects.end(), p.effectName) == selectedEffects.end();
                }),
            editableParams.end());
    }

    std::vector<EffectParameter> ImGuiOverlay::getModifiedParams()
    {
        return editableParams;
    }

    std::vector<std::string> ImGuiOverlay::getActiveEffects() const
    {
        std::vector<std::string> activeEffects;
        for (const auto& effectName : selectedEffects)
        {
            // Use registry as single source of truth for enabled state
            bool enabled = pEffectRegistry ? pEffectRegistry->isEffectEnabled(effectName) : true;
            if (enabled)
                activeEffects.push_back(effectName);
        }
        return activeEffects;
    }

    void ImGuiOverlay::saveCurrentConfig()
    {
        // Collect parameters that differ from defaults
        std::vector<EffectParam> params;
        for (const auto& p : editableParams)
        {
            bool differs = false;
            if (p.type == ParamType::Float)
                differs = (p.valueFloat != p.defaultFloat);
            else if (p.type == ParamType::Int)
                differs = (p.valueInt != p.defaultInt);
            else
                differs = (p.valueBool != p.defaultBool);

            if (!differs)
                continue;

            EffectParam ep;
            ep.effectName = p.effectName;
            ep.paramName = p.name;
            if (p.type == ParamType::Float)
                ep.value = std::to_string(p.valueFloat);
            else if (p.type == ParamType::Int)
                ep.value = std::to_string(p.valueInt);
            else
                ep.value = p.valueBool ? "true" : "false";
            params.push_back(ep);
        }

        // Collect disabled effects (from registry)
        std::vector<std::string> disabledEffects;
        for (const auto& effect : selectedEffects)
        {
            bool enabled = pEffectRegistry ? pEffectRegistry->isEffectEnabled(effect) : true;
            if (!enabled)
                disabledEffects.push_back(effect);
        }

        ConfigSerializer::saveConfig(saveConfigName, selectedEffects, disabledEffects, params);
    }

    void ImGuiOverlay::setSelectedEffects(const std::vector<std::string>& effects,
                                          const std::vector<std::string>& disabledEffects)
    {
        selectedEffects = effects;

        // Build set of disabled effects for quick lookup
        std::set<std::string> disabledSet(disabledEffects.begin(), disabledEffects.end());

        // Set enabled states in registry: disabled if in disabledEffects, enabled otherwise
        if (pEffectRegistry)
        {
            for (const auto& effectName : selectedEffects)
            {
                bool enabled = (disabledSet.find(effectName) == disabledSet.end());
                pEffectRegistry->setEffectEnabled(effectName, enabled);
            }
        }

        // Clear editable params so they get reloaded from the new config
        // (otherwise updateState() would preserve old values)
        editableParams.clear();

        saveToPersistentState();
    }

    void ImGuiOverlay::initVulkanBackend(VkFormat swapchainFormat, uint32_t imageCount)
    {
        // Load Vulkan functions for ImGui using vkBasalt's dispatch tables
        bool loaded = ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_3, imguiVulkanLoaderDummy, pLogicalDevice);
        if (!loaded)
        {
            Logger::err("Failed to load Vulkan functions for ImGui");
            return;
        }
        Logger::debug("ImGui Vulkan functions loaded");

        // Create descriptor pool for ImGui
        VkDescriptorPoolSize poolSizes[] = {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 }
        };

        VkDescriptorPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets = 100;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = poolSizes;

        pLogicalDevice->vkd.CreateDescriptorPool(pLogicalDevice->device, &poolInfo, nullptr, &descriptorPool);

        // Create render pass for ImGui
        VkAttachmentDescription attachment = {};
        attachment.format = swapchainFormat;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorRef = {};
        colorRef.attachment = 0;
        colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;

        VkSubpassDependency dependency = {};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &attachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        pLogicalDevice->vkd.CreateRenderPass(pLogicalDevice->device, &renderPassInfo, nullptr, &renderPass);

        // Initialize ImGui Vulkan backend
        ImGui_ImplVulkan_InitInfo initInfo = {};
        initInfo.ApiVersion = VK_API_VERSION_1_3;
        initInfo.Instance = pLogicalDevice->instance;
        initInfo.PhysicalDevice = pLogicalDevice->physicalDevice;
        initInfo.Device = pLogicalDevice->device;
        initInfo.QueueFamily = pLogicalDevice->queueFamilyIndex;
        initInfo.Queue = pLogicalDevice->queue;
        initInfo.DescriptorPool = descriptorPool;
        initInfo.MinImageCount = 2;
        initInfo.ImageCount = 2;
        initInfo.PipelineInfoMain.RenderPass = renderPass;

        ImGui_ImplVulkan_Init(&initInfo);
        backendInitialized = true;

        this->swapchainFormat = swapchainFormat;
        this->imageCount = imageCount;

        // Create command pool
        VkCommandPoolCreateInfo poolCreateInfo = {};
        poolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolCreateInfo.queueFamilyIndex = pLogicalDevice->queueFamilyIndex;
        pLogicalDevice->vkd.CreateCommandPool(pLogicalDevice->device, &poolCreateInfo, nullptr, &commandPool);

        // Allocate command buffers
        commandBuffers.resize(imageCount);
        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = imageCount;
        pLogicalDevice->vkd.AllocateCommandBuffers(pLogicalDevice->device, &allocInfo, commandBuffers.data());

        Logger::debug("ImGui Vulkan backend initialized");
    }

    VkCommandBuffer ImGuiOverlay::recordFrame(uint32_t imageIndex, VkImageView imageView, uint32_t width, uint32_t height)
    {
        if (!backendInitialized || !visible)
            return VK_NULL_HANDLE;

        VkCommandBuffer cmd = commandBuffers[imageIndex];

        // Begin command buffer
        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        pLogicalDevice->vkd.BeginCommandBuffer(cmd, &beginInfo);

        // Create framebuffer for this image view
        VkFramebuffer framebuffer;
        VkFramebufferCreateInfo fbInfo = {};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = renderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &imageView;
        fbInfo.width = width;
        fbInfo.height = height;
        fbInfo.layers = 1;
        pLogicalDevice->vkd.CreateFramebuffer(pLogicalDevice->device, &fbInfo, nullptr, &framebuffer);

        // Set display size and mouse input BEFORE NewFrame
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)width, (float)height);

        // Mouse input for interactivity
        MouseState mouse = getMouseState();
        io.MousePos = ImVec2((float)mouse.x, (float)mouse.y);
        io.MouseDown[0] = mouse.leftButton;
        io.MouseDown[1] = mouse.rightButton;
        io.MouseDown[2] = mouse.middleButton;
        io.MouseWheel = mouse.scrollDelta;
        io.MouseDrawCursor = true;  // Draw software cursor (games often hide the OS cursor)

        // Keyboard input for text fields
        // Keys are one-shot events, so we send press and release in same frame
        KeyboardState keyboard = getKeyboardState();
        for (char c : keyboard.typedChars)
            io.AddInputCharacter(c);
        if (keyboard.backspace) { io.AddKeyEvent(ImGuiKey_Backspace, true); io.AddKeyEvent(ImGuiKey_Backspace, false); }
        if (keyboard.del) { io.AddKeyEvent(ImGuiKey_Delete, true); io.AddKeyEvent(ImGuiKey_Delete, false); }
        if (keyboard.enter) { io.AddKeyEvent(ImGuiKey_Enter, true); io.AddKeyEvent(ImGuiKey_Enter, false); }
        if (keyboard.left) { io.AddKeyEvent(ImGuiKey_LeftArrow, true); io.AddKeyEvent(ImGuiKey_LeftArrow, false); }
        if (keyboard.right) { io.AddKeyEvent(ImGuiKey_RightArrow, true); io.AddKeyEvent(ImGuiKey_RightArrow, false); }
        if (keyboard.home) { io.AddKeyEvent(ImGuiKey_Home, true); io.AddKeyEvent(ImGuiKey_Home, false); }
        if (keyboard.end) { io.AddKeyEvent(ImGuiKey_End, true); io.AddKeyEvent(ImGuiKey_End, false); }

        // ImGui frame
        ImGui_ImplVulkan_NewFrame();
        ImGui::NewFrame();

        // vkBasalt info window
        ImGui::Begin("vkBasalt Controls");

        if (inSelectionMode)
        {
            // Selection mode - show all available effects with checkboxes
            ImGui::Text("Select Effects (max %zu)", maxEffects);
            ImGui::Separator();

            size_t selectedCount = tempSelectedEffects.size();
            ImGui::Text("Selected: %zu / %zu", selectedCount, maxEffects);
            ImGui::Separator();

            // Built-in effects (hardcoded)
            std::vector<std::string> builtinEffects = {"cas", "dls", "fxaa", "smaa", "deband", "lut"};

            // Helper to check if effect is in vector
            auto containsEffect = [](const std::vector<std::string>& vec, const std::string& name) {
                return std::find(vec.begin(), vec.end(), name) != vec.end();
            };

            // Helper lambda to render effect checkbox
            auto renderEffectCheckbox = [&](const std::string& effectName) {
                bool isSelected = containsEffect(tempSelectedEffects, effectName);

                // Disable checkbox if at max and not selected
                bool atLimit = selectedCount >= maxEffects && !isSelected;
                if (atLimit)
                    ImGui::BeginDisabled();

                if (ImGui::Checkbox(effectName.c_str(), &isSelected))
                {
                    if (isSelected)
                        tempSelectedEffects.push_back(effectName);
                    else
                        tempSelectedEffects.erase(std::find(tempSelectedEffects.begin(), tempSelectedEffects.end(), effectName));
                }

                if (atLimit)
                    ImGui::EndDisabled();
            };

            // Scrollable effect list (reserve space for footer buttons)
            float footerHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
            ImGui::BeginChild("SelectionList", ImVec2(0, -footerHeight), false);

            // Sort effects for each category
            std::vector<std::string> sortedCurrentConfig = state.currentConfigEffects;
            std::vector<std::string> sortedDefaultConfig = state.defaultConfigEffects;
            std::sort(sortedCurrentConfig.begin(), sortedCurrentConfig.end());
            std::sort(sortedDefaultConfig.begin(), sortedDefaultConfig.end());

            // Category 1: Built-in effects (already sorted in definition)
            ImGui::Text("Built-in:");
            for (const auto& effectName : builtinEffects)
                renderEffectCheckbox(effectName);

            // Category 2: ReShade effects from current config
            if (!sortedCurrentConfig.empty())
            {
                ImGui::Separator();
                ImGui::Text("ReShade (%s):", state.configName.c_str());
                for (const auto& effectName : sortedCurrentConfig)
                    renderEffectCheckbox(effectName);
            }

            // Category 3: ReShade effects from default config (no duplicates)
            if (!sortedDefaultConfig.empty())
            {
                ImGui::Separator();
                ImGui::Text("ReShade (all):");
                for (const auto& effectName : sortedDefaultConfig)
                    renderEffectCheckbox(effectName);
            }

            ImGui::EndChild();

            ImGui::Separator();

            if (ImGui::Button("OK"))
            {
                // Apply selection
                selectedEffects = tempSelectedEffects;
                // Initialize enabled states for new effects in registry (default to enabled)
                if (pEffectRegistry)
                {
                    for (const auto& effectName : selectedEffects)
                    {
                        if (!pEffectRegistry->hasEffect(effectName))
                        {
                            pEffectRegistry->ensureEffect(effectName);
                            pEffectRegistry->setEffectEnabled(effectName, true);
                        }
                    }
                }
                inSelectionMode = false;
                applyRequested = true;  // Trigger reload with new effects
                saveToPersistentState();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                inSelectionMode = false;
            }
        }
        else if (inConfigManageMode)
        {
            // Config management mode
            ImGui::Text("Manage Configs");
            ImGui::Separator();

            // Refresh config list and get current default
            configList = ConfigSerializer::listConfigs();
            std::string currentDefault = ConfigSerializer::getDefaultConfig();

            ImGui::BeginChild("ConfigList", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), false);
            for (size_t i = 0; i < configList.size(); i++)
            {
                ImGui::PushID(static_cast<int>(i));
                const std::string& cfg = configList[i];

                // Selectable config name - click to load (limited width so buttons are clickable)
                float buttonAreaWidth = 130;
                float nameWidth = ImGui::GetWindowWidth() - buttonAreaWidth;
                if (ImGui::Selectable(cfg.c_str(), false, 0, ImVec2(nameWidth, 0)))
                {
                    // Signal to basalt.cpp to load this config
                    pendingConfigPath = ConfigSerializer::getConfigsDir() + "/" + cfg + ".conf";
                    strncpy(saveConfigName, cfg.c_str(), sizeof(saveConfigName) - 1);
                    applyRequested = true;
                    inConfigManageMode = false;
                }
                ImGui::SameLine();

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
        else
        {
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
            ImGui::TextDisabled("(Home)");
            ImGui::Separator();

            // Select Effects button
            if (ImGui::Button("Select Effects..."))
            {
                // Enter selection mode, copy current selection to temp
                tempSelectedEffects = selectedEffects;
                inSelectionMode = true;
            }
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

                // Checkbox to enable/disable effect (read/write via registry)
                bool effectEnabled = pEffectRegistry ? pEffectRegistry->isEffectEnabled(effectName) : true;
                if (ImGui::Checkbox("##enabled", &effectEnabled))
                {
                    if (pEffectRegistry)
                        pEffectRegistry->setEffectEnabled(effectName, effectEnabled);
                    changedThisFrame = true;
                    paramsDirty = true;
                    lastChangeTime = std::chrono::steady_clock::now();
                }
                ImGui::SameLine();

                bool treeOpen = ImGui::TreeNode("effect", "%s", effectName.c_str());

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

                    // Remove effect
                    if (ImGui::MenuItem("Remove"))
                    {
                        selectedEffects.erase(selectedEffects.begin() + i);
                        changedThisFrame = true;
                        paramsDirty = true;
                        lastChangeTime = std::chrono::steady_clock::now();
                    }

                    ImGui::EndPopup();
                }

                ImGui::PopID();

                if (treeOpen)
                {
                    // Find and show parameters for this effect
                    int paramIndex = 0;
                    for (auto& param : editableParams)
                    {
                        if (param.effectName != effectName)
                            continue;

                        ImGui::PushID(paramIndex);
                        bool changed = false;
                        switch (param.type)
                        {
                        case ParamType::Float:
                            if (ImGui::SliderFloat(param.label.c_str(), &param.valueFloat, param.minFloat, param.maxFloat))
                            {
                                // Snap to step if specified
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
                            if (!param.tooltip.empty() && ImGui::IsItemHovered())
                                ImGui::SetTooltip("%s", param.tooltip.c_str());
                            break;
                        case ParamType::Int:
                            // Check for combo box (ui_type="combo" or has items)
                            if (!param.items.empty())
                            {
                                // Build items string for Combo (null-separated, double-null terminated)
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
                                    // Snap to step if specified
                                    if (param.step > 0.0f)
                                    {
                                        int step = (int)param.step;
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
                            if (!param.tooltip.empty() && ImGui::IsItemHovered())
                                ImGui::SetTooltip("%s", param.tooltip.c_str());
                            break;
                        case ParamType::Bool:
                            if (ImGui::Checkbox(param.label.c_str(), &param.valueBool))
                                changed = true;
                            if (!param.tooltip.empty() && ImGui::IsItemHovered())
                                ImGui::SetTooltip("%s", param.tooltip.c_str());
                            break;
                        }
                        if (changed)
                        {
                            paramsDirty = true;
                            changedThisFrame = true;
                            lastChangeTime = std::chrono::steady_clock::now();
                        }
                        ImGui::PopID();
                        paramIndex++;
                    }
                    ImGui::TreePop();
                }
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
                        changedThisFrame = true;
                        paramsDirty = true;
                        lastChangeTime = std::chrono::steady_clock::now();
                        saveToPersistentState();
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
            ImGui::SameLine(ImGui::GetWindowWidth() - 60);

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

        ImGui::End();

        ImGui::Render();

        // Begin render pass
        VkRenderPassBeginInfo rpBegin = {};
        rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass = renderPass;
        rpBegin.framebuffer = framebuffer;
        rpBegin.renderArea.extent.width = width;
        rpBegin.renderArea.extent.height = height;

        pLogicalDevice->vkd.CmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
        pLogicalDevice->vkd.CmdEndRenderPass(cmd);

        pLogicalDevice->vkd.EndCommandBuffer(cmd);

        // Destroy framebuffer (created per-frame)
        pLogicalDevice->vkd.DestroyFramebuffer(pLogicalDevice->device, framebuffer, nullptr);

        return cmd;
    }

} // namespace vkBasalt
