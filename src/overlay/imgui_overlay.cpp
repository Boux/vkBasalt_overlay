#include "imgui_overlay.hpp"
#include "effect_registry.hpp"
#include "logger.hpp"
#include "mouse_input.hpp"
#include "keyboard_input.hpp"
#include "input_blocker.hpp"
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

        // Restore UI preferences from persistent state
        if (pPersistentState)
        {
            autoApply = pPersistentState->autoApply;
            visible = pPersistentState->visible;
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

    void ImGuiOverlay::toggle()
    {
        visible = !visible;
        setInputBlocked(visible);
        saveToPersistentState();
    }

    void ImGuiOverlay::saveToPersistentState()
    {
        if (!pPersistentState)
            return;

        // Only save UI preferences - effect state is in the registry
        pPersistentState->autoApply = autoApply;
        pPersistentState->visible = visible;
    }

    void ImGuiOverlay::updateState(const OverlayState& newState)
    {
        state = newState;

        if (!pEffectRegistry)
            return;

        // Registry is already initialized from config at swapchain creation
        // Just ensure any newly added effects are in the registry
        const auto& selectedEffects = pEffectRegistry->getSelectedEffects();
        for (const auto& effectName : selectedEffects)
        {
            if (!pEffectRegistry->hasEffect(effectName))
                pEffectRegistry->ensureEffect(effectName);
        }

        // Merge new parameters with existing ones
        for (const auto& newParam : state.parameters)
        {
            bool found = false;
            for (auto& existingParam : editableParams)
            {
                if (existingParam.effectName != newParam.effectName || existingParam.name != newParam.name)
                    continue;
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
                [&selectedEffects](const EffectParameter& p) {
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
        if (!pEffectRegistry)
            return activeEffects;

        for (const auto& effectName : pEffectRegistry->getSelectedEffects())
        {
            if (pEffectRegistry->isEffectEnabled(effectName))
                activeEffects.push_back(effectName);
        }
        return activeEffects;
    }

    const std::vector<std::string>& ImGuiOverlay::getSelectedEffects() const
    {
        static std::vector<std::string> empty;
        return pEffectRegistry ? pEffectRegistry->getSelectedEffects() : empty;
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

        if (!pEffectRegistry)
            return;

        const auto& selectedEffects = pEffectRegistry->getSelectedEffects();

        // Collect disabled effects (from registry)
        std::vector<std::string> disabledEffects;
        for (const auto& effect : selectedEffects)
        {
            if (!pEffectRegistry->isEffectEnabled(effect))
                disabledEffects.push_back(effect);
        }

        // Collect effect paths/types for serialization
        std::map<std::string, std::string> effectPaths;
        std::vector<PreprocessorDefinition> allDefs;
        for (const auto& effectName : selectedEffects)
        {
            if (pEffectRegistry->isEffectBuiltIn(effectName))
            {
                std::string effectType = pEffectRegistry->getEffectType(effectName);
                if (!effectType.empty())
                    effectPaths[effectName] = effectType;
            }
            else
            {
                std::string path = pEffectRegistry->getEffectFilePath(effectName);
                if (!path.empty())
                    effectPaths[effectName] = path;

                const auto& defs = pEffectRegistry->getPreprocessorDefs(effectName);
                for (const auto& def : defs)
                    allDefs.push_back(def);
            }
        }

        ConfigSerializer::saveConfig(saveConfigName, selectedEffects, disabledEffects, params, effectPaths, allDefs);
    }

    void ImGuiOverlay::setSelectedEffects(const std::vector<std::string>& effects,
                                          const std::vector<std::string>& disabledEffects)
    {
        if (!pEffectRegistry)
            return;

        pEffectRegistry->setSelectedEffects(effects);

        // Build set of disabled effects for quick lookup
        std::set<std::string> disabledSet(disabledEffects.begin(), disabledEffects.end());

        // Set enabled states in registry: disabled if in disabledEffects, enabled otherwise
        for (const auto& effectName : effects)
        {
            bool enabled = (disabledSet.find(effectName) == disabledSet.end());
            pEffectRegistry->setEffectEnabled(effectName, enabled);
        }

        // Clear editable params so they get reloaded from the new config
        editableParams.clear();
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

        // Tab bar for main navigation
        if (ImGui::BeginTabBar("MainTabs"))
        {
            if (ImGui::BeginTabItem("Effects"))
            {
                // Reset to main view when switching back to Effects tab
                if (currentTab != 0)
                {
                    inSelectionMode = false;
                    inConfigManageMode = false;
                }
                currentTab = 0;
                if (inSelectionMode)
                    renderAddEffectsView();
                else if (inConfigManageMode)
                    renderConfigManagerView();
                else
                    renderMainView(keyboard);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Shaders"))
            {
                currentTab = 1;
                renderShaderManagerView();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Settings"))
            {
                currentTab = 2;
                renderSettingsView(keyboard);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
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
