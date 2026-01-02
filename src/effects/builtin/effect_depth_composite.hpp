#ifndef EFFECT_DEPTH_COMPOSITE_HPP_INCLUDED
#define EFFECT_DEPTH_COMPOSITE_HPP_INCLUDED

#include <vector>
#include <memory>

#include "vulkan_include.hpp"
#include "../effect.hpp"
#include "config.hpp"
#include "logical_device.hpp"

namespace vkBasalt
{
    // Composites original (pre-effects) and effected images based on depth.
    // Pixels at depth >= threshold get the original image (UI),
    // pixels at depth < threshold get the effected image (3D world).
    class DepthCompositeEffect : public Effect
    {
    public:
        DepthCompositeEffect(LogicalDevice*       pLogicalDevice,
                             VkFormat             format,
                             VkExtent2D           imageExtent,
                             std::vector<VkImage> originalImages,  // Pre-effects images
                             std::vector<VkImage> effectedImages,  // Post-effects images
                             std::vector<VkImage> outputImages,    // Final output
                             Config*              pConfig);
        ~DepthCompositeEffect();

        void applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer) override;
        void useDepthImage(VkImageView depthImageView) override;

    private:
        LogicalDevice*               pLogicalDevice = nullptr;
        std::vector<VkImage>         originalImages;
        std::vector<VkImage>         effectedImages;
        std::vector<VkImage>         outputImages;
        std::vector<VkImageView>     originalImageViews;
        std::vector<VkImageView>     effectedImageViews;
        std::vector<VkImageView>     outputImageViews;
        std::vector<VkDescriptorSet> imageDescriptorSets;
        std::vector<VkFramebuffer>   framebuffers;
        VkDescriptorSetLayout        imageSamplerDescriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool             descriptorPool = VK_NULL_HANDLE;
        VkShaderModule               vertexModule = VK_NULL_HANDLE;
        VkShaderModule               fragmentModule = VK_NULL_HANDLE;
        VkRenderPass                 renderPass = VK_NULL_HANDLE;
        VkPipelineLayout             pipelineLayout = VK_NULL_HANDLE;
        VkPipeline                   graphicsPipeline = VK_NULL_HANDLE;
        VkExtent2D                   imageExtent = {};
        VkFormat                     format = VK_FORMAT_UNDEFINED;
        VkSampler                    sampler = VK_NULL_HANDLE;
        VkImageView                  depthImageView = VK_NULL_HANDLE;
        bool                         descriptorSetsNeedUpdate = true;
    };
} // namespace vkBasalt

#endif // EFFECT_DEPTH_COMPOSITE_HPP_INCLUDED
