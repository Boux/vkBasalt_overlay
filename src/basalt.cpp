#include "vulkan_include.hpp"

#include <mutex>
#include <map>
#include <set>
#include <chrono>
#include <vector>
#include <unordered_map>
#include <iostream>
#include <string>
#include <memory>
#include <cstring>
#include <filesystem>
#include <algorithm>

#include "util.hpp"
#include "keyboard_input.hpp"

#include "logical_device.hpp"
#include "logical_swapchain.hpp"

#include "image_view.hpp"
#include "sampler.hpp"
#include "framebuffer.hpp"
#include "descriptor_set.hpp"
#include "shader.hpp"
#include "graphics_pipeline.hpp"
#include "command_buffer.hpp"
#include "buffer.hpp"
#include "config.hpp"
#include "config_serializer.hpp"
#include "fake_swapchain.hpp"
#include "renderpass.hpp"
#include "format.hpp"
#include "logger.hpp"

#include "effect.hpp"
#include "effect_fxaa.hpp"
#include "effect_cas.hpp"
#include "effect_dls.hpp"
#include "effect_smaa.hpp"
#include "effect_deband.hpp"
#include "effect_lut.hpp"
#include "effect_reshade.hpp"
#include "effect_transfer.hpp"
#include "imgui_overlay.hpp"
#include "effect_params.hpp"

#define VKBASALT_NAME "VK_LAYER_VKBASALT_post_processing"

#if defined(__GNUC__) && __GNUC__ >= 4
#define VK_BASALT_EXPORT __attribute__((visibility("default")))
#else
#error "Unsupported platform!"
#endif

namespace vkBasalt
{
    std::shared_ptr<Config> pBaseConfig = nullptr;  // Always vkBasalt.conf
    std::shared_ptr<Config> pConfig = nullptr;      // Current config (base + overlay)

    Logger Logger::s_instance;

    // layer book-keeping information, to store dispatch tables by key
    std::unordered_map<void*, InstanceDispatch>                           instanceDispatchMap;
    std::unordered_map<void*, VkInstance>                                 instanceMap;
    std::unordered_map<void*, uint32_t>                                   instanceVersionMap;
    std::unordered_map<void*, std::shared_ptr<LogicalDevice>>             deviceMap;
    std::unordered_map<VkSwapchainKHR, std::shared_ptr<LogicalSwapchain>> swapchainMap;

    std::mutex globalLock;
#ifdef _GCC_
    using scoped_lock __attribute__((unused)) = std::lock_guard<std::mutex>;
#else
    using scoped_lock = std::lock_guard<std::mutex>;
#endif

    template<typename DispatchableType>
    void* GetKey(DispatchableType inst)
    {
        return *(void**) inst;
    }

    // Cached available effects data (to avoid re-parsing config every frame)
    struct CachedEffectsData
    {
        std::vector<std::string> currentConfigEffects;
        std::vector<std::string> defaultConfigEffects;
        std::map<std::string, std::string> effectPaths;
        std::string configPath;
        bool initialized = false;
    };
    CachedEffectsData cachedEffects;

    // Cached parameters (to avoid re-parsing config every frame)
    struct CachedParametersData
    {
        std::vector<EffectParameter> parameters;
        std::vector<std::string> effectNames;  // Effects when params were collected
        std::string configPath;
        bool dirty = true;  // Set to true to force recollection
    };
    CachedParametersData cachedParams;

    // Debounce for resize - delays effect reload until resize stops
    struct ResizeDebounceState
    {
        std::chrono::steady_clock::time_point lastResizeTime;
        bool pending = false;
    };
    ResizeDebounceState resizeDebounce;
    constexpr int64_t RESIZE_DEBOUNCE_MS = 200;

    // Initialize configs: base (vkBasalt.conf) + current (from env/default_config)
    void initConfigs()
    {
        if (pBaseConfig != nullptr)
            return;  // Already initialized

        // Load base config (vkBasalt.conf) - used for paths, effect definitions
        pBaseConfig = std::make_shared<Config>();

        // Determine current config path
        std::string currentConfigPath;

        // 1. Check env var
        const char* envConfig = std::getenv("VKBASALT_CONFIG_FILE");
        if (envConfig && *envConfig)
        {
            currentConfigPath = envConfig;
        }
        // 2. Check default_config file
        else
        {
            std::string defaultName = ConfigSerializer::getDefaultConfig();
            if (!defaultName.empty())
                currentConfigPath = ConfigSerializer::getConfigsDir() + "/" + defaultName + ".conf";
        }

        // Load current config if specified, otherwise use base
        if (!currentConfigPath.empty())
        {
            std::ifstream file(currentConfigPath);
            if (file.good())
            {
                pConfig = std::make_shared<Config>(currentConfigPath);
                pConfig->setFallback(pBaseConfig.get());
                Logger::info("current config: " + currentConfigPath);
            }
            else
            {
                pConfig = pBaseConfig;  // Fall back to base
            }
        }
        else
        {
            pConfig = pBaseConfig;  // No current config, use base
        }
    }

    // Switch to a new config (called from overlay)
    void switchConfig(const std::string& configPath)
    {
        pConfig = std::make_shared<Config>(configPath);
        pConfig->setFallback(pBaseConfig.get());
        cachedParams.dirty = true;
        Logger::info("switched to config: " + configPath);
    }

    // Helper function to get available effects separated by source (uses cache)
    void getAvailableEffects(Config* pConfig,
                             std::vector<std::string>& currentConfigEffects,
                             std::vector<std::string>& defaultConfigEffects,
                             std::map<std::string, std::string>& effectPaths)
    {
        // Use cache if available and config hasn't changed
        if (cachedEffects.initialized && cachedEffects.configPath == pConfig->getConfigFilePath())
        {
            currentConfigEffects = cachedEffects.currentConfigEffects;
            defaultConfigEffects = cachedEffects.defaultConfigEffects;
            effectPaths = cachedEffects.effectPaths;
            return;
        }

        currentConfigEffects.clear();
        defaultConfigEffects.clear();
        effectPaths.clear();

        // Collect all known effect names (to avoid duplicates)
        std::set<std::string> knownEffects;

        // Get effect definitions from current config
        auto configEffects = pConfig->getEffectDefinitions();
        for (const auto& [name, path] : configEffects)
        {
            currentConfigEffects.push_back(name);
            effectPaths[name] = path;
            knownEffects.insert(name);
        }

        // Also load effect definitions from the base config file (vkBasalt.conf)
        if (pBaseConfig && pBaseConfig->getConfigFilePath() != pConfig->getConfigFilePath())
        {
            auto defaultEffects = pBaseConfig->getEffectDefinitions();
            for (const auto& [name, path] : defaultEffects)
            {
                if (knownEffects.find(name) == knownEffects.end())
                {
                    defaultConfigEffects.push_back(name);
                    effectPaths[name] = path;
                    knownEffects.insert(name);
                }
            }
        }

        // Auto-discover .fx files in reshadeIncludePath
        std::string includePath = pBaseConfig ? pBaseConfig->getOption<std::string>("reshadeIncludePath", "")
                                              : pConfig->getOption<std::string>("reshadeIncludePath", "");
        if (!includePath.empty())
        {
            try
            {
                for (const auto& entry : std::filesystem::directory_iterator(includePath))
                {
                    if (!entry.is_regular_file())
                        continue;

                    std::string filename = entry.path().filename().string();
                    if (filename.size() < 4 || filename.substr(filename.size() - 3) != ".fx")
                        continue;

                    // Effect name is filename without .fx extension
                    std::string effectName = filename.substr(0, filename.size() - 3);

                    // Skip if already known (from config definitions)
                    if (knownEffects.find(effectName) != knownEffects.end())
                        continue;

                    defaultConfigEffects.push_back(effectName);
                    effectPaths[effectName] = entry.path().string();
                    knownEffects.insert(effectName);
                }

                // Sort discovered effects alphabetically
                std::sort(defaultConfigEffects.begin(), defaultConfigEffects.end());
            }
            catch (const std::filesystem::filesystem_error& e)
            {
                Logger::warn("failed to scan reshadeIncludePath: " + std::string(e.what()));
            }
        }

        // Update cache
        cachedEffects.currentConfigEffects = currentConfigEffects;
        cachedEffects.defaultConfigEffects = defaultConfigEffects;
        cachedEffects.effectPaths = effectPaths;
        cachedEffects.configPath = pConfig->getConfigFilePath();
        cachedEffects.initialized = true;
    }

    // Helper function to reload effects for a swapchain (for hot-reload)
    void reloadEffectsForSwapchain(LogicalSwapchain* pLogicalSwapchain, Config* pConfig,
                                   const std::map<std::string, bool>& effectEnabledStates = {},
                                   const std::vector<std::string>& activeEffects = {})
    {
        LogicalDevice* pLogicalDevice = pLogicalSwapchain->pLogicalDevice;

        // Wait for GPU to finish
        pLogicalDevice->vkd.QueueWaitIdle(pLogicalDevice->queue);

        // Free command buffers
        pLogicalDevice->vkd.FreeCommandBuffers(
            pLogicalDevice->device, pLogicalDevice->commandPool, pLogicalSwapchain->commandBuffersEffect.size(), pLogicalSwapchain->commandBuffersEffect.data());
        pLogicalDevice->vkd.FreeCommandBuffers(
            pLogicalDevice->device, pLogicalDevice->commandPool, pLogicalSwapchain->commandBuffersNoEffect.size(), pLogicalSwapchain->commandBuffersNoEffect.data());

        // Clear effects
        pLogicalSwapchain->effects.clear();
        pLogicalSwapchain->defaultTransfer.reset();

        // Use provided active effects list, or fall back to config
        std::vector<std::string> effectStrings = activeEffects.empty()
            ? pConfig->getOption<std::vector<std::string>>("effects", {"cas"})
            : activeEffects;

        // Check if we have enough fake images for the effects
        // Fake images are allocated at swapchain creation based on maxEffectSlots
        if (effectStrings.size() > pLogicalSwapchain->maxEffectSlots)
        {
            Logger::warn("Cannot add more effects than maxEffectSlots (" +
                        std::to_string(effectStrings.size()) + " > " + std::to_string(pLogicalSwapchain->maxEffectSlots) +
                        "). Increase maxEffects in config.");
            effectStrings.resize(pLogicalSwapchain->maxEffectSlots);
        }

        VkFormat unormFormat = convertToUNORM(pLogicalSwapchain->format);
        VkFormat srgbFormat  = convertToSRGB(pLogicalSwapchain->format);

        Logger::info("reloading " + std::to_string(effectStrings.size()) + " effects, fakeImages.size()=" +
                    std::to_string(pLogicalSwapchain->fakeImages.size()) + ", imageCount=" + std::to_string(pLogicalSwapchain->imageCount) +
                    ", maxEffectSlots=" + std::to_string(pLogicalSwapchain->maxEffectSlots));

        for (uint32_t i = 0; i < effectStrings.size(); i++)
        {
            Logger::info("reloading effect " + std::to_string(i) + ": " + effectStrings[i] +
                        ", firstImages start=" + std::to_string(pLogicalSwapchain->imageCount * i) +
                        ", end=" + std::to_string(pLogicalSwapchain->imageCount * (i + 1)));
            std::vector<VkImage> firstImages(pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount * i,
                                             pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount * (i + 1));
            std::vector<VkImage> secondImages;
            if (i == effectStrings.size() - 1)
            {
                secondImages = pLogicalDevice->supportsMutableFormat
                                   ? pLogicalSwapchain->images
                                   : std::vector<VkImage>(pLogicalSwapchain->fakeImages.end() - pLogicalSwapchain->imageCount,
                                                          pLogicalSwapchain->fakeImages.end());
            }
            else
            {
                secondImages = std::vector<VkImage>(pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount * (i + 1),
                                                    pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount * (i + 2));
            }

            // Check if effect is disabled - if so, use TransferEffect to pass through
            auto enabledIt = effectEnabledStates.find(effectStrings[i]);
            bool effectEnabled = (enabledIt == effectEnabledStates.end()) || enabledIt->second;
            if (!effectEnabled)
            {
                Logger::debug("effect disabled, using pass-through: " + effectStrings[i]);
                pLogicalSwapchain->effects.push_back(std::shared_ptr<Effect>(
                    new TransferEffect(pLogicalDevice, pLogicalSwapchain->format, pLogicalSwapchain->imageExtent, firstImages, secondImages, pConfig)));
                continue;
            }

            if (effectStrings[i] == std::string("fxaa"))
            {
                pLogicalSwapchain->effects.push_back(std::shared_ptr<Effect>(
                    new FxaaEffect(pLogicalDevice, srgbFormat, pLogicalSwapchain->imageExtent, firstImages, secondImages, pConfig)));
            }
            else if (effectStrings[i] == std::string("cas"))
            {
                pLogicalSwapchain->effects.push_back(std::shared_ptr<Effect>(
                    new CasEffect(pLogicalDevice, unormFormat, pLogicalSwapchain->imageExtent, firstImages, secondImages, pConfig)));
            }
            else if (effectStrings[i] == std::string("deband"))
            {
                pLogicalSwapchain->effects.push_back(std::shared_ptr<Effect>(
                    new DebandEffect(pLogicalDevice, unormFormat, pLogicalSwapchain->imageExtent, firstImages, secondImages, pConfig)));
            }
            else if (effectStrings[i] == std::string("smaa"))
            {
                pLogicalSwapchain->effects.push_back(std::shared_ptr<Effect>(
                    new SmaaEffect(pLogicalDevice, unormFormat, pLogicalSwapchain->imageExtent, firstImages, secondImages, pConfig)));
            }
            else if (effectStrings[i] == std::string("lut"))
            {
                pLogicalSwapchain->effects.push_back(std::shared_ptr<Effect>(
                    new LutEffect(pLogicalDevice, unormFormat, pLogicalSwapchain->imageExtent, firstImages, secondImages, pConfig)));
            }
            else if (effectStrings[i] == std::string("dls"))
            {
                pLogicalSwapchain->effects.push_back(std::shared_ptr<Effect>(
                    new DlsEffect(pLogicalDevice, unormFormat, pLogicalSwapchain->imageExtent, firstImages, secondImages, pConfig)));
            }
            else
            {
                pLogicalSwapchain->effects.push_back(std::shared_ptr<Effect>(new ReshadeEffect(pLogicalDevice,
                                                                                               pLogicalSwapchain->format,
                                                                                               pLogicalSwapchain->imageExtent,
                                                                                               firstImages,
                                                                                               secondImages,
                                                                                               pConfig,
                                                                                               effectStrings[i])));
            }
        }

        if (!pLogicalDevice->supportsMutableFormat)
        {
            pLogicalSwapchain->effects.push_back(std::shared_ptr<Effect>(new TransferEffect(
                pLogicalDevice,
                pLogicalSwapchain->format,
                pLogicalSwapchain->imageExtent,
                std::vector<VkImage>(pLogicalSwapchain->fakeImages.end() - pLogicalSwapchain->imageCount, pLogicalSwapchain->fakeImages.end()),
                pLogicalSwapchain->images,
                pConfig)));
        }

        VkImageView depthImageView = pLogicalDevice->depthImageViews.size() ? pLogicalDevice->depthImageViews[0] : VK_NULL_HANDLE;
        VkImage     depthImage     = pLogicalDevice->depthImageViews.size() ? pLogicalDevice->depthImages[0] : VK_NULL_HANDLE;
        VkFormat    depthFormat    = pLogicalDevice->depthImageViews.size() ? pLogicalDevice->depthFormats[0] : VK_FORMAT_UNDEFINED;

        // Allocate and write new command buffers
        pLogicalSwapchain->commandBuffersEffect = allocateCommandBuffer(pLogicalDevice, pLogicalSwapchain->imageCount);
        writeCommandBuffers(
            pLogicalDevice, pLogicalSwapchain->effects, depthImage, depthImageView, depthFormat, pLogicalSwapchain->commandBuffersEffect);

        pLogicalSwapchain->defaultTransfer = std::shared_ptr<Effect>(new TransferEffect(
            pLogicalDevice,
            pLogicalSwapchain->format,
            pLogicalSwapchain->imageExtent,
            std::vector<VkImage>(pLogicalSwapchain->fakeImages.begin(), pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount),
            pLogicalSwapchain->images,
            pConfig));

        pLogicalSwapchain->commandBuffersNoEffect = allocateCommandBuffer(pLogicalDevice, pLogicalSwapchain->imageCount);
        writeCommandBuffers(pLogicalDevice,
                            {pLogicalSwapchain->defaultTransfer},
                            VK_NULL_HANDLE,
                            VK_NULL_HANDLE,
                            VK_FORMAT_UNDEFINED,
                            pLogicalSwapchain->commandBuffersNoEffect);

        Logger::info("effects reloaded successfully");
    }

    VkResult VKAPI_CALL vkBasalt_CreateInstance(const VkInstanceCreateInfo*  pCreateInfo,
                                                const VkAllocationCallbacks* pAllocator,
                                                VkInstance*                  pInstance)
    {
        VkLayerInstanceCreateInfo* layerCreateInfo = (VkLayerInstanceCreateInfo*) pCreateInfo->pNext;

        // step through the chain of pNext until we get to the link info
        while (layerCreateInfo
               && (layerCreateInfo->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO || layerCreateInfo->function != VK_LAYER_LINK_INFO))
        {
            layerCreateInfo = (VkLayerInstanceCreateInfo*) layerCreateInfo->pNext;
        }

        Logger::trace("vkCreateInstance");

        if (layerCreateInfo == nullptr)
        {
            // No loader instance create info
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        PFN_vkGetInstanceProcAddr gpa = layerCreateInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
        // move chain on for next layer
        layerCreateInfo->u.pLayerInfo = layerCreateInfo->u.pLayerInfo->pNext;

        PFN_vkCreateInstance createFunc = (PFN_vkCreateInstance) gpa(VK_NULL_HANDLE, "vkCreateInstance");

        VkInstanceCreateInfo modifiedCreateInfo = *pCreateInfo;
        VkApplicationInfo    appInfo;
        if (modifiedCreateInfo.pApplicationInfo)
        {
            appInfo = *(modifiedCreateInfo.pApplicationInfo);
            if (appInfo.apiVersion < VK_API_VERSION_1_1)
            {
                appInfo.apiVersion = VK_API_VERSION_1_1;
            }
        }
        else
        {
            appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
            appInfo.pNext              = nullptr;
            appInfo.pApplicationName   = nullptr;
            appInfo.applicationVersion = 0;
            appInfo.pEngineName        = nullptr;
            appInfo.engineVersion      = 0;
            appInfo.apiVersion         = VK_API_VERSION_1_1;
        }

        modifiedCreateInfo.pApplicationInfo = &appInfo;
        VkResult ret                        = createFunc(&modifiedCreateInfo, pAllocator, pInstance);

        // fetch our own dispatch table for the functions we need, into the next layer
        InstanceDispatch dispatchTable;
        fillDispatchTableInstance(*pInstance, gpa, &dispatchTable);

        // store the table by key
        {
            scoped_lock l(globalLock);
            instanceDispatchMap[GetKey(*pInstance)] = dispatchTable;
            instanceMap[GetKey(*pInstance)]         = *pInstance;
            instanceVersionMap[GetKey(*pInstance)]  = modifiedCreateInfo.pApplicationInfo->apiVersion;
        }

        return ret;
    }

    void VKAPI_CALL vkBasalt_DestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator)
    {
        if (!instance)
            return;

        scoped_lock l(globalLock);

        Logger::trace("vkDestroyInstance");

        InstanceDispatch dispatchTable = instanceDispatchMap[GetKey(instance)];

        dispatchTable.DestroyInstance(instance, pAllocator);

        instanceDispatchMap.erase(GetKey(instance));
        instanceMap.erase(GetKey(instance));
        instanceVersionMap.erase(GetKey(instance));
    }

    VkResult VKAPI_CALL vkBasalt_CreateDevice(VkPhysicalDevice             physicalDevice,
                                              const VkDeviceCreateInfo*    pCreateInfo,
                                              const VkAllocationCallbacks* pAllocator,
                                              VkDevice*                    pDevice)
    {
        scoped_lock l(globalLock);
        Logger::trace("vkCreateDevice");
        VkLayerDeviceCreateInfo* layerCreateInfo = (VkLayerDeviceCreateInfo*) pCreateInfo->pNext;

        // step through the chain of pNext until we get to the link info
        while (layerCreateInfo
               && (layerCreateInfo->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO || layerCreateInfo->function != VK_LAYER_LINK_INFO))
        {
            layerCreateInfo = (VkLayerDeviceCreateInfo*) layerCreateInfo->pNext;
        }

        if (layerCreateInfo == nullptr)
        {
            // No loader instance create info
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        PFN_vkGetInstanceProcAddr gipa = layerCreateInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
        PFN_vkGetDeviceProcAddr   gdpa = layerCreateInfo->u.pLayerInfo->pfnNextGetDeviceProcAddr;
        // move chain on for next layer
        layerCreateInfo->u.pLayerInfo = layerCreateInfo->u.pLayerInfo->pNext;

        PFN_vkCreateDevice createFunc = (PFN_vkCreateDevice) gipa(VK_NULL_HANDLE, "vkCreateDevice");

        // check and activate extentions
        uint32_t extensionCount = 0;

        instanceDispatchMap[GetKey(physicalDevice)].EnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> extensionProperties(extensionCount);
        instanceDispatchMap[GetKey(physicalDevice)].EnumerateDeviceExtensionProperties(
            physicalDevice, nullptr, &extensionCount, extensionProperties.data());

        bool supportsMutableFormat = false;
        for (VkExtensionProperties properties : extensionProperties)
        {
            if (properties.extensionName == std::string("VK_KHR_swapchain_mutable_format"))
            {
                Logger::debug("device supports VK_KHR_swapchain_mutable_format");
                supportsMutableFormat = true;
                break;
            }
        }

        VkPhysicalDeviceProperties deviceProps;
        instanceDispatchMap[GetKey(physicalDevice)].GetPhysicalDeviceProperties(physicalDevice, &deviceProps);

        VkDeviceCreateInfo       modifiedCreateInfo = *pCreateInfo;
        std::vector<const char*> enabledExtensionNames;
        if (modifiedCreateInfo.enabledExtensionCount)
        {
            enabledExtensionNames = std::vector<const char*>(modifiedCreateInfo.ppEnabledExtensionNames,
                                                             modifiedCreateInfo.ppEnabledExtensionNames + modifiedCreateInfo.enabledExtensionCount);
        }

        if (supportsMutableFormat)
        {
            Logger::debug("activating mutable_format");
            addUniqueCString(enabledExtensionNames, "VK_KHR_swapchain_mutable_format");
        }
        if (deviceProps.apiVersion < VK_API_VERSION_1_2 || instanceVersionMap[GetKey(physicalDevice)] < VK_API_VERSION_1_2)
        {
            addUniqueCString(enabledExtensionNames, "VK_KHR_image_format_list");
        }
        modifiedCreateInfo.ppEnabledExtensionNames = enabledExtensionNames.data();
        modifiedCreateInfo.enabledExtensionCount   = enabledExtensionNames.size();

        // Active needed Features
        VkPhysicalDeviceFeatures deviceFeatures = {};
        if (modifiedCreateInfo.pEnabledFeatures)
        {
            deviceFeatures = *(modifiedCreateInfo.pEnabledFeatures);
        }
        deviceFeatures.shaderImageGatherExtended = VK_TRUE;
        modifiedCreateInfo.pEnabledFeatures      = &deviceFeatures;

        VkResult ret = createFunc(physicalDevice, &modifiedCreateInfo, pAllocator, pDevice);

        if (ret != VK_SUCCESS)
            return ret;

        std::shared_ptr<LogicalDevice> pLogicalDevice(new LogicalDevice());
        pLogicalDevice->vki                   = instanceDispatchMap[GetKey(physicalDevice)];
        pLogicalDevice->device                = *pDevice;
        pLogicalDevice->physicalDevice        = physicalDevice;
        pLogicalDevice->instance              = instanceMap[GetKey(physicalDevice)];
        pLogicalDevice->queue                 = VK_NULL_HANDLE;
        pLogicalDevice->queueFamilyIndex      = 0;
        pLogicalDevice->commandPool           = VK_NULL_HANDLE;
        pLogicalDevice->supportsMutableFormat = supportsMutableFormat;

        fillDispatchTableDevice(*pDevice, gdpa, &pLogicalDevice->vkd);

        uint32_t count;

        pLogicalDevice->vki.GetPhysicalDeviceQueueFamilyProperties(pLogicalDevice->physicalDevice, &count, nullptr);

        std::vector<VkQueueFamilyProperties> queueProperties(count);

        pLogicalDevice->vki.GetPhysicalDeviceQueueFamilyProperties(pLogicalDevice->physicalDevice, &count, queueProperties.data());
        for (uint32_t i = 0; i < pCreateInfo->queueCreateInfoCount; i++)
        {
            auto& queueInfo = pCreateInfo->pQueueCreateInfos[i];
            if ((queueProperties[queueInfo.queueFamilyIndex].queueFlags & VK_QUEUE_GRAPHICS_BIT))
            {
                pLogicalDevice->vkd.GetDeviceQueue(pLogicalDevice->device, queueInfo.queueFamilyIndex, 0, &pLogicalDevice->queue);

                VkCommandPoolCreateInfo commandPoolCreateInfo;
                commandPoolCreateInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
                commandPoolCreateInfo.pNext            = nullptr;
                commandPoolCreateInfo.flags            = 0;
                commandPoolCreateInfo.queueFamilyIndex = queueInfo.queueFamilyIndex;

                Logger::debug("Found graphics capable queue");
                pLogicalDevice->vkd.CreateCommandPool(pLogicalDevice->device, &commandPoolCreateInfo, nullptr, &pLogicalDevice->commandPool);
                pLogicalDevice->queueFamilyIndex = queueInfo.queueFamilyIndex;

                initializeDispatchTable(pLogicalDevice->queue, pLogicalDevice->device);

                break;
            }
        }

        if (!pLogicalDevice->queue)
            Logger::err("Did not find a graphics queue!");

        deviceMap[GetKey(*pDevice)] = pLogicalDevice;

        return VK_SUCCESS;
    }

    void VKAPI_CALL vkBasalt_DestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator)
    {
        if (!device)
            return;

        scoped_lock l(globalLock);

        Logger::trace("vkDestroyDevice");

        LogicalDevice* pLogicalDevice = deviceMap[GetKey(device)].get();

        // Destroy ImGui overlay before device (it uses device resources)
        pLogicalDevice->imguiOverlay.reset();

        if (pLogicalDevice->commandPool != VK_NULL_HANDLE)
        {
            Logger::debug("DestroyCommandPool");
            pLogicalDevice->vkd.DestroyCommandPool(device, pLogicalDevice->commandPool, pAllocator);
        }

        pLogicalDevice->vkd.DestroyDevice(device, pAllocator);

        deviceMap.erase(GetKey(device));
    }

    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_CreateSwapchainKHR(VkDevice                        device,
                                                               const VkSwapchainCreateInfoKHR* pCreateInfo,
                                                               const VkAllocationCallbacks*    pAllocator,
                                                               VkSwapchainKHR*                 pSwapchain)
    {
        scoped_lock l(globalLock);

        Logger::trace("vkCreateSwapchainKHR");

        LogicalDevice* pLogicalDevice = deviceMap[GetKey(device)].get();

        VkSwapchainCreateInfoKHR modifiedCreateInfo = *pCreateInfo;

        VkFormat format = modifiedCreateInfo.imageFormat;

        VkFormat srgbFormat  = isSRGB(format) ? format : convertToSRGB(format);
        VkFormat unormFormat = isSRGB(format) ? convertToUNORM(format) : format;
        Logger::debug(std::to_string(srgbFormat) + " " + std::to_string(unormFormat));

        VkFormat formats[] = {unormFormat, srgbFormat};

        VkImageFormatListCreateInfoKHR imageFormatListCreateInfo;
        if (pLogicalDevice->supportsMutableFormat)
        {
            modifiedCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                                            | VK_IMAGE_USAGE_SAMPLED_BIT; // we want to use the swapchain images as output of the graphics pipeline
            modifiedCreateInfo.flags |= VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR;
            // TODO what if the application already uses multiple formats for the swapchain?

            imageFormatListCreateInfo.sType           = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO_KHR;
            imageFormatListCreateInfo.pNext           = modifiedCreateInfo.pNext;
            imageFormatListCreateInfo.viewFormatCount = (srgbFormat == unormFormat) ? 1 : 2;
            imageFormatListCreateInfo.pViewFormats    = formats;

            modifiedCreateInfo.pNext = &imageFormatListCreateInfo;
        }

        modifiedCreateInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

        Logger::debug("format " + std::to_string(modifiedCreateInfo.imageFormat));
        std::shared_ptr<LogicalSwapchain> pLogicalSwapchain(new LogicalSwapchain());
        pLogicalSwapchain->pLogicalDevice      = pLogicalDevice;
        pLogicalSwapchain->swapchainCreateInfo = *pCreateInfo;
        pLogicalSwapchain->imageExtent         = modifiedCreateInfo.imageExtent;
        pLogicalSwapchain->format              = modifiedCreateInfo.imageFormat;
        pLogicalSwapchain->imageCount          = 0;

        VkResult result = pLogicalDevice->vkd.CreateSwapchainKHR(device, &modifiedCreateInfo, pAllocator, pSwapchain);

        swapchainMap[*pSwapchain] = pLogicalSwapchain;

        return result;
    }

    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_GetSwapchainImagesKHR(VkDevice       device,
                                                                  VkSwapchainKHR swapchain,
                                                                  uint32_t*      pCount,
                                                                  VkImage*       pSwapchainImages)
    {
        scoped_lock l(globalLock);
        Logger::trace("vkGetSwapchainImagesKHR " + std::to_string(*pCount));

        LogicalDevice* pLogicalDevice = deviceMap[GetKey(device)].get();

        if (pSwapchainImages == nullptr)
        {
            return pLogicalDevice->vkd.GetSwapchainImagesKHR(device, swapchain, pCount, pSwapchainImages);
        }

        LogicalSwapchain* pLogicalSwapchain = swapchainMap[swapchain].get();

        // If the images got already requested once, return them again instead of creating new images
        if (pLogicalSwapchain->fakeImages.size())
        {
            *pCount = std::min<uint32_t>(*pCount, pLogicalSwapchain->imageCount);
            std::memcpy(pSwapchainImages, pLogicalSwapchain->fakeImages.data(), sizeof(VkImage) * (*pCount));
            return *pCount < pLogicalSwapchain->imageCount ? VK_INCOMPLETE : VK_SUCCESS;
        }

        pLogicalDevice->vkd.GetSwapchainImagesKHR(device, swapchain, &pLogicalSwapchain->imageCount, nullptr);
        pLogicalSwapchain->images.resize(pLogicalSwapchain->imageCount);
        pLogicalDevice->vkd.GetSwapchainImagesKHR(device, swapchain, &pLogicalSwapchain->imageCount, pLogicalSwapchain->images.data());

        // Create image views for overlay rendering
        pLogicalSwapchain->imageViews.resize(pLogicalSwapchain->imageCount);
        for (uint32_t i = 0; i < pLogicalSwapchain->imageCount; i++)
        {
            VkImageViewCreateInfo viewInfo = {};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = pLogicalSwapchain->images[i];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = pLogicalSwapchain->format;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;
            pLogicalDevice->vkd.CreateImageView(pLogicalDevice->device, &viewInfo, nullptr, &pLogicalSwapchain->imageViews[i]);
        }

        std::vector<std::string> effectStrings = pConfig->getOption<std::vector<std::string>>("effects", {"cas"});

        // Allow dynamic effect loading by allocating for more effects than configured
        // maxEffects defaults to 10, allowing users to enable additional effects at runtime
        int32_t maxEffects = pConfig->getOption<int32_t>("maxEffects", 10);
        size_t effectSlots = std::max(effectStrings.size(), (size_t)maxEffects);
        pLogicalSwapchain->maxEffectSlots = effectSlots;

        // create 1 more set of images when we can't use the swapchain it self
        uint32_t fakeImageCount = pLogicalSwapchain->imageCount * (effectSlots + !pLogicalDevice->supportsMutableFormat);

        pLogicalSwapchain->fakeImages =
            createFakeSwapchainImages(pLogicalDevice, pLogicalSwapchain->swapchainCreateInfo, fakeImageCount, pLogicalSwapchain->fakeImageMemory);
        Logger::debug("created fake swapchain images");

        // If there's persisted state, skip expensive effect creation - use pass-through and debounce
        bool hasPersisted = pLogicalDevice->overlayPersistentState &&
                            pLogicalDevice->overlayPersistentState->initialized &&
                            !pLogicalDevice->overlayPersistentState->selectedEffects.empty();

        if (hasPersisted)
        {
            Logger::debug("using pass-through during resize, will restore effects after debounce");
            // Create simple pass-through: first fake images -> swapchain images
            std::vector<VkImage> firstImages(pLogicalSwapchain->fakeImages.begin(),
                                             pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount);
            pLogicalSwapchain->effects.push_back(std::shared_ptr<Effect>(new TransferEffect(
                pLogicalDevice, pLogicalSwapchain->format, pLogicalSwapchain->imageExtent,
                firstImages, pLogicalSwapchain->images, pConfig.get())));

            resizeDebounce.pending = true;
            resizeDebounce.lastResizeTime = std::chrono::steady_clock::now();
        }
        else
        {
        // Normal effect creation from config
        VkFormat unormFormat = convertToUNORM(pLogicalSwapchain->format);
        VkFormat srgbFormat  = convertToSRGB(pLogicalSwapchain->format);

        for (uint32_t i = 0; i < effectStrings.size(); i++)
        {
            Logger::debug("current effectString " + effectStrings[i]);
            std::vector<VkImage> firstImages(pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount * i,
                                             pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount * (i + 1));
            Logger::debug(std::to_string(firstImages.size()) + " images in firstImages");
            std::vector<VkImage> secondImages;
            if (i == effectStrings.size() - 1)
            {
                secondImages = pLogicalDevice->supportsMutableFormat
                                   ? pLogicalSwapchain->images
                                   : std::vector<VkImage>(pLogicalSwapchain->fakeImages.end() - pLogicalSwapchain->imageCount,
                                                          pLogicalSwapchain->fakeImages.end());
                Logger::debug("using swapchain images as second images");
            }
            else
            {
                secondImages = std::vector<VkImage>(pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount * (i + 1),
                                                    pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount * (i + 2));
                Logger::debug("not using swapchain images as second images");
            }
            Logger::debug(std::to_string(secondImages.size()) + " images in secondImages");
            if (effectStrings[i] == std::string("fxaa"))
            {
                pLogicalSwapchain->effects.push_back(std::shared_ptr<Effect>(
                    new FxaaEffect(pLogicalDevice, srgbFormat, pLogicalSwapchain->imageExtent, firstImages, secondImages, pConfig.get())));
                Logger::debug("created FxaaEffect");
            }
            else if (effectStrings[i] == std::string("cas"))
            {
                pLogicalSwapchain->effects.push_back(std::shared_ptr<Effect>(
                    new CasEffect(pLogicalDevice, unormFormat, pLogicalSwapchain->imageExtent, firstImages, secondImages, pConfig.get())));
                Logger::debug("created CasEffect");
            }
            else if (effectStrings[i] == std::string("deband"))
            {
                pLogicalSwapchain->effects.push_back(std::shared_ptr<Effect>(
                    new DebandEffect(pLogicalDevice, unormFormat, pLogicalSwapchain->imageExtent, firstImages, secondImages, pConfig.get())));
                Logger::debug("created DebandEffect");
            }
            else if (effectStrings[i] == std::string("smaa"))
            {
                pLogicalSwapchain->effects.push_back(std::shared_ptr<Effect>(
                    new SmaaEffect(pLogicalDevice, unormFormat, pLogicalSwapchain->imageExtent, firstImages, secondImages, pConfig.get())));
                Logger::debug("created SmaaEffect");
            }
            else if (effectStrings[i] == std::string("lut"))
            {
                pLogicalSwapchain->effects.push_back(std::shared_ptr<Effect>(
                    new LutEffect(pLogicalDevice, unormFormat, pLogicalSwapchain->imageExtent, firstImages, secondImages, pConfig.get())));
                Logger::debug("created LutEffect");
            }
            else if (effectStrings[i] == std::string("dls"))
            {
                pLogicalSwapchain->effects.push_back(std::shared_ptr<Effect>(
                    new DlsEffect(pLogicalDevice, unormFormat, pLogicalSwapchain->imageExtent, firstImages, secondImages, pConfig.get())));
                Logger::debug("created DlsEffect");
            }
            else
            {
                pLogicalSwapchain->effects.push_back(std::shared_ptr<Effect>(new ReshadeEffect(pLogicalDevice,
                                                                                               pLogicalSwapchain->format,
                                                                                               pLogicalSwapchain->imageExtent,
                                                                                               firstImages,
                                                                                               secondImages,
                                                                                               pConfig.get(),
                                                                                               effectStrings[i])));
                Logger::debug("created ReshadeEffect");
            }
        }

        if (!pLogicalDevice->supportsMutableFormat)
        {
            pLogicalSwapchain->effects.push_back(std::shared_ptr<Effect>(new TransferEffect(
                pLogicalDevice,
                pLogicalSwapchain->format,
                pLogicalSwapchain->imageExtent,
                std::vector<VkImage>(pLogicalSwapchain->fakeImages.end() - pLogicalSwapchain->imageCount, pLogicalSwapchain->fakeImages.end()),
                pLogicalSwapchain->images,
                pConfig.get())));
        }
        } // end else (normal effect creation)

        VkImageView depthImageView = pLogicalDevice->depthImageViews.size() ? pLogicalDevice->depthImageViews[0] : VK_NULL_HANDLE;
        VkImage     depthImage     = pLogicalDevice->depthImageViews.size() ? pLogicalDevice->depthImages[0] : VK_NULL_HANDLE;
        VkFormat    depthFormat    = pLogicalDevice->depthImageViews.size() ? pLogicalDevice->depthFormats[0] : VK_FORMAT_UNDEFINED;

        Logger::debug("effect string count: " + std::to_string(effectStrings.size()));
        Logger::debug("effect count: " + std::to_string(pLogicalSwapchain->effects.size()));

        pLogicalSwapchain->commandBuffersEffect = allocateCommandBuffer(pLogicalDevice, pLogicalSwapchain->imageCount);
        Logger::debug("allocated ComandBuffers " + std::to_string(pLogicalSwapchain->commandBuffersEffect.size()) + " for swapchain "
                      + convertToString(swapchain));

        writeCommandBuffers(
            pLogicalDevice, pLogicalSwapchain->effects, depthImage, depthImageView, depthFormat, pLogicalSwapchain->commandBuffersEffect);
        Logger::debug("wrote CommandBuffers");

        pLogicalSwapchain->semaphores = createSemaphores(pLogicalDevice, pLogicalSwapchain->imageCount);
        pLogicalSwapchain->overlaySemaphores = createSemaphores(pLogicalDevice, pLogicalSwapchain->imageCount);
        Logger::debug("created semaphores");
        for (unsigned int i = 0; i < pLogicalSwapchain->imageCount; i++)
        {
            Logger::debug(std::to_string(i) + " written commandbuffer " + convertToString(pLogicalSwapchain->commandBuffersEffect[i]));
        }
        Logger::trace("vkGetSwapchainImagesKHR");

        pLogicalSwapchain->defaultTransfer = std::shared_ptr<Effect>(new TransferEffect(
            pLogicalDevice,
            pLogicalSwapchain->format,
            pLogicalSwapchain->imageExtent,
            std::vector<VkImage>(pLogicalSwapchain->fakeImages.begin(), pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount),
            pLogicalSwapchain->images,
            pConfig.get()));

        pLogicalSwapchain->commandBuffersNoEffect = allocateCommandBuffer(pLogicalDevice, pLogicalSwapchain->imageCount);

        writeCommandBuffers(pLogicalDevice,
                            {pLogicalSwapchain->defaultTransfer},
                            VK_NULL_HANDLE,
                            VK_NULL_HANDLE,
                            VK_FORMAT_UNDEFINED,
                            pLogicalSwapchain->commandBuffersNoEffect);

        for (unsigned int i = 0; i < pLogicalSwapchain->imageCount; i++)
        {
            Logger::debug(std::to_string(i) + " written commandbuffer " + convertToString(pLogicalSwapchain->commandBuffersNoEffect[i]));
        }

        // Create ImGui overlay at device level (if not already created)
        // This survives swapchain recreation during resize
        if (!pLogicalDevice->imguiOverlay)
        {
            if (!pLogicalDevice->overlayPersistentState)
                pLogicalDevice->overlayPersistentState = std::make_unique<OverlayPersistentState>();
            pLogicalDevice->imguiOverlay = std::make_unique<ImGuiOverlay>(
                pLogicalDevice, pLogicalSwapchain->format, pLogicalSwapchain->imageCount,
                pLogicalDevice->overlayPersistentState.get());
        }

        *pCount = std::min<uint32_t>(*pCount, pLogicalSwapchain->imageCount);
        std::memcpy(pSwapchainImages, pLogicalSwapchain->fakeImages.data(), sizeof(VkImage) * (*pCount));
        return *pCount < pLogicalSwapchain->imageCount ? VK_INCOMPLETE : VK_SUCCESS;
    }

    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
    {
        scoped_lock l(globalLock);

        static uint32_t keySymbol = convertToKeySym(pConfig->getOption<std::string>("toggleKey", "Home"));
        static uint32_t reloadKeySymbol = convertToKeySym(pConfig->getOption<std::string>("reloadKey", "F10"));
        static uint32_t overlayKeySymbol = convertToKeySym(pConfig->getOption<std::string>("overlayKey", "End"));
        static bool initLogged = false;

        static bool pressed       = false;
        static bool presentEffect = pConfig->getOption<bool>("enableOnLaunch", true);
        static bool reloadPressed = false;
        static bool overlayPressed = false;

        if (!initLogged)
        {
            Logger::info("hot-reload initialized, config: " + pConfig->getConfigFilePath());
            initLogged = true;
        }

        // Toggle effect on/off
        if (isKeyPressed(keySymbol))
        {
            if (!pressed)
            {
                presentEffect = !presentEffect;
                pressed       = true;
            }
        }
        else
        {
            pressed = false;
        }

        // Hot-reload: check for key press or config file change
        bool shouldReload = false;

        if (isKeyPressed(reloadKeySymbol))
        {
            if (!reloadPressed)
            {
                Logger::debug("reload key pressed");
                shouldReload = true;
                reloadPressed = true;
            }
        }
        else
        {
            reloadPressed = false;
        }

        // Also check if config file was modified
        if (pConfig->hasConfigChanged())
        {
            Logger::debug("config file changed detected");
            shouldReload = true;
        }

        // Toggle overlay on/off
        if (isKeyPressed(overlayKeySymbol))
        {
            if (!overlayPressed)
            {
                // Overlay is now at device level
                LogicalDevice* pDevice = deviceMap[GetKey(queue)].get();
                if (pDevice->imguiOverlay)
                    pDevice->imguiOverlay->toggle();
                overlayPressed = true;
            }
        }
        else
        {
            overlayPressed = false;
        }

        // Check for Apply button press in overlay (overlay is at device level)
        std::map<std::string, bool> effectEnabledStates;
        LogicalDevice* pLogicalDevice = deviceMap[GetKey(queue)].get();
        if (pLogicalDevice->imguiOverlay && pLogicalDevice->imguiOverlay->hasModifiedParams())
        {
            Logger::info("Applying modified parameters from overlay");
            auto params = pLogicalDevice->imguiOverlay->getModifiedParams();
            for (const auto& param : params)
            {
                std::string valueStr;
                switch (param.type)
                {
                case ParamType::Float:
                    valueStr = std::to_string(param.valueFloat);
                    break;
                case ParamType::Int:
                    valueStr = std::to_string(param.valueInt);
                    break;
                case ParamType::Bool:
                    valueStr = param.valueBool ? "true" : "false";
                    break;
                }
                pConfig->setOverride(param.name, valueStr);
            }
            effectEnabledStates = pLogicalDevice->imguiOverlay->getEffectEnabledStates();
            pLogicalDevice->imguiOverlay->clearApplyRequest();
            shouldReload = true;
        }

        if (shouldReload)
        {
            Logger::info("hot-reloading config and effects...");

            // Check if overlay wants to load a different config
            if (pLogicalDevice->imguiOverlay && pLogicalDevice->imguiOverlay->hasPendingConfig())
            {
                std::string newConfigPath = pLogicalDevice->imguiOverlay->getPendingConfigPath();
                switchConfig(newConfigPath);
                // Update overlay with effects and disabled effects from the new config
                std::vector<std::string> newEffects = pConfig->getOption<std::vector<std::string>>("effects", {});
                std::vector<std::string> disabledEffects = pConfig->getOption<std::vector<std::string>>("disabledEffects", {});
                pLogicalDevice->imguiOverlay->setSelectedEffects(newEffects, disabledEffects);
                pLogicalDevice->imguiOverlay->clearPendingConfig();
            }
            else
            {
                pConfig->reload();
            }
            cachedEffects.initialized = false;  // Invalidate cache
            cachedParams.dirty = true;          // Invalidate params cache

            // Get active effects from overlay (at device level)
            std::vector<std::string> activeEffects;
            if (pLogicalDevice->imguiOverlay)
                activeEffects = pLogicalDevice->imguiOverlay->getActiveEffects();
            else
                activeEffects = pConfig->getOption<std::vector<std::string>>("effects", {"cas"});

            // Reload effects for all swapchains
            for (auto& swapchainPair : swapchainMap)
            {
                LogicalSwapchain* pLogicalSwapchain = swapchainPair.second.get();
                if (pLogicalSwapchain->fakeImages.size() > 0)
                {
                    reloadEffectsForSwapchain(pLogicalSwapchain, pConfig.get(), effectEnabledStates, activeEffects);
                }
            }
        }

        // Check for debounced resize reload (separate from config reload)
        auto resizeElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - resizeDebounce.lastResizeTime).count();

        if (resizeDebounce.pending && resizeElapsed >= RESIZE_DEBOUNCE_MS)
        {
            Logger::info("debounced resize reload after " + std::to_string(resizeElapsed) + "ms");
            resizeDebounce.pending = false;

            for (auto& [_, pSwapchain] : swapchainMap)
            {
                if (pSwapchain->fakeImages.empty() || !pLogicalDevice->overlayPersistentState)
                    continue;
                reloadEffectsForSwapchain(pSwapchain.get(), pConfig.get(),
                    pLogicalDevice->overlayPersistentState->effectEnabledStates,
                    pLogicalDevice->overlayPersistentState->selectedEffects);
            }
        }

        std::vector<VkSemaphore> presentSemaphores;
        presentSemaphores.reserve(pPresentInfo->swapchainCount);

        std::vector<VkPipelineStageFlags> waitStages(pPresentInfo->waitSemaphoreCount, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        for (unsigned int i = 0; i < (*pPresentInfo).swapchainCount; i++)
        {
            uint32_t          index             = (*pPresentInfo).pImageIndices[i];
            VkSwapchainKHR    swapchain         = (*pPresentInfo).pSwapchains[i];
            LogicalSwapchain* pLogicalSwapchain = swapchainMap[swapchain].get();

            for (auto& effect : pLogicalSwapchain->effects)
            {
                effect->updateEffect();
            }

            VkSubmitInfo submitInfo;
            submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.pNext              = nullptr;
            submitInfo.waitSemaphoreCount = i == 0 ? pPresentInfo->waitSemaphoreCount : 0;
            submitInfo.pWaitSemaphores    = i == 0 ? pPresentInfo->pWaitSemaphores : nullptr;
            submitInfo.pWaitDstStageMask  = i == 0 ? waitStages.data() : nullptr;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers =
                presentEffect ? &(pLogicalSwapchain->commandBuffersEffect[index]) : &(pLogicalSwapchain->commandBuffersNoEffect[index]);
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores    = &(pLogicalSwapchain->semaphores[index]);

            VkResult vr = pLogicalDevice->vkd.QueueSubmit(pLogicalDevice->queue, 1, &submitInfo, VK_NULL_HANDLE);
            if (vr != VK_SUCCESS)
                return vr;

            // Default: wait on effects semaphore for present
            VkSemaphore finalSemaphore = pLogicalSwapchain->semaphores[index];

            // Update overlay state and render if visible (overlay is at device level)
            if (pLogicalDevice->imguiOverlay && pLogicalDevice->imguiOverlay->isVisible())
            {
                OverlayState overlayState;
                // Use active effects for display, but collect params for ALL selected effects
                overlayState.effectNames = pLogicalDevice->imguiOverlay->getActiveEffects();
                if (overlayState.effectNames.empty())
                    overlayState.effectNames = pConfig->getOption<std::vector<std::string>>("effects", {"cas"});
                getAvailableEffects(pConfig.get(), overlayState.currentConfigEffects,
                                    overlayState.defaultConfigEffects, overlayState.effectPaths);
                overlayState.configPath = pConfig->getConfigFilePath();
                overlayState.configName = std::filesystem::path(overlayState.configPath).filename().string();
                overlayState.effectsEnabled = presentEffect;

                // Collect parameters for ALL selected effects (including disabled)
                const auto& allSelectedEffects = pLogicalDevice->imguiOverlay->getSelectedEffects();
                bool effectsChanged = cachedParams.effectNames != allSelectedEffects;
                bool configChanged = cachedParams.configPath != overlayState.configPath;
                if (cachedParams.dirty || effectsChanged || configChanged)
                {
                    cachedParams.parameters = collectEffectParameters(
                        pConfig, allSelectedEffects, pLogicalSwapchain->effects);
                    cachedParams.effectNames = allSelectedEffects;
                    cachedParams.configPath = overlayState.configPath;
                    cachedParams.dirty = false;
                }
                overlayState.parameters = cachedParams.parameters;

                pLogicalDevice->imguiOverlay->updateState(overlayState);
            }

            VkCommandBuffer overlayCmd = pLogicalDevice->imguiOverlay
                ? pLogicalDevice->imguiOverlay->recordFrame(index, pLogicalSwapchain->imageViews[index],
                      pLogicalSwapchain->imageExtent.width, pLogicalSwapchain->imageExtent.height)
                : VK_NULL_HANDLE;

            if (overlayCmd != VK_NULL_HANDLE)
            {
                VkPipelineStageFlags overlayWaitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                VkSubmitInfo overlaySubmit = {};
                overlaySubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                overlaySubmit.waitSemaphoreCount = 1;
                overlaySubmit.pWaitSemaphores = &(pLogicalSwapchain->semaphores[index]);
                overlaySubmit.pWaitDstStageMask = &overlayWaitStage;
                overlaySubmit.commandBufferCount = 1;
                overlaySubmit.pCommandBuffers = &overlayCmd;
                overlaySubmit.signalSemaphoreCount = 1;
                overlaySubmit.pSignalSemaphores = &(pLogicalSwapchain->overlaySemaphores[index]);

                vr = pLogicalDevice->vkd.QueueSubmit(pLogicalDevice->queue, 1, &overlaySubmit, VK_NULL_HANDLE);
                if (vr != VK_SUCCESS)
                    return vr;

                finalSemaphore = pLogicalSwapchain->overlaySemaphores[index];
            }

            presentSemaphores.push_back(finalSemaphore);
        }

        VkPresentInfoKHR presentInfo   = *pPresentInfo;
        presentInfo.waitSemaphoreCount = presentSemaphores.size();
        presentInfo.pWaitSemaphores    = presentSemaphores.data();

        return pLogicalDevice->vkd.QueuePresentKHR(queue, &presentInfo);
    }

    VKAPI_ATTR void VKAPI_CALL vkBasalt_DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator)
    {
        if (!swapchain)
            return;

        scoped_lock l(globalLock);
        // we need to delete the infos of the oldswapchain

        Logger::trace("vkDestroySwapchainKHR " + convertToString(swapchain));
        swapchainMap[swapchain]->destroy();
        swapchainMap.erase(swapchain);
        LogicalDevice* pLogicalDevice = deviceMap[GetKey(device)].get();

        pLogicalDevice->vkd.DestroySwapchainKHR(device, swapchain, pAllocator);
    }

    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_CreateImage(VkDevice                     device,
                                                        const VkImageCreateInfo*     pCreateInfo,
                                                        const VkAllocationCallbacks* pAllocator,
                                                        VkImage*                     pImage)
    {
        scoped_lock l(globalLock);

        LogicalDevice* pLogicalDevice = deviceMap[GetKey(device)].get();
        if (isDepthFormat(pCreateInfo->format) && pCreateInfo->samples == VK_SAMPLE_COUNT_1_BIT
            && ((pCreateInfo->usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) == VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))
        {
            Logger::debug("detected depth image with format: " + convertToString(pCreateInfo->format));
            Logger::debug(std::to_string(pCreateInfo->extent.width) + "x" + std::to_string(pCreateInfo->extent.height));
            Logger::debug(
                std::to_string((pCreateInfo->usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) == VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT));

            VkImageCreateInfo modifiedCreateInfo = *pCreateInfo;
            modifiedCreateInfo.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
            VkResult result = pLogicalDevice->vkd.CreateImage(device, &modifiedCreateInfo, pAllocator, pImage);
            pLogicalDevice->depthImages.push_back(*pImage);
            pLogicalDevice->depthFormats.push_back(pCreateInfo->format);

            return result;
        }
        else
        {
            return pLogicalDevice->vkd.CreateImage(device, pCreateInfo, pAllocator, pImage);
        }
    }

    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_BindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset)
    {
        scoped_lock l(globalLock);

        LogicalDevice* pLogicalDevice = deviceMap[GetKey(device)].get();

        VkResult result = pLogicalDevice->vkd.BindImageMemory(device, image, memory, memoryOffset);
        // TODO what if the application creates more than one image before binding memory?
        if (pLogicalDevice->depthImages.size() && image == pLogicalDevice->depthImages.back())
        {
            Logger::debug("before creating depth image view");
            VkImageView depthImageView = createImageViews(pLogicalDevice,
                                                          pLogicalDevice->depthFormats[pLogicalDevice->depthImages.size() - 1],
                                                          {image},
                                                          VK_IMAGE_VIEW_TYPE_2D,
                                                          VK_IMAGE_ASPECT_DEPTH_BIT)[0];

            VkFormat depthFormat = pLogicalDevice->depthFormats[pLogicalDevice->depthImages.size() - 1];

            Logger::debug("created depth image view");
            pLogicalDevice->depthImageViews.push_back(depthImageView);
            if (pLogicalDevice->depthImageViews.size() > 1)
            {
                return result;
            }

            for (auto& it : swapchainMap)
            {
                LogicalSwapchain* pLogicalSwapchain = it.second.get();
                if (pLogicalSwapchain->pLogicalDevice == pLogicalDevice)
                {
                    if (pLogicalSwapchain->commandBuffersEffect.size())
                    {
                        pLogicalDevice->vkd.FreeCommandBuffers(pLogicalDevice->device,
                                                               pLogicalDevice->commandPool,
                                                               pLogicalSwapchain->commandBuffersEffect.size(),
                                                               pLogicalSwapchain->commandBuffersEffect.data());
                        pLogicalSwapchain->commandBuffersEffect.clear();
                        pLogicalSwapchain->commandBuffersEffect = allocateCommandBuffer(pLogicalDevice, pLogicalSwapchain->imageCount);
                        Logger::debug("allocated CommandBuffers for swapchain " + convertToString(it.first));

                        writeCommandBuffers(
                            pLogicalDevice, pLogicalSwapchain->effects, image, depthImageView, depthFormat, pLogicalSwapchain->commandBuffersEffect);
                        Logger::debug("wrote CommandBuffers");
                    }
                }
            }
        }
        return result;
    }

    VKAPI_ATTR void VKAPI_CALL vkBasalt_DestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator)
    {
        if (!image)
            return;

        scoped_lock l(globalLock);

        LogicalDevice* pLogicalDevice = deviceMap[GetKey(device)].get();

        for (uint32_t i = 0; i < pLogicalDevice->depthImages.size(); i++)
        {
            if (pLogicalDevice->depthImages[i] == image)
            {
                pLogicalDevice->depthImages.erase(pLogicalDevice->depthImages.begin() + i);
                // TODO what if a image gets destroyed before binding memory?
                if (pLogicalDevice->depthImageViews.size() - 1 >= i)
                {
                    pLogicalDevice->vkd.DestroyImageView(pLogicalDevice->device, pLogicalDevice->depthImageViews[i], nullptr);
                    pLogicalDevice->depthImageViews.erase(pLogicalDevice->depthImageViews.begin() + i);
                }
                pLogicalDevice->depthFormats.erase(pLogicalDevice->depthFormats.begin() + i);

                VkImageView depthImageView = pLogicalDevice->depthImageViews.size() ? pLogicalDevice->depthImageViews[0] : VK_NULL_HANDLE;
                VkImage     depthImage     = pLogicalDevice->depthImageViews.size() ? pLogicalDevice->depthImages[0] : VK_NULL_HANDLE;
                VkFormat    depthFormat    = pLogicalDevice->depthImageViews.size() ? pLogicalDevice->depthFormats[0] : VK_FORMAT_UNDEFINED;
                for (auto& it : swapchainMap)
                {
                    LogicalSwapchain* pLogicalSwapchain = it.second.get();
                    if (pLogicalSwapchain->pLogicalDevice == pLogicalDevice)
                    {
                        if (pLogicalSwapchain->commandBuffersEffect.size())
                        {
                            pLogicalDevice->vkd.FreeCommandBuffers(pLogicalDevice->device,
                                                                   pLogicalDevice->commandPool,
                                                                   pLogicalSwapchain->commandBuffersEffect.size(),
                                                                   pLogicalSwapchain->commandBuffersEffect.data());
                            pLogicalSwapchain->commandBuffersEffect.clear();
                            pLogicalSwapchain->commandBuffersEffect = allocateCommandBuffer(pLogicalDevice, pLogicalSwapchain->imageCount);
                            Logger::debug("allocated CommandBuffers for swapchain " + convertToString(it.first));

                            writeCommandBuffers(pLogicalDevice,
                                                pLogicalSwapchain->effects,
                                                depthImage,
                                                depthImageView,
                                                depthFormat,
                                                pLogicalSwapchain->commandBuffersEffect);
                            Logger::debug("wrote CommandBuffers");
                        }
                    }
                }
            }
        }

        pLogicalDevice->vkd.DestroyImage(pLogicalDevice->device, image, pAllocator);
    }

    ///////////////////////////////////////////////////////////////////////////////////////////
    // Enumeration function

    VkResult VKAPI_CALL vkBasalt_EnumerateInstanceLayerProperties(uint32_t* pPropertyCount, VkLayerProperties* pProperties)
    {
        if (pPropertyCount)
            *pPropertyCount = 1;

        if (pProperties)
        {
            std::strcpy(pProperties->layerName, VKBASALT_NAME);
            std::strcpy(pProperties->description, "a post processing layer");
            pProperties->implementationVersion = 1;
            pProperties->specVersion           = VK_MAKE_VERSION(1, 2, 0);
        }

        return VK_SUCCESS;
    }

    VkResult VKAPI_CALL vkBasalt_EnumerateDeviceLayerProperties(VkPhysicalDevice   physicalDevice,
                                                                uint32_t*          pPropertyCount,
                                                                VkLayerProperties* pProperties)
    {
        return vkBasalt_EnumerateInstanceLayerProperties(pPropertyCount, pProperties);
    }

    VkResult VKAPI_CALL vkBasalt_EnumerateInstanceExtensionProperties(const char*            pLayerName,
                                                                      uint32_t*              pPropertyCount,
                                                                      VkExtensionProperties* pProperties)
    {
        if (pLayerName == NULL || std::strcmp(pLayerName, VKBASALT_NAME))
        {
            return VK_ERROR_LAYER_NOT_PRESENT;
        }

        // don't expose any extensions
        if (pPropertyCount)
        {
            *pPropertyCount = 0;
        }
        return VK_SUCCESS;
    }

    VkResult VKAPI_CALL vkBasalt_EnumerateDeviceExtensionProperties(VkPhysicalDevice       physicalDevice,
                                                                    const char*            pLayerName,
                                                                    uint32_t*              pPropertyCount,
                                                                    VkExtensionProperties* pProperties)
    {
        // pass through any queries that aren't to us
        if (pLayerName == NULL || std::strcmp(pLayerName, VKBASALT_NAME))
        {
            if (physicalDevice == VK_NULL_HANDLE)
            {
                return VK_SUCCESS;
            }

            scoped_lock l(globalLock);
            return instanceDispatchMap[GetKey(physicalDevice)].EnumerateDeviceExtensionProperties(
                physicalDevice, pLayerName, pPropertyCount, pProperties);
        }

        // don't expose any extensions
        if (pPropertyCount)
        {
            *pPropertyCount = 0;
        }
        return VK_SUCCESS;
    }
} // namespace vkBasalt

extern "C"
{ // these are the entry points for the layer, so they need to be c-linkeable

    VK_BASALT_EXPORT PFN_vkVoidFunction VKAPI_CALL vkBasalt_GetDeviceProcAddr(VkDevice device, const char* pName);
    VK_BASALT_EXPORT PFN_vkVoidFunction VKAPI_CALL vkBasalt_GetInstanceProcAddr(VkInstance instance, const char* pName);

#define GETPROCADDR(func) \
    if (!std::strcmp(pName, "vk" #func)) \
        return (PFN_vkVoidFunction) &vkBasalt::vkBasalt_##func;
    /*
    Return our funktions for the funktions we want to intercept
    the macro takes the name and returns our vkBasalt_##func, if the name is equal
    */

    // vkGetDeviceProcAddr needs to behave like vkGetInstanceProcAddr thanks to some games
#define INTERCEPT_CALLS \
    /* instance chain functions we intercept */ \
    if (!std::strcmp(pName, "vkGetInstanceProcAddr")) \
        return (PFN_vkVoidFunction) &vkBasalt_GetInstanceProcAddr; \
    GETPROCADDR(EnumerateInstanceLayerProperties); \
    GETPROCADDR(EnumerateInstanceExtensionProperties); \
    GETPROCADDR(CreateInstance); \
    GETPROCADDR(DestroyInstance); \
\
    /* device chain functions we intercept*/ \
    if (!std::strcmp(pName, "vkGetDeviceProcAddr")) \
        return (PFN_vkVoidFunction) &vkBasalt_GetDeviceProcAddr; \
    GETPROCADDR(EnumerateDeviceLayerProperties); \
    GETPROCADDR(EnumerateDeviceExtensionProperties); \
    GETPROCADDR(CreateDevice); \
    GETPROCADDR(DestroyDevice); \
    GETPROCADDR(CreateSwapchainKHR); \
    GETPROCADDR(GetSwapchainImagesKHR); \
    GETPROCADDR(QueuePresentKHR); \
    GETPROCADDR(DestroySwapchainKHR); \
\
    if (vkBasalt::pConfig->getOption<std::string>("depthCapture", "off") == "on") \
    { \
        GETPROCADDR(CreateImage); \
        GETPROCADDR(DestroyImage); \
        GETPROCADDR(BindImageMemory); \
    }

    VK_BASALT_EXPORT PFN_vkVoidFunction VKAPI_CALL vkBasalt_GetDeviceProcAddr(VkDevice device, const char* pName)
    {
        vkBasalt::initConfigs();

        INTERCEPT_CALLS

        {
            vkBasalt::scoped_lock l(vkBasalt::globalLock);
            return vkBasalt::deviceMap[vkBasalt::GetKey(device)]->vkd.GetDeviceProcAddr(device, pName);
        }
    }

    VK_BASALT_EXPORT PFN_vkVoidFunction VKAPI_CALL vkBasalt_GetInstanceProcAddr(VkInstance instance, const char* pName)
    {
        vkBasalt::initConfigs();

        INTERCEPT_CALLS

        {
            vkBasalt::scoped_lock l(vkBasalt::globalLock);
            return vkBasalt::instanceDispatchMap[vkBasalt::GetKey(instance)].GetInstanceProcAddr(instance, pName);
        }
    }

} // extern "C"
