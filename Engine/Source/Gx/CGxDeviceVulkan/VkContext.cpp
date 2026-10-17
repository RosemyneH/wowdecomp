#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include "Engine/Source/Gx/CGxDeviceVulkan/VkContext.h"
#include "Engine/Source/Os/Gui.h"

int32_t VkHeap::Alloc(VkDevice device, const VkMemoryRequirements& requirements, uint32_t type, VkAllocation& out) {
    VkDeviceSize alignment = requirements.alignment ? requirements.alignment : 1;

    for (size_t i = 0; i < this->m_blocks.size(); i++) {
        Block& block = this->m_blocks[i];

        if (block.type != type) {
            continue;
        }

        for (auto it = block.free.begin(); it != block.free.end(); ++it) {
            VkDeviceSize freeStart = it->first;
            VkDeviceSize freeEnd = it->first + it->second;
            VkDeviceSize start = (freeStart + alignment - 1) / alignment * alignment;

            if (start + requirements.size > freeEnd) {
                continue;
            }

            block.free.erase(it);

            if (start > freeStart) {
                block.free[freeStart] = start - freeStart;
            }

            if (start + requirements.size < freeEnd) {
                block.free[start + requirements.size] = freeEnd - start - requirements.size;
            }

            out = { block.memory, start, requirements.size, static_cast<int32_t>(i) };
            return 1;
        }
    }

    VkDeviceSize blockSize = requirements.size > BlockSize ? requirements.size : static_cast<VkDeviceSize>(BlockSize);

    VkMemoryAllocateInfo info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    info.allocationSize = blockSize;
    info.memoryTypeIndex = type;

    Block block;
    block.size = blockSize;
    block.type = type;

    if (vkAllocateMemory(device, &info, nullptr, &block.memory) != VK_SUCCESS) {
        return 0;
    }

    if (requirements.size < blockSize) {
        block.free[requirements.size] = blockSize - requirements.size;
    }

    this->m_blocks.push_back(block);
    out = { block.memory, 0, requirements.size, static_cast<int32_t>(this->m_blocks.size() - 1) };
    return 1;
}

void VkHeap::Free(const VkAllocation& alloc) {
    std::map<VkDeviceSize, VkDeviceSize>& free = this->m_blocks[alloc.block].free;
    auto it = free.emplace(alloc.offset, alloc.size).first;

    auto next = std::next(it);

    if (next != free.end() && it->first + it->second == next->first) {
        it->second += next->second;
        free.erase(next);
    }

    if (it != free.begin()) {
        auto prev = std::prev(it);

        if (prev->first + prev->second == it->first) {
            prev->second += it->second;
            free.erase(it);
        }
    }
}

void VkHeap::Destroy(VkDevice device) {
    for (Block& block : this->m_blocks) {
        vkFreeMemory(device, block.memory, nullptr);
    }

    this->m_blocks.clear();
}

int32_t VkContext::CreateInstance(const char* const* extensions, uint32_t extensionCount) {
    if (this->m_instance) {
        return 1;
    }

    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "World of Warcraft";
    app.pEngineName = "Gx";
    app.apiVersion = VK_API_VERSION_1_2;

    std::vector<const char*> enabled(extensions, extensions + extensionCount);
    const char* validation = "VK_LAYER_KHRONOS_validation";

    VkInstanceCreateInfo info = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    info.pApplicationInfo = &app;

#if defined(WOWDECOMP_VULKAN_MOLTENVK)
    enabled.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

    info.enabledExtensionCount = static_cast<uint32_t>(enabled.size());
    info.ppEnabledExtensionNames = enabled.data();

    if (getenv("WOWDECOMP_VK_VALIDATION")) {
        info.enabledLayerCount = 1;
        info.ppEnabledLayerNames = &validation;
    }

#if defined(_WIN32)
    if (volkInitialize() != VK_SUCCESS) {
        return 0;
    }
#else
    volkInitializeCustom(reinterpret_cast<PFN_vkGetInstanceProcAddr>(OsGuiGetVulkanProcAddr()));
    if (!vkCreateInstance) {
        return 0;
    }
#endif

    if (vkCreateInstance(&info, nullptr, &this->m_instance) != VK_SUCCESS) {
        return 0;
    }

    volkLoadInstance(this->m_instance);
    return 1;
}

int32_t VkContext::CreateDevice(VkSurfaceKHR surface) {
    this->m_surface = surface;

    uint32_t count = 0;
    vkEnumeratePhysicalDevices(this->m_instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(this->m_instance, &count, devices.data());

    int32_t bestScore = -1;

    for (VkPhysicalDevice device : devices) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(device, &properties);

        if (properties.apiVersion < VK_API_VERSION_1_1) {
            continue;
        }

        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, families.data());

        for (uint32_t family = 0; family < familyCount; family++) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, family, surface, &present);

            if (!(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !present) {
                continue;
            }

            int32_t score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 1 : 0;

            if (score > bestScore) {
                bestScore = score;
                this->m_physicalDevice = device;
                this->m_properties = properties;
                this->m_queueFamily = family;
            }

            break;
        }
    }

    if (bestScore < 0) {
        return 0;
    }

    vkGetPhysicalDeviceMemoryProperties(this->m_physicalDevice, &this->m_memoryProperties);
    this->ChooseHeaps();

    VkPhysicalDeviceDescriptorIndexingFeatures indexingSupported = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES };
    VkPhysicalDeviceFeatures2 features2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    features2.pNext = &indexingSupported;
    vkGetPhysicalDeviceFeatures2(this->m_physicalDevice, &features2);
    VkPhysicalDeviceFeatures supported = features2.features;
    this->m_features.textureCompressionBC = supported.textureCompressionBC;
    this->m_features.samplerAnisotropy = supported.samplerAnisotropy;
    this->m_features.fillModeNonSolid = supported.fillModeNonSolid;
    this->m_features.largePoints = supported.largePoints;
    // ʕ •ᴥ•ʔ✿ One indirect command covers a whole terrain layer. firstInstance still indexes doodad instances ✿ ʕ •ᴥ•ʔ
    this->m_features.multiDrawIndirect = supported.multiDrawIndirect;
    this->m_features.drawIndirectFirstInstance = supported.drawIndirectFirstInstance;
    this->m_bindless = indexingSupported.shaderSampledImageArrayNonUniformIndexing && indexingSupported.descriptorBindingPartiallyBound && indexingSupported.descriptorBindingSampledImageUpdateAfterBind && indexingSupported.runtimeDescriptorArray;

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    queueInfo.queueFamilyIndex = this->m_queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    std::vector<const char*> extensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(this->m_physicalDevice, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> available(extensionCount);
    vkEnumerateDeviceExtensionProperties(this->m_physicalDevice, nullptr, &extensionCount, available.data());

    for (const VkExtensionProperties& extension : available) {
        if (!strcmp(extension.extensionName, "VK_KHR_portability_subset")) {
            extensions.push_back("VK_KHR_portability_subset");
        }
    }

    VkPhysicalDeviceDescriptorIndexingFeatures indexing = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES };

    if (this->m_bindless) {
        indexing.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        indexing.descriptorBindingPartiallyBound = VK_TRUE;
        indexing.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
        indexing.runtimeDescriptorArray = VK_TRUE;
    }

    VkDeviceCreateInfo info = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    info.pNext = this->m_bindless ? &indexing : nullptr;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queueInfo;
    info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();
    info.pEnabledFeatures = &this->m_features;

    if (vkCreateDevice(this->m_physicalDevice, &info, nullptr, &this->m_device) != VK_SUCCESS) {
        return 0;
    }

    volkLoadDevice(this->m_device);

    vkGetDeviceQueue(this->m_device, this->m_queueFamily, 0, &this->m_queue);

    const VkFormat depthFormats[] = { VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D32_SFLOAT };

    for (VkFormat format : depthFormats) {
        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(this->m_physicalDevice, format, &properties);

        if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            this->m_depthFormat = format;
            break;
        }
    }

    return this->CreateDeviceResources();
}

int32_t VkContext::CreateDeviceResources() {
    VkCommandPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = this->m_queueFamily;
    if (vkCreateCommandPool(this->m_device, &poolInfo, nullptr, &this->m_commandPool) != VK_SUCCESS) {
        this->m_commandPool = VK_NULL_HANDLE;
        return 0;
    }

    for (VkFrame& frame : this->m_frames) {
        VkCommandBufferAllocateInfo cmdInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        cmdInfo.commandPool = this->m_commandPool;
        cmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdInfo.commandBufferCount = 2;

        VkCommandBuffer cmds[2];
        if (vkAllocateCommandBuffers(this->m_device, &cmdInfo, cmds) != VK_SUCCESS) {
            cmds[0] = VK_NULL_HANDLE;
            cmds[1] = VK_NULL_HANDLE;
            return 0;
        }
        frame.cmd = cmds[0];
        frame.uploadCmd = cmds[1];

        VkFenceCreateInfo fenceInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        if (vkCreateFence(this->m_device, &fenceInfo, nullptr, &frame.fence) != VK_SUCCESS) {
            frame.fence = VK_NULL_HANDLE;
            return 0;
        }

        VkSemaphoreCreateInfo semaphoreInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        if (vkCreateSemaphore(this->m_device, &semaphoreInfo, nullptr, &frame.imageAvailable) != VK_SUCCESS) {
            frame.imageAvailable = VK_NULL_HANDLE;
            return 0;
        }

        VkDescriptorPool pool = this->CreateDescriptorPool();
        if (!pool) return 0;
        frame.descriptorPools.push_back(pool);

        if (!this->CreateBuffer(UniformRingSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, frame.uniforms)) {
            return 0;
        }

        if (!this->CreateBuffer(BoneRingSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, frame.bones)) {
            return 0;
        }

        if (!this->CreateBuffer(ItemRingSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, frame.items)) {
            return 0;
        }

        if (!this->CreateBuffer(IndirectRingSize, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, frame.indirect)) {
            return 0;
        }
    }

    this->m_passes[Pass_Offscreen] = this->CreateRenderPass(s_offscreenFormat, VK_ATTACHMENT_LOAD_OP_LOAD, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

    return this->m_depthFormat != VK_FORMAT_UNDEFINED && this->m_passes[Pass_Offscreen];
}

void VkContext::DestroySurface() {
    if (this->m_device) {
        vkDeviceWaitIdle(this->m_device);
        this->DestroySwapchain();
    }

    if (this->m_surface) {
        vkDestroySurfaceKHR(this->m_instance, this->m_surface, nullptr);
        this->m_surface = VK_NULL_HANDLE;
    }
}

int32_t VkContext::SetSurface(VkSurfaceKHR surface) {
    this->m_surface = surface;

    VkBool32 present = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(this->m_physicalDevice, this->m_queueFamily, surface, &present);

    return present;
}

VkRenderPass VkContext::CreateRenderPass(VkFormat color, VkAttachmentLoadOp load, VkImageLayout colorInitial, VkImageLayout colorFinal, VkImageLayout depthInitial) {
    VkAttachmentDescription attachments[2] = {};
    attachments[0].format = color;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = load;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = colorInitial;
    attachments[0].finalLayout = colorFinal;

    attachments[1] = attachments[0];
    attachments[1].format = this->m_depthFormat;
    attachments[1].initialLayout = depthInitial;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depthRef = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    const VkPipelineStageFlags attachmentStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    const VkAccessFlags attachmentAccess = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkSubpassDependency dependencies[2] = {};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = attachmentStages | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
    dependencies[0].dstStageMask = attachmentStages | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    dependencies[0].dstAccessMask = attachmentAccess | VK_ACCESS_SHADER_READ_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = attachmentStages;
    dependencies[1].dstStageMask = attachmentStages | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = attachmentAccess | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;

    VkRenderPassCreateInfo info = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    info.attachmentCount = 2;
    info.pAttachments = attachments;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 2;
    info.pDependencies = dependencies;

    VkRenderPass pass = VK_NULL_HANDLE;
    if (vkCreateRenderPass(this->m_device, &info, nullptr, &pass) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return pass;
}

int32_t VkContext::CreateSwapchain(uint32_t width, uint32_t height, int32_t vsync) {
    if (!this->m_device || !this->m_surface) {
        return 0;
    }

    vkDeviceWaitIdle(this->m_device);

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(this->m_physicalDevice, this->m_surface, &caps);

    VkExtent2D extent = caps.currentExtent;

    if (extent.width == UINT32_MAX) {
        extent.width = width < caps.minImageExtent.width ? caps.minImageExtent.width : width > caps.maxImageExtent.width ? caps.maxImageExtent.width : width;
        extent.height = height < caps.minImageExtent.height ? caps.minImageExtent.height : height > caps.maxImageExtent.height ? caps.maxImageExtent.height : height;
    }

    if (!extent.width || !extent.height) {
        return 0;
    }

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(this->m_physicalDevice, this->m_surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(this->m_physicalDevice, this->m_surface, &formatCount, formats.data());

    VkSurfaceFormatKHR surfaceFormat = formats[0];

    for (const VkSurfaceFormatKHR& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            surfaceFormat = format;
            break;
        }

        if (format.format == VK_FORMAT_R8G8B8A8_UNORM) {
            surfaceFormat = format;
        }
    }

    uint32_t modeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(this->m_physicalDevice, this->m_surface, &modeCount, nullptr);
    std::vector<VkPresentModeKHR> modes(modeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(this->m_physicalDevice, this->m_surface, &modeCount, modes.data());

    auto modeSupported = [&](VkPresentModeKHR mode) {
        for (VkPresentModeKHR supported : modes) {
            if (supported == mode) {
                return true;
            }
        }

        return false;
    };

    VkPresentModeKHR candidates[3];
    uint32_t candidateCount = 0;

    if (vsync) {
        candidates[candidateCount++] = VK_PRESENT_MODE_FIFO_KHR;
    } else {
        if (modeSupported(VK_PRESENT_MODE_MAILBOX_KHR)) {
            candidates[candidateCount++] = VK_PRESENT_MODE_MAILBOX_KHR;
        }

        if (modeSupported(VK_PRESENT_MODE_IMMEDIATE_KHR)) {
            candidates[candidateCount++] = VK_PRESENT_MODE_IMMEDIATE_KHR;
        }

        candidates[candidateCount++] = VK_PRESENT_MODE_FIFO_KHR;
    }

    VkSwapchainCreateInfoKHR info = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    info.surface = this->m_surface;
    info.imageFormat = surfaceFormat.format;
    info.imageColorSpace = surfaceFormat.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (caps.supportedUsageFlags & (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT));
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;

    if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) {
        info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    } else if (caps.supportedCompositeAlpha) {
        info.compositeAlpha = static_cast<VkCompositeAlphaFlagBitsKHR>(caps.supportedCompositeAlpha & -static_cast<int32_t>(caps.supportedCompositeAlpha));
    } else {
        info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    }

    info.clipped = VK_TRUE;
    info.oldSwapchain = this->m_swapchain;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;

    for (uint32_t candidateIndex = 0; candidateIndex < candidateCount; candidateIndex++) {
        VkPresentModeKHR presentMode = candidates[candidateIndex];
        uint32_t imageCount = caps.minImageCount + 1;

        if (presentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            imageCount = caps.minImageCount + 2;

            if (imageCount < 3) {
                imageCount = 3;
            }
        }

        if (caps.maxImageCount && imageCount > caps.maxImageCount) {
            imageCount = caps.maxImageCount;
        }

        if (presentMode == VK_PRESENT_MODE_MAILBOX_KHR && imageCount < 3) {
            continue;
        }

        info.presentMode = presentMode;
        info.minImageCount = imageCount;

        if (vkCreateSwapchainKHR(this->m_device, &info, nullptr, &swapchain) == VK_SUCCESS) {
            break;
        }

        swapchain = VK_NULL_HANDLE;
    }

    if (!swapchain) {
        return 0;
    }

    this->DestroySwapchain();
    this->m_swapchain = swapchain;
    this->m_extent = extent;

    if (this->m_colorFormat != surfaceFormat.format) {
        for (uint32_t pass = Pass_Clear; pass <= Pass_Load; pass++) {
            if (this->m_passes[pass]) {
                vkDestroyRenderPass(this->m_device, this->m_passes[pass], nullptr);
                this->m_passes[pass] = VK_NULL_HANDLE;
            }
        }

        VkFormat previousColor = this->m_colorFormat;
        this->m_colorFormat = surfaceFormat.format;
        this->m_passes[Pass_Clear] = this->CreateRenderPass(this->m_colorFormat, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_UNDEFINED);
        if (!this->m_passes[Pass_Clear]) {
            this->m_colorFormat = previousColor;
            this->DestroySwapchain();
            return 0;
        }
        this->m_passes[Pass_Load] = this->CreateRenderPass(this->m_colorFormat, VK_ATTACHMENT_LOAD_OP_LOAD, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        if (!this->m_passes[Pass_Load]) {
            vkDestroyRenderPass(this->m_device, this->m_passes[Pass_Clear], nullptr);
            this->m_passes[Pass_Clear] = VK_NULL_HANDLE;
            this->m_colorFormat = previousColor;
            this->DestroySwapchain();
            return 0;
        }
    }

    uint32_t count = 0;
    vkGetSwapchainImagesKHR(this->m_device, this->m_swapchain, &count, nullptr);
    this->m_swapImages.resize(count);
    vkGetSwapchainImagesKHR(this->m_device, this->m_swapchain, &count, this->m_swapImages.data());

    VkImageCreateInfo depthInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    depthInfo.imageType = VK_IMAGE_TYPE_2D;
    depthInfo.format = this->m_depthFormat;
    depthInfo.extent = { extent.width, extent.height, 1 };
    depthInfo.mipLevels = 1;
    depthInfo.arrayLayers = 1;
    depthInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    depthInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    depthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    if (!this->CreateImage(depthInfo, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_VIEW_TYPE_2D, {}, this->m_depth)) {
        // ʕ ●ᴥ●ʔ✿ Don't keep a swapchain that has no depth target or framebuffers ✿ ʕ ●ᴥ●ʔ
        this->DestroySwapchain();
        return 0;
    }

    for (uint32_t i = 0; i < count; i++) {
        VkImageViewCreateInfo viewInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        viewInfo.image = this->m_swapImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = this->m_colorFormat;
        viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

        VkImageView view;
        if (vkCreateImageView(this->m_device, &viewInfo, nullptr, &view) != VK_SUCCESS) {
            this->DestroySwapchain();
            return 0;
        }
        this->m_swapViews.push_back(view);

        VkImageView attachments[2] = { view, this->m_depth.view };

        VkFramebufferCreateInfo framebufferInfo = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        framebufferInfo.renderPass = this->m_passes[Pass_Clear];
        framebufferInfo.attachmentCount = 2;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = extent.width;
        framebufferInfo.height = extent.height;
        framebufferInfo.layers = 1;

        VkFramebuffer framebuffer;
        if (vkCreateFramebuffer(this->m_device, &framebufferInfo, nullptr, &framebuffer) != VK_SUCCESS) {
            this->DestroySwapchain();
            return 0;
        }
        this->m_swapFramebuffers.push_back(framebuffer);

        VkSemaphoreCreateInfo semaphoreInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VkSemaphore semaphore;
        if (vkCreateSemaphore(this->m_device, &semaphoreInfo, nullptr, &semaphore) != VK_SUCCESS) {
            this->DestroySwapchain();
            return 0;
        }
        this->m_renderFinished.push_back(semaphore);
    }

    return 1;
}

void VkContext::DestroySwapchain() {
    for (VkFramebuffer framebuffer : this->m_swapFramebuffers) {
        vkDestroyFramebuffer(this->m_device, framebuffer, nullptr);
    }

    for (VkImageView view : this->m_swapViews) {
        vkDestroyImageView(this->m_device, view, nullptr);
    }

    this->m_swapFramebuffers.clear();
    this->m_swapViews.clear();
    this->m_swapImages.clear();

    if (this->m_depth.image) {
        vkDestroyImageView(this->m_device, this->m_depth.view, nullptr);
        vkDestroyImage(this->m_device, this->m_depth.image, nullptr);
        this->m_heap.Free(this->m_depth.alloc);
        this->m_depth = {};
    }

    if (this->m_swapchain) {
        vkDestroySwapchainKHR(this->m_device, this->m_swapchain, nullptr);
        this->m_swapchain = VK_NULL_HANDLE;
    }

    // ʕ •ᴥ•ʔ✿ Present semaphores are destroyed only after the swapchain they served ✿ ʕ •ᴥ•ʔ
    for (VkSemaphore semaphore : this->m_renderFinished) {
        vkDestroySemaphore(this->m_device, semaphore, nullptr);
    }
    this->m_renderFinished.clear();
}

void VkContext::DestroyDevice() {
    if (!this->m_device) {
        return;
    }

    vkDeviceWaitIdle(this->m_device);
    this->m_residentUploads.clear();
    this->DestroySwapchain();

    for (VkFrame& frame : this->m_frames) {
        this->Collect(frame);
        this->DestroyBuffer(frame.uniforms);
        this->DestroyBuffer(frame.bones);
        this->DestroyBuffer(frame.items);
        this->DestroyBuffer(frame.indirect);
        for (VkBufferAlloc& chunk : frame.stagingChunks) this->DestroyBuffer(chunk);
        for (VkBufferAlloc& chunk : frame.streamChunks) this->DestroyBuffer(chunk);
        for (VkDescriptorPool pool : frame.descriptorPools) vkDestroyDescriptorPool(this->m_device, pool, nullptr);
        vkDestroySemaphore(this->m_device, frame.imageAvailable, nullptr);
        vkDestroyFence(this->m_device, frame.fence, nullptr);
        frame = {};
    }

    for (VkBufferAlloc& spare : this->m_spareBuffers) {
        this->DestroyBuffer(spare);
    }
    this->m_spareBuffers.clear();

    for (VkRenderPass& pass : this->m_passes) {
        if (pass) {
            vkDestroyRenderPass(this->m_device, pass, nullptr);
            pass = VK_NULL_HANDLE;
        }
    }

    vkDestroyCommandPool(this->m_device, this->m_commandPool, nullptr);
    this->m_heap.Destroy(this->m_device);
    vkDestroyDevice(this->m_device, nullptr);

    this->m_device = VK_NULL_HANDLE;
    this->m_commandPool = VK_NULL_HANDLE;
    this->m_queue = VK_NULL_HANDLE;
    this->m_colorFormat = VK_FORMAT_UNDEFINED;
    this->m_depthFormat = VK_FORMAT_UNDEFINED;
    this->m_frameIndex = 0;
}

void VkContext::DestroyInstance() {
    this->DestroyDevice();
    this->DestroySurface();

    if (this->m_instance) {
        vkDestroyInstance(this->m_instance, nullptr);
        this->m_instance = VK_NULL_HANDLE;
    }
}

VkFrame& VkContext::Frame() {
    return this->m_frames[this->m_frameIndex];
}

void VkContext::PrepareFrame() {
    VkFrame& frame = this->Frame();

    if (frame.prepared) {
        return;
    }

    // ʕ •ᴥ•ʔ✿ Known-signaled and in-flight fences can wait; a stuck reset fence must not ✿ ʕ •ᴥ•ʔ
    uint64_t timeout = frame.fenceUnsignaled ? 100 * 1000000ull : UINT64_MAX;
    if (vkWaitForFences(this->m_device, 1, &frame.fence, VK_TRUE, timeout) == VK_SUCCESS) {
        frame.fenceUnsignaled = 0;
        // ʕ •ᴥ•ʔ✿ Retired buffers stay live until this fence signals; a timeout means earlier submits may still sample them ✿ ʕ •ᴥ•ʔ
        this->Collect(frame);
    }
    for (VkDescriptorPool pool : frame.descriptorPools) vkResetDescriptorPool(this->m_device, pool, 0);
    frame.descriptorPoolCursor = 0;
    frame.sets.clear();
    frame.uniformOffset = 0;
    frame.boneCursor = 0;
    frame.itemCursor = 0;
    frame.indirectCursor = 0;
    frame.stagingChunk = 0;
    frame.stagingOffset = 0;
    frame.streamChunk = 0;
    frame.streamOffset = 0;
    frame.acquired = 0;
    frame.imageWaited = 0;
    frame.prepared = 1;
}

VkDescriptorPool VkContext::CreateDescriptorPool() {
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, DescriptorSetsPerFrame },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, DescriptorSetsPerFrame * 2 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DescriptorSetsPerFrame * 2 },
    };
    VkDescriptorPoolCreateInfo info = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    info.maxSets = DescriptorSetsPerFrame;
    info.poolSizeCount = 3;
    info.pPoolSizes = poolSizes;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(this->m_device, &info, nullptr, &pool) != VK_SUCCESS) {
        pool = VK_NULL_HANDLE;
    }
    return pool;
}

VkDescriptorSet VkContext::AllocateSet(VkDescriptorSetLayout layout) {
    VkFrame& frame = this->Frame();
    for (;;) {
        if (frame.descriptorPoolCursor >= frame.descriptorPools.size()) {
            if (frame.descriptorPools.size() >= 16) return VK_NULL_HANDLE;
            VkDescriptorPool pool = this->CreateDescriptorPool();
            if (!pool) return VK_NULL_HANDLE;
            frame.descriptorPools.push_back(pool);
        }
        VkDescriptorSetAllocateInfo info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        info.descriptorPool = frame.descriptorPools[frame.descriptorPoolCursor];
        info.descriptorSetCount = 1;
        info.pSetLayouts = &layout;
        VkDescriptorSet set = VK_NULL_HANDLE;
        VkResult result = vkAllocateDescriptorSets(this->m_device, &info, &set);
        if (result == VK_SUCCESS) return set;
        if (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL) return VK_NULL_HANDLE;
        frame.descriptorPoolCursor++;
    }
}

VkDescriptorSet VkContext::CachedSet(const VkSetKey& key, VkDescriptorSetLayout layout, int32_t& created) {
    VkFrame& frame = this->Frame();
    auto it = frame.sets.find(key);
    if (it != frame.sets.end()) {
        created = 0;
        return it->second;
    }
    VkDescriptorSet set = this->AllocateSet(layout);
    if (set) frame.sets.emplace(key, set);
    created = set ? 1 : 0;
    return set;
}

// ʕ ◕ᴥ◕ ʔ✿ While invisible, submit pending uploads so garbage and staging don't grow without bound ✿ ʕ ◕ᴥ◕ ʔ
void VkContext::FlushUploads() {
    VkFrame& frame = this->Frame();
    if (!frame.prepared || frame.acquired || frame.recording) return;
    this->Submit(0);
    frame.prepared = 0;
    this->m_frameIndex = (this->m_frameIndex + 1) % FramesInFlight;
}

int32_t VkContext::BeginFrame() {
    this->PrepareFrame();
    VkFrame& frame = this->Frame();

    // ʕ •ᴥ•ʔ✿ Null swapchain is a recreate, not an acquire ✿ ʕ •ᴥ•ʔ
    if (!this->m_swapchain) {
        return 0;
    }

    VkResult result = vkAcquireNextImageKHR(this->m_device, this->m_swapchain, 100 * 1000000ull, frame.imageAvailable, VK_NULL_HANDLE, &this->m_imageIndex);

    if (result == VK_TIMEOUT || result == VK_NOT_READY) {
        return 2;
    }

    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        return 0;
    }

    this->m_suboptimal = result == VK_SUBOPTIMAL_KHR;
    frame.acquired = 1;
    frame.imageWaited = 0;

    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    // ʕ •ᴥ•ʔ✿ A failed begin is not a live command buffer; present the image already acquired ✿ ʕ •ᴥ•ʔ
    if (vkBeginCommandBuffer(frame.cmd, &beginInfo) != VK_SUCCESS) {
        frame.recording = 0;
        // ʕ •ᴥ•ʔ✿ 3 is a clean present; 4 means EndFrame failed so the next scene recreates ✿ ʕ •ᴥ•ʔ
        return this->EndFrame() ? 3 : 4;
    }
    frame.recording = 1;

    return 1;
}

VkCommandBuffer VkContext::UploadCmd() {
    this->PrepareFrame();
    VkFrame& frame = this->Frame();

    if (!frame.uploadRecording) {
        VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        // ʕ •ᴥ•ʔ✿ A failed begin is not a live upload buffer ✿ ʕ •ᴥ•ʔ
        if (vkBeginCommandBuffer(frame.uploadCmd, &beginInfo) != VK_SUCCESS) {
            frame.uploadRecording = 0;
            return VK_NULL_HANDLE;
        }
        frame.uploadRecording = 1;
    }

    return frame.uploadCmd;
}

int32_t VkContext::Submit(int32_t present) {
    VkFrame& frame = this->Frame();
    VkCommandBuffer cmds[2];
    uint32_t cmdCount = 0;

    // ʕ •ᴥ•ʔ✿ A failed end leaves the buffer invalid; omit it and still reach the fence submit ✿ ʕ •ᴥ•ʔ
    if (frame.uploadRecording) {
        if (vkEndCommandBuffer(frame.uploadCmd) == VK_SUCCESS) {
            cmds[cmdCount++] = frame.uploadCmd;
        }
        frame.uploadRecording = 0;
    }

    if (frame.recording) {
        if (vkEndCommandBuffer(frame.cmd) == VK_SUCCESS) {
            cmds[cmdCount++] = frame.cmd;
        }
        frame.recording = 0;
    }

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit.commandBufferCount = cmdCount;
    submit.pCommandBuffers = cmds;

    int32_t attachImageWait = 0;
    if (frame.acquired && !frame.imageWaited) {
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &frame.imageAvailable;
        submit.pWaitDstStageMask = &waitStage;
        attachImageWait = 1;
    }

    if (present) {
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &this->m_renderFinished[this->m_imageIndex];
    }

    vkResetFences(this->m_device, 1, &frame.fence);
    if (vkQueueSubmit(this->m_queue, 1, &submit, frame.fence) == VK_SUCCESS) {
        frame.fenceUnsignaled = 0;
        // ʕ •ᴥ•ʔ✿ imageAvailable is consumed only when this submit succeeds ✿ ʕ •ᴥ•ʔ
        if (attachImageWait) {
            frame.imageWaited = 1;
        }
        return 1;
    }

    // ʕ •ᴥ•ʔ✿ A failed submit leaves the reset fence unsignaled; swap in a signaled fence so the next wait cannot hang ✿ ʕ •ᴥ•ʔ
    VkFenceCreateInfo fenceInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VkFence signaled = VK_NULL_HANDLE;
    if (vkCreateFence(this->m_device, &fenceInfo, nullptr, &signaled) == VK_SUCCESS) {
        vkDestroyFence(this->m_device, frame.fence, nullptr);
        frame.fence = signaled;
        frame.fenceUnsignaled = 0;
    } else {
        frame.fenceUnsignaled = 1;
    }
    return 0;
}

int32_t VkContext::EndFrame() {
    VkFrame& frame = this->Frame();
    int32_t present = frame.acquired;
    int32_t submitted = this->Submit(present);

    VkResult result = VK_SUCCESS;

    if (submitted && present) {
        VkPresentInfoKHR info = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        info.waitSemaphoreCount = 1;
        info.pWaitSemaphores = &this->m_renderFinished[this->m_imageIndex];
        info.swapchainCount = 1;
        info.pSwapchains = &this->m_swapchain;
        info.pImageIndices = &this->m_imageIndex;
        result = vkQueuePresentKHR(this->m_queue, &info);
    }

    frame.prepared = 0;
    // ʕ •ᴥ•ʔ✿ Reusing this slot must not present the image EndFrame already finished ✿ ʕ •ᴥ•ʔ
    frame.acquired = 0;
    this->m_frameIndex = (this->m_frameIndex + 1) % FramesInFlight;

    // ʕ •ᴥ•ʔ✿ Suboptimal still presented. Recreating every frame on a permanently suboptimal surface stalls the game ✿ ʕ •ᴥ•ʔ
    return submitted && (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR);
}

int32_t VkContext::AbandonFrame() {
    VkFrame& frame = this->Frame();
    int32_t submitted = this->Submit(0);
    // ʕ •ᴥ•ʔ✿ A failed submit can leave the reset fence unsignaled when the signaled replacement cannot be created ✿ ʕ •ᴥ•ʔ
    uint64_t timeout = submitted ? UINT64_MAX : 100 * 1000000ull;
    vkWaitForFences(this->m_device, 1, &frame.fence, VK_TRUE, timeout);
    // ʕ ◕ᴥ◕ ʔ✿ The next BeginFrame must reset rings/pools and must not wait on an unpresented image ✿ ʕ ◕ᴥ◕ ʔ
    frame.prepared = 0;
    frame.acquired = 0;
    return submitted;
}

void VkContext::FlushFrame() {
    VkFrame& frame = this->Frame();
    // ʕ ●ᴥ●ʔ✿ Preserve the acquired image across the reset, or it is never presented and stalls acquire ✿ ʕ ●ᴥ●ʔ
    int32_t acquired = frame.acquired;
    int32_t alreadyWaited = frame.imageWaited;
    int32_t submitted = this->AbandonFrame();
    this->PrepareFrame();
    frame.acquired = acquired;
    // ʕ •ᴥ•ʔ✿ A failed abandon submit did not consume imageAvailable; an earlier successful wait stays consumed ✿ ʕ •ᴥ•ʔ
    frame.imageWaited = alreadyWaited || (submitted && acquired);

    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    frame.recording = vkBeginCommandBuffer(frame.cmd, &beginInfo) == VK_SUCCESS;
}

void VkContext::ChooseHeaps() {
    this->m_uploadType = UINT32_MAX;
    this->m_rebarType = UINT32_MAX;
    this->m_localType = UINT32_MAX;
    this->m_rebarHeap = 0;
    this->m_localHeap = 0;

    VkDeviceSize bestLocal = 0;

    for (uint32_t i = 0; i < this->m_memoryProperties.memoryTypeCount; i++) {
        const VkMemoryType& type = this->m_memoryProperties.memoryTypes[i];
        VkDeviceSize heap = this->m_memoryProperties.memoryHeaps[type.heapIndex].size;
        VkMemoryPropertyFlags flags = type.propertyFlags;
        int32_t visible = (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
        int32_t local = (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
        int32_t coherent = (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;

        if (visible && coherent && !local && this->m_uploadType == UINT32_MAX) {
            this->m_uploadType = i;
        }

        // ʕ •ᴥ•ʔ✿ 256MB is the legacy BAR window. Larger means the CPU can see the VRAM heap ✿ ʕ •ᴥ•ʔ
        if (visible && coherent && local && heap > (256ull << 20) && heap > this->m_rebarHeap) {
            this->m_rebarHeap = heap;
            this->m_rebarType = i;
        }

        if (local && !visible && heap >= bestLocal) {
            bestLocal = heap;
            this->m_localType = i;
        }
    }

    this->m_localHeap = bestLocal;

    if (this->m_uploadType == UINT32_MAX) {
        this->m_uploadType = this->FindMemoryType(0xFFFFFFFFu, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }

    std::fprintf(stderr, "vulkan memory: rebar=%s heap=%lluMiB uploadType=%u localType=%u\n",
        this->m_rebarType == UINT32_MAX ? "off" : "on",
        static_cast<unsigned long long>(this->m_rebarHeap >> 20),
        this->m_uploadType,
        this->m_localType);
}

uint32_t VkContext::FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags flags) {
    for (uint32_t i = 0; i < this->m_memoryProperties.memoryTypeCount; i++) {
        if ((typeBits & (1u << i)) && (this->m_memoryProperties.memoryTypes[i].propertyFlags & flags) == flags) {
            return i;
        }
    }

    return UINT32_MAX;
}

int32_t VkContext::AcquireBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBufferAlloc& out) {
    for (size_t i = 0; i < this->m_spareBuffers.size(); i++) {
        if (this->m_spareBuffers[i].size == size && this->m_spareBuffers[i].usage == usage) {
            out = this->m_spareBuffers[i];
            this->m_spareBuffers[i] = this->m_spareBuffers.back();
            this->m_spareBuffers.pop_back();
            return 1;
        }
    }
    return this->CreateBuffer(size, usage, out);
}

int32_t VkContext::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBufferAlloc& out) {
    out = {};
    out.size = size;
    out.usage = usage;

    VkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(this->m_device, &info, nullptr, &out.buffer) != VK_SUCCESS) {
        return 0;
    }

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(this->m_device, out.buffer, &requirements);

    VkMemoryAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.allocationSize = requirements.size;

    int32_t bounce = (usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) &&
        !(usage & (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT));
    int32_t readback = usage == VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    uint32_t memoryType = UINT32_MAX;

    if (!bounce && !readback && this->m_rebarType != UINT32_MAX && (requirements.memoryTypeBits & (1u << this->m_rebarType))) {
        memoryType = this->m_rebarType;
    }

    if (memoryType == UINT32_MAX || !(requirements.memoryTypeBits & (1u << memoryType))) {
        memoryType = (this->m_uploadType != UINT32_MAX && (requirements.memoryTypeBits & (1u << this->m_uploadType)))
            ? this->m_uploadType
            : this->FindMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }

    allocInfo.memoryTypeIndex = memoryType;

    if (allocInfo.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(this->m_device, &allocInfo, nullptr, &out.memory) != VK_SUCCESS) {
        if (memoryType == this->m_rebarType && this->m_uploadType != UINT32_MAX && (requirements.memoryTypeBits & (1u << this->m_uploadType))) {
            allocInfo.memoryTypeIndex = this->m_uploadType;
            if (vkAllocateMemory(this->m_device, &allocInfo, nullptr, &out.memory) != VK_SUCCESS) {
                vkDestroyBuffer(this->m_device, out.buffer, nullptr);
                out = {};
                return 0;
            }
        } else {
            vkDestroyBuffer(this->m_device, out.buffer, nullptr);
            out = {};
            return 0;
        }
    }

    if (vkBindBufferMemory(this->m_device, out.buffer, out.memory, 0) != VK_SUCCESS ||
        vkMapMemory(this->m_device, out.memory, 0, VK_WHOLE_SIZE, 0, reinterpret_cast<void**>(&out.mapped)) != VK_SUCCESS) {
        this->DestroyBuffer(out);
        return 0;
    }

    return 1;
}

int32_t VkContext::BumpChunk(std::vector<VkBufferAlloc>& chunks, uint32_t& chunkIndex, VkDeviceSize& cursor, VkDeviceSize chunkSize, VkBufferUsageFlags usage, uint32_t align, uint32_t bytes, uint32_t& index, VkBufferAlloc& out) {
    if (!align || !bytes || !chunkSize) {
        return 0;
    }

    if (bytes > chunkSize) {
        if (!this->CreateBuffer(bytes, usage, out)) {
            return 0;
        }
        index = 0;
        return 1;
    }

    for (;;) {
        if (chunkIndex >= chunks.size()) {
            VkBufferAlloc chunk = {};
            if (!this->CreateBuffer(chunkSize, usage, chunk)) {
                return 0;
            }
            chunks.push_back(chunk);
        }

        VkBufferAlloc& chunk = chunks[chunkIndex];
        uint64_t aligned = (static_cast<uint64_t>(align) + cursor - 1) - (static_cast<uint64_t>(align) + cursor - 1) % align;

        if (aligned + bytes <= chunk.size) {
            cursor = aligned + bytes;
            out = chunk;
            out.borrowed = 1;
            index = static_cast<uint32_t>(aligned);
            return 1;
        }

        chunkIndex++;
        cursor = 0;
    }
}

int32_t VkContext::AllocDrawItems(uint32_t count, uint32_t& first, GpuDrawItem*& mapped) {
    this->PrepareFrame();
    VkFrame& frame = this->Frame();
    uint32_t capacity = frame.items.mapped ? static_cast<uint32_t>(frame.items.size / sizeof(GpuDrawItem)) : 0;

    if (!count || frame.itemCursor + count > capacity) {
        return 0;
    }

    first = frame.itemCursor;
    mapped = reinterpret_cast<GpuDrawItem*>(frame.items.mapped) + frame.itemCursor;
    frame.itemCursor += count;
    return 1;
}

int32_t VkContext::AllocIndirect(uint32_t count, VkDeviceSize& offset, VkDrawIndexedIndirectCommand*& mapped) {
    this->PrepareFrame();
    VkFrame& frame = this->Frame();
    VkDeviceSize bytes = static_cast<VkDeviceSize>(count) * sizeof(VkDrawIndexedIndirectCommand);

    if (!count || !frame.indirect.mapped || frame.indirectCursor + bytes > frame.indirect.size) {
        return 0;
    }

    offset = frame.indirectCursor;
    mapped = reinterpret_cast<VkDrawIndexedIndirectCommand*>(frame.indirect.mapped + frame.indirectCursor);
    frame.indirectCursor += bytes;
    return 1;
}

int32_t VkContext::StreamLock(uint32_t align, uint32_t bytes, uint32_t& index, VkBufferAlloc& out) {
    this->PrepareFrame();
    VkFrame& frame = this->Frame();
    return this->BumpChunk(frame.streamChunks, frame.streamChunk, frame.streamOffset, StreamRingSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, align, bytes, index, out);
}

void VkContext::DestroyBuffer(VkBufferAlloc& buffer) {
    if (buffer.shadow && buffer.mapped) {
        std::free(buffer.mapped);
        buffer.mapped = nullptr;
    }

    if (buffer.buffer) {
        vkDestroyBuffer(this->m_device, buffer.buffer, nullptr);
        if (buffer.memory) {
            vkFreeMemory(this->m_device, buffer.memory, nullptr);
        }
    }

    buffer = {};
}

int32_t VkContext::AllocateTypedBuffer(VkDeviceSize size, VkBufferUsageFlags usage, uint32_t memoryType, int32_t map, VkBufferAlloc& out) {
    out = {};
    out.size = size;
    out.usage = usage;

    if (!size || memoryType == UINT32_MAX) {
        return 0;
    }

    VkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(this->m_device, &info, nullptr, &out.buffer) != VK_SUCCESS) {
        out = {};
        return 0;
    }

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(this->m_device, out.buffer, &requirements);

    if (!(requirements.memoryTypeBits & (1u << memoryType))) {
        this->DestroyBuffer(out);
        return 0;
    }

    VkMemoryAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex = memoryType;

    if (vkAllocateMemory(this->m_device, &allocInfo, nullptr, &out.memory) != VK_SUCCESS ||
        vkBindBufferMemory(this->m_device, out.buffer, out.memory, 0) != VK_SUCCESS) {
        this->DestroyBuffer(out);
        return 0;
    }

    if (map && vkMapMemory(this->m_device, out.memory, 0, VK_WHOLE_SIZE, 0, reinterpret_cast<void**>(&out.mapped)) != VK_SUCCESS) {
        this->DestroyBuffer(out);
        return 0;
    }

    return 1;
}

int32_t VkContext::CreateResidentBuffer(VkDeviceSize size, VkBufferAlloc& out) {
    VkBufferUsageFlags hostUsage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    VkBufferUsageFlags gpuUsage = hostUsage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    // ʕ •ᴥ•ʔ✿ ReBAR is the VRAM heap and the CPU can write it. No second copy ✿ ʕ •ᴥ•ʔ
    if (this->m_rebarType != UINT32_MAX && this->AllocateTypedBuffer(size, hostUsage, this->m_rebarType, 1, out)) {
        return 1;
    }

    if (this->m_localType != UINT32_MAX && this->AllocateTypedBuffer(size, gpuUsage, this->m_localType, 0, out)) {
        out.mapped = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(size)));
        if (out.mapped) {
            out.shadow = 1;
            return 1;
        }
        this->DestroyBuffer(out);
    }

    return this->CreateBuffer(size, hostUsage, out);
}

void VkContext::QueueResidentUpload(VkBuffer buffer, const uint8_t* src, VkDeviceSize offset, VkDeviceSize size) {
    if (!buffer || !src || !size) {
        return;
    }

    this->m_residentUploads.push_back({ buffer, src, offset, size });
}

void VkContext::FlushResidentUploads(VkCommandBuffer cmd) {
    if (!cmd || this->m_residentUploads.empty()) {
        return;
    }

    std::vector<ResidentCopy> pending;
    pending.swap(this->m_residentUploads);
    std::sort(pending.begin(), pending.end(), [](const ResidentCopy& a, const ResidentCopy& b) {
        if (a.buffer != b.buffer) {
            return a.buffer < b.buffer;
        }
        return a.offset < b.offset;
    });

    std::vector<ResidentCopy> merged;
    merged.reserve(pending.size());

    for (const ResidentCopy& copy : pending) {
        if (!merged.empty()) {
            ResidentCopy& tail = merged.back();
            if (tail.buffer == copy.buffer && tail.offset + tail.size == copy.offset && tail.src + tail.size == copy.src) {
                tail.size += copy.size;
                continue;
            }
        }
        merged.push_back(copy);
    }

    std::vector<VkBuffer> touched;
    size_t doneCount = 0;

    for (; doneCount < merged.size(); ++doneCount) {
        const ResidentCopy& copy = merged[doneCount];
        VkDeviceSize done = 0;
        int32_t ok = 1;

        while (done < copy.size) {
            VkDeviceSize bytes = copy.size - done;
            const VkDeviceSize slice = 8ull << 20;

            if (bytes > slice) {
                bytes = slice;
            }

            VkStaging staging = this->Stage(bytes);

            if (!staging.buffer || !staging.data) {
                ok = 0;
                break;
            }

            std::memcpy(staging.data, copy.src + done, static_cast<size_t>(bytes));

            VkBufferMemoryBarrier host = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
            host.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
            host.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            host.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            host.buffer = staging.buffer;
            host.offset = staging.offset;
            host.size = bytes;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &host, 0, nullptr);

            VkBufferCopy region = { staging.offset, copy.offset + done, bytes };
            vkCmdCopyBuffer(cmd, staging.buffer, copy.buffer, 1, &region);
            done += bytes;
        }

        if (!ok) {
            if (done && (touched.empty() || touched.back() != copy.buffer)) {
                touched.push_back(copy.buffer);
            }

            ResidentCopy rest = copy;
            rest.src += done;
            rest.offset += done;
            rest.size -= done;

            if (rest.size) {
                this->m_residentUploads.push_back(rest);
            }
            break;
        }

        if (touched.empty() || touched.back() != copy.buffer) {
            touched.push_back(copy.buffer);
        }
    }

    for (size_t i = doneCount + 1; i < merged.size(); ++i) {
        this->m_residentUploads.push_back(merged[i]);
    }

    if (touched.empty()) {
        return;
    }

    std::vector<VkBufferMemoryBarrier> barriers(touched.size());

    for (size_t i = 0; i < touched.size(); ++i) {
        VkBufferMemoryBarrier& barrier = barriers[i];
        barrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = touched[i];
        barrier.offset = 0;
        barrier.size = VK_WHOLE_SIZE;
    }

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 0, nullptr, static_cast<uint32_t>(barriers.size()), barriers.data(), 0, nullptr);
}

int32_t VkContext::CreateImage(const VkImageCreateInfo& info, VkImageAspectFlags aspect, VkImageViewType viewType, const VkComponentMapping& swizzle, VkImageAlloc& out) {
    out = {};
    out.format = info.format;

    if (vkCreateImage(this->m_device, &info, nullptr, &out.image) != VK_SUCCESS) {
        return 0;
    }

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(this->m_device, out.image, &requirements);

    uint32_t memoryType = UINT32_MAX;

    if (this->m_localType != UINT32_MAX && (requirements.memoryTypeBits & (1u << this->m_localType))) {
        memoryType = this->m_localType;
    } else {
        for (uint32_t i = 0; i < this->m_memoryProperties.memoryTypeCount; i++) {
            const VkMemoryType& type = this->m_memoryProperties.memoryTypes[i];

            if ((requirements.memoryTypeBits & (1u << i)) &&
                (type.propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) &&
                !(type.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
                memoryType = i;
                break;
            }
        }
    }

    if (memoryType == UINT32_MAX) {
        memoryType = this->FindMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    }

    if (memoryType == UINT32_MAX || !this->m_heap.Alloc(this->m_device, requirements, memoryType, out.alloc)) {
        vkDestroyImage(this->m_device, out.image, nullptr);
        out = {};
        return 0;
    }

    VkImageViewCreateInfo viewInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewInfo.image = out.image;
    viewInfo.viewType = viewType;
    viewInfo.format = info.format;
    viewInfo.components = swizzle;
    viewInfo.subresourceRange = { aspect, 0, info.mipLevels, 0, info.arrayLayers };

    if (vkBindImageMemory(this->m_device, out.image, out.alloc.memory, out.alloc.offset) != VK_SUCCESS ||
        vkCreateImageView(this->m_device, &viewInfo, nullptr, &out.view) != VK_SUCCESS) {
        vkDestroyImage(this->m_device, out.image, nullptr);
        this->m_heap.Free(out.alloc);
        out = {};
        return 0;
    }

    return 1;
}

VkStaging VkContext::Stage(VkDeviceSize size) {
    if (size > static_cast<VkDeviceSize>(UINT32_MAX)) {
        return {};
    }

    VkFrame& frame = this->Frame();
    uint32_t index = 0;
    VkBufferAlloc chunk = {};

    if (!this->BumpChunk(frame.stagingChunks, frame.stagingChunk, frame.stagingOffset, StagingRingSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 16, static_cast<uint32_t>(size), index, chunk)) {
        return {};
    }

    if (!chunk.borrowed) {
        VkStaging staging = { chunk.buffer, 0, chunk.mapped };
        this->RetireBuffer(chunk);
        return staging;
    }

    return { chunk.buffer, index, chunk.mapped + index };
}

void VkContext::Retire(const VkGarbage& garbage) {
    this->PrepareFrame();
    this->Frame().garbage.push_back(garbage);
}

void VkContext::RetireBuffer(VkBufferAlloc& buffer) {
    if (buffer.borrowed) {
        buffer = {};
        return;
    }

    if (buffer.buffer) {
        VkGarbage garbage = {};
        garbage.buffer = buffer.buffer;
        garbage.memory = buffer.memory;
        garbage.mapped = buffer.mapped;
        garbage.size = buffer.size;
        garbage.usage = buffer.usage;
        garbage.shadow = buffer.shadow;
        this->Retire(garbage);
    }

    buffer.buffer = VK_NULL_HANDLE;
    buffer.memory = VK_NULL_HANDLE;
    buffer.mapped = nullptr;
    buffer.shadow = 0;
}

void VkContext::RetireImage(VkImageAlloc& image) {
    if (image.image) {
        VkGarbage garbage = {};
        garbage.image = image.image;
        garbage.view = image.view;
        garbage.alloc = image.alloc;
        this->Retire(garbage);
    }

    image = {};
}

void VkContext::Collect(VkFrame& frame) {
    for (const VkGarbage& garbage : frame.garbage) {
        if (garbage.framebuffer) {
            vkDestroyFramebuffer(this->m_device, garbage.framebuffer, nullptr);
        }

        if (garbage.view) {
            vkDestroyImageView(this->m_device, garbage.view, nullptr);
        }

        if (garbage.image) {
            vkDestroyImage(this->m_device, garbage.image, nullptr);
        }

        // ʕ ◕ᴥ◕ ʔ✿ Only stream-pool buffers recycle; staging temps would pin hundreds of MB ✿ ʕ ◕ᴥ◕ ʔ
        bool recyclable = garbage.buffer && garbage.mapped && !garbage.shadow &&
            garbage.usage == (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        bool pushed = false;
        if (recyclable) {
            uint64_t spareBytes = 0;
            for (auto& spare : this->m_spareBuffers) spareBytes += spare.size;
            while (!this->m_spareBuffers.empty() && (spareBytes + garbage.size > MaxSpareBytes || this->m_spareBuffers.size() >= MaxSpareBuffers)) {
                spareBytes -= this->m_spareBuffers.front().size;
                this->DestroyBuffer(this->m_spareBuffers.front());
                this->m_spareBuffers.erase(this->m_spareBuffers.begin());
            }
            if (spareBytes + garbage.size <= MaxSpareBytes) {
                this->m_spareBuffers.push_back({ garbage.buffer, garbage.memory, garbage.mapped, garbage.size, garbage.usage });
                pushed = true;
            }
        }
        if (!pushed) {
            if (garbage.shadow && garbage.mapped) {
                std::free(garbage.mapped);
            }

            if (garbage.buffer) {
                vkDestroyBuffer(this->m_device, garbage.buffer, nullptr);
            }

            if (garbage.memory) {
                vkFreeMemory(this->m_device, garbage.memory, nullptr);
            }
        }

        if (garbage.alloc.memory) {
            this->m_heap.Free(garbage.alloc);
        }
    }

    frame.garbage.clear();
}

void VkContext::Barrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, uint32_t levels, VkImageLayout oldLayout, VkImageLayout newLayout) {
    if (oldLayout == newLayout) {
        return;
    }

    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkAccessFlags srcAccess = 0;
    VkAccessFlags dstAccess = 0;

    switch (oldLayout) {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            break;

        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            srcAccess = VK_ACCESS_SHADER_READ_BIT;
            break;

        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            srcAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
            break;

        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            srcAccess = VK_ACCESS_TRANSFER_READ_BIT;
            break;

        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            srcAccess = VK_ACCESS_MEMORY_READ_BIT;
            break;

        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            srcAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            break;

        default:
            srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            srcAccess = VK_ACCESS_MEMORY_WRITE_BIT;
            break;
    }

    switch (newLayout) {
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            dstAccess = VK_ACCESS_SHADER_READ_BIT;
            break;

        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dstAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
            break;

        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dstAccess = VK_ACCESS_TRANSFER_READ_BIT;
            break;

        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            break;

        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dstAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            break;

        default:
            dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            dstAccess = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            break;
    }

    VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = { aspect, 0, levels, 0, VK_REMAINING_ARRAY_LAYERS };

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}
