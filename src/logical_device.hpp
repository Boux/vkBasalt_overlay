#ifndef LOGICAL_DEVICE_HPP_INCLUDED
#define LOGICAL_DEVICE_HPP_INCLUDED
#include <vector>
#include <fstream>
#include <string>
#include <iostream>
#include <vector>
#include <memory>

#include "vulkan_include.hpp"
#include "vkdispatch.hpp"

namespace vkBasalt
{
    struct OverlayPersistentState;  // Forward declaration
    class ImGuiOverlay;  // Forward declaration

    struct LogicalDevice
    {
        DeviceDispatch           vkd;
        InstanceDispatch         vki;
        VkDevice                 device;
        VkPhysicalDevice         physicalDevice;
        VkInstance               instance;
        VkQueue                  queue;
        uint32_t                 queueFamilyIndex;
        VkCommandPool            commandPool;
        bool                     supportsMutableFormat;
        std::vector<VkImage>     depthImages;
        std::vector<VkFormat>    depthFormats;
        std::vector<VkImageView> depthImageViews;

        // Persistent overlay state that survives swapchain recreation
        std::unique_ptr<OverlayPersistentState> overlayPersistentState;

        // ImGui overlay - lives at device level to survive swapchain recreation
        std::unique_ptr<ImGuiOverlay> imguiOverlay;
    };
} // namespace vkBasalt

#endif // LOGICAL_DEVICE_HPP_INCLUDED
