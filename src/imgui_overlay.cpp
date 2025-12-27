#include "imgui_overlay.hpp"
#include "logger.hpp"
#include "mouse_input.hpp"

#include <algorithm>
#include <cmath>

#include "imgui/imgui.h"
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
            effectEnabledStates = pPersistentState->effectEnabledStates;
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
        pPersistentState->effectEnabledStates = effectEnabledStates;
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
        }

        // Initialize enabled state for selected effects (default to enabled)
        for (const auto& effectName : selectedEffects)
        {
            if (effectEnabledStates.find(effectName) == effectEnabledStates.end())
                effectEnabledStates[effectName] = true;
        }

        // Sync editable params with new state
        // If params changed (different effect list), reset editableParams
        if (editableParams.size() != state.parameters.size())
        {
            editableParams = state.parameters;
            return;
        }

        // Check if params match, update values from state if not modified by user
        for (size_t i = 0; i < state.parameters.size(); i++)
        {
            if (editableParams[i].name != state.parameters[i].name ||
                editableParams[i].effectName != state.parameters[i].effectName)
            {
                // Params don't match, reset
                editableParams = state.parameters;
                return;
            }
            // Keep editable values, but update min/max from state
            editableParams[i].minFloat = state.parameters[i].minFloat;
            editableParams[i].maxFloat = state.parameters[i].maxFloat;
            editableParams[i].minInt = state.parameters[i].minInt;
            editableParams[i].maxInt = state.parameters[i].maxInt;
        }
    }

    std::vector<EffectParameter> ImGuiOverlay::getModifiedParams()
    {
        return editableParams;
    }

    std::vector<std::string> ImGuiOverlay::getActiveEffects() const
    {
        std::vector<std::string> activeEffects;

        // Return enabled effects from selectedEffects
        for (const auto& effectName : selectedEffects)
        {
            auto it = effectEnabledStates.find(effectName);
            if (it != effectEnabledStates.end() && it->second)
                activeEffects.push_back(effectName);
        }

        return activeEffects;
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

            // Sort effects: built-in first (alphabetically), then reshade (alphabetically)
            std::vector<std::string> builtinEffects, reshadeEffects;
            for (const auto& effectName : state.availableEffects)
            {
                if (effectName == "cas" || effectName == "dls" || effectName == "fxaa" ||
                    effectName == "smaa" || effectName == "deband" || effectName == "lut")
                    builtinEffects.push_back(effectName);
                else
                    reshadeEffects.push_back(effectName);
            }
            std::sort(builtinEffects.begin(), builtinEffects.end());
            std::sort(reshadeEffects.begin(), reshadeEffects.end());

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

            // Show built-in effects first
            if (!builtinEffects.empty())
            {
                ImGui::Text("Built-in:");
                for (const auto& effectName : builtinEffects)
                    renderEffectCheckbox(effectName);
            }

            // Show reshade effects
            if (!reshadeEffects.empty())
            {
                if (!builtinEffects.empty())
                    ImGui::Separator();
                ImGui::Text("Reshade:");
                for (const auto& effectName : reshadeEffects)
                    renderEffectCheckbox(effectName);
            }

            ImGui::EndChild();

            ImGui::Separator();

            if (ImGui::Button("OK"))
            {
                // Apply selection
                selectedEffects = tempSelectedEffects;
                // Initialize enabled states for new effects
                for (const auto& effectName : selectedEffects)
                {
                    if (effectEnabledStates.find(effectName) == effectEnabledStates.end())
                        effectEnabledStates[effectName] = true;
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
        else
        {
            // Normal mode - show config and effect controls
            ImGui::Text("Config: %s", state.configPath.c_str());
            ImGui::Separator();
            ImGui::Text("Effects %s (Home to toggle)", state.effectsEnabled ? "ON" : "OFF");
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

                // Drag handle for reordering
                ImGui::Button("::");
                if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0))
                {
                    if (!isDragging)
                    {
                        isDragging = true;
                        dragSourceIndex = static_cast<int>(i);
                    }
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
                ImGui::SameLine();

                // Checkbox to enable/disable effect
                bool& effectEnabled = effectEnabledStates[effectName];
                if (ImGui::Checkbox("##enabled", &effectEnabled))
                {
                    changedThisFrame = true;
                    paramsDirty = true;
                    lastChangeTime = std::chrono::steady_clock::now();
                }
                ImGui::SameLine();

                bool treeOpen = ImGui::TreeNode("effect", "%s", effectName.c_str());
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
                            break;
                        case ParamType::Bool:
                            if (ImGui::Checkbox(param.label.c_str(), &param.valueBool))
                                changed = true;
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
            if (autoApply)
            {
                ImGui::BeginDisabled();
                ImGui::Button("Apply");
                ImGui::EndDisabled();

                // Auto-apply with debounce (200ms after last change)
                // Only apply if no changes happened this frame to ensure latest value
                if (paramsDirty && !changedThisFrame)
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
            }
            else
            {
                if (ImGui::Button("Apply"))
                {
                    applyRequested = true;
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
