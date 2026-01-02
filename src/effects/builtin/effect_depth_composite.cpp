#include "effect_depth_composite.hpp"

#include <cstring>

#include "image_view.hpp"
#include "descriptor_set.hpp"
#include "renderpass.hpp"
#include "graphics_pipeline.hpp"
#include "framebuffer.hpp"
#include "shader.hpp"
#include "sampler.hpp"
#include "logger.hpp"
#include "util.hpp"

#include "shader_sources.hpp"

namespace vkBasalt
{
    DepthCompositeEffect::DepthCompositeEffect(LogicalDevice*       pLogicalDevice,
                                               VkFormat             format,
                                               VkExtent2D           imageExtent,
                                               std::vector<VkImage> originalImages,
                                               std::vector<VkImage> effectedImages,
                                               std::vector<VkImage> outputImages,
                                               Config*              pConfig)
    {
        Logger::debug("Creating DepthCompositeEffect");

        this->pLogicalDevice  = pLogicalDevice;
        this->format          = format;
        this->imageExtent     = imageExtent;
        this->originalImages  = originalImages;
        this->effectedImages  = effectedImages;
        this->outputImages    = outputImages;

        // Create image views
        originalImageViews = createImageViews(pLogicalDevice, format, originalImages);
        effectedImageViews = createImageViews(pLogicalDevice, format, effectedImages);
        outputImageViews   = createImageViews(pLogicalDevice, format, outputImages);

        // Create sampler
        sampler = createSampler(pLogicalDevice);

        // Create descriptor set layout with 3 bindings:
        // binding 0: original image
        // binding 1: effected image
        // binding 2: depth image
        imageSamplerDescriptorSetLayout = createImageSamplerDescriptorSetLayout(pLogicalDevice, 3);

        // Create descriptor pool (3 samplers per image)
        VkDescriptorPoolSize imagePoolSize;
        imagePoolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        imagePoolSize.descriptorCount = originalImages.size() * 3 + 10;

        std::vector<VkDescriptorPoolSize> poolSizes = {imagePoolSize};
        descriptorPool = createDescriptorPool(pLogicalDevice, poolSizes);

        // Create shader modules
        createShaderModule(pLogicalDevice, full_screen_triangle_vert, &vertexModule);
        createShaderModule(pLogicalDevice, depth_composite_frag, &fragmentModule);

        // Create render pass
        renderPass = createRenderPass(pLogicalDevice, format);

        // Create pipeline layout
        std::vector<VkDescriptorSetLayout> descriptorSetLayouts = {imageSamplerDescriptorSetLayout};
        pipelineLayout = createGraphicsPipelineLayout(pLogicalDevice, descriptorSetLayouts);

        // Get threshold from config
        float depthThreshold = pConfig->getOption<float>("depthMaskThreshold", 0.9999f);

        // Set up specialization constant for threshold
        VkSpecializationMapEntry thresholdMapEntry;
        thresholdMapEntry.constantID = 0;
        thresholdMapEntry.offset     = 0;
        thresholdMapEntry.size       = sizeof(float);

        VkSpecializationInfo fragmentSpecializationInfo;
        fragmentSpecializationInfo.mapEntryCount = 1;
        fragmentSpecializationInfo.pMapEntries   = &thresholdMapEntry;
        fragmentSpecializationInfo.dataSize      = sizeof(float);
        fragmentSpecializationInfo.pData         = &depthThreshold;

        // Create graphics pipeline
        graphicsPipeline = createGraphicsPipeline(pLogicalDevice,
                                                  vertexModule,
                                                  nullptr,
                                                  "main",
                                                  fragmentModule,
                                                  &fragmentSpecializationInfo,
                                                  "main",
                                                  imageExtent,
                                                  renderPass,
                                                  pipelineLayout);

        // Allocate descriptor sets (one per swapchain image)
        std::vector<VkDescriptorSetLayout> layouts(originalImages.size(), imageSamplerDescriptorSetLayout);
        VkDescriptorSetAllocateInfo descriptorSetAllocateInfo;
        descriptorSetAllocateInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descriptorSetAllocateInfo.pNext              = nullptr;
        descriptorSetAllocateInfo.descriptorPool     = descriptorPool;
        descriptorSetAllocateInfo.descriptorSetCount = layouts.size();
        descriptorSetAllocateInfo.pSetLayouts        = layouts.data();

        imageDescriptorSets.resize(originalImages.size());
        VkResult result = pLogicalDevice->vkd.AllocateDescriptorSets(
            pLogicalDevice->device, &descriptorSetAllocateInfo, imageDescriptorSets.data());
        ASSERT_VULKAN(result);

        // Create framebuffers
        framebuffers = createFramebuffers(pLogicalDevice, renderPass, imageExtent, {outputImageViews});

        Logger::debug("DepthCompositeEffect created successfully");
    }

    void DepthCompositeEffect::useDepthImage(VkImageView newDepthImageView)
    {
        if (depthImageView == newDepthImageView)
            return;

        depthImageView = newDepthImageView;
        descriptorSetsNeedUpdate = true;
        Logger::debug("DepthCompositeEffect: depth image view updated");
    }

    void DepthCompositeEffect::applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer)
    {
        // If no depth image, skip compositing
        if (depthImageView == VK_NULL_HANDLE)
        {
            Logger::warn("DepthCompositeEffect: no depth image available, skipping");
            return;
        }

        // Update descriptor sets if needed (when depth image changes)
        if (descriptorSetsNeedUpdate)
        {
            Logger::debug("DepthCompositeEffect: updating descriptor sets");

            for (size_t i = 0; i < imageDescriptorSets.size(); i++)
            {
                VkDescriptorImageInfo originalInfo = {};
                originalInfo.sampler     = sampler;
                originalInfo.imageView   = originalImageViews[i];
                originalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                VkDescriptorImageInfo effectedInfo = {};
                effectedInfo.sampler     = sampler;
                effectedInfo.imageView   = effectedImageViews[i];
                effectedInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                VkDescriptorImageInfo depthInfo = {};
                depthInfo.sampler     = sampler;
                depthInfo.imageView   = depthImageView;
                depthInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                std::vector<VkWriteDescriptorSet> writes(3);

                writes[0] = {};
                writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[0].dstSet          = imageDescriptorSets[i];
                writes[0].dstBinding      = 0;
                writes[0].descriptorCount = 1;
                writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[0].pImageInfo      = &originalInfo;

                writes[1] = {};
                writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[1].dstSet          = imageDescriptorSets[i];
                writes[1].dstBinding      = 1;
                writes[1].descriptorCount = 1;
                writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[1].pImageInfo      = &effectedInfo;

                writes[2] = {};
                writes[2].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[2].dstSet          = imageDescriptorSets[i];
                writes[2].dstBinding      = 2;
                writes[2].descriptorCount = 1;
                writes[2].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[2].pImageInfo      = &depthInfo;

                pLogicalDevice->vkd.UpdateDescriptorSets(
                    pLogicalDevice->device, writes.size(), writes.data(), 0, nullptr);
            }

            descriptorSetsNeedUpdate = false;
        }

        Logger::debug("applying DepthCompositeEffect to cb " + convertToString(commandBuffer));

        // Barrier for original image (transition to shader read)
        VkImageMemoryBarrier originalBarrier = {};
        originalBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        originalBarrier.srcAccessMask       = VK_ACCESS_MEMORY_WRITE_BIT;
        originalBarrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        originalBarrier.oldLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        originalBarrier.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        originalBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        originalBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        originalBarrier.image               = originalImages[imageIndex];
        originalBarrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        // Barrier for effected image (transition to shader read)
        VkImageMemoryBarrier effectedBarrier = {};
        effectedBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        effectedBarrier.srcAccessMask       = VK_ACCESS_MEMORY_WRITE_BIT;
        effectedBarrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        effectedBarrier.oldLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        effectedBarrier.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        effectedBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        effectedBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        effectedBarrier.image               = effectedImages[imageIndex];
        effectedBarrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        std::vector<VkImageMemoryBarrier> barriers = {originalBarrier, effectedBarrier};

        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr,
            barriers.size(), barriers.data());

        // Begin render pass
        VkRenderPassBeginInfo renderPassBeginInfo = {};
        renderPassBeginInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderPass        = renderPass;
        renderPassBeginInfo.framebuffer       = framebuffers[imageIndex];
        renderPassBeginInfo.renderArea.offset = {0, 0};
        renderPassBeginInfo.renderArea.extent = imageExtent;
        VkClearValue clearValue               = {0.0f, 0.0f, 0.0f, 1.0f};
        renderPassBeginInfo.clearValueCount   = 1;
        renderPassBeginInfo.pClearValues      = &clearValue;

        pLogicalDevice->vkd.CmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        pLogicalDevice->vkd.CmdBindDescriptorSets(
            commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
            &imageDescriptorSets[imageIndex], 0, nullptr);

        pLogicalDevice->vkd.CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

        pLogicalDevice->vkd.CmdDraw(commandBuffer, 3, 1, 0, 0);

        pLogicalDevice->vkd.CmdEndRenderPass(commandBuffer);

        // Reverse barriers
        originalBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        originalBarrier.dstAccessMask = 0;
        originalBarrier.oldLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        originalBarrier.newLayout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        effectedBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        effectedBarrier.dstAccessMask = 0;
        effectedBarrier.oldLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        effectedBarrier.newLayout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        barriers = {originalBarrier, effectedBarrier};

        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr,
            barriers.size(), barriers.data());

        Logger::debug("DepthCompositeEffect applied");
    }

    DepthCompositeEffect::~DepthCompositeEffect()
    {
        Logger::debug("destroying DepthCompositeEffect");

        if (!pLogicalDevice)
            return;

        pLogicalDevice->vkd.DestroyPipeline(pLogicalDevice->device, graphicsPipeline, nullptr);
        pLogicalDevice->vkd.DestroyPipelineLayout(pLogicalDevice->device, pipelineLayout, nullptr);
        pLogicalDevice->vkd.DestroyRenderPass(pLogicalDevice->device, renderPass, nullptr);
        pLogicalDevice->vkd.DestroyDescriptorSetLayout(pLogicalDevice->device, imageSamplerDescriptorSetLayout, nullptr);
        pLogicalDevice->vkd.DestroyShaderModule(pLogicalDevice->device, vertexModule, nullptr);
        pLogicalDevice->vkd.DestroyShaderModule(pLogicalDevice->device, fragmentModule, nullptr);
        pLogicalDevice->vkd.DestroyDescriptorPool(pLogicalDevice->device, descriptorPool, nullptr);

        for (size_t i = 0; i < framebuffers.size(); i++)
        {
            pLogicalDevice->vkd.DestroyFramebuffer(pLogicalDevice->device, framebuffers[i], nullptr);
            pLogicalDevice->vkd.DestroyImageView(pLogicalDevice->device, originalImageViews[i], nullptr);
            pLogicalDevice->vkd.DestroyImageView(pLogicalDevice->device, effectedImageViews[i], nullptr);
            pLogicalDevice->vkd.DestroyImageView(pLogicalDevice->device, outputImageViews[i], nullptr);
        }

        pLogicalDevice->vkd.DestroySampler(pLogicalDevice->device, sampler, nullptr);

        Logger::debug("DepthCompositeEffect destroyed");
    }

} // namespace vkBasalt
