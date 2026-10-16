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
    app.apiVersion = VK_API_VERSION_1_1;

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

    VkPhysicalDeviceFeatures supported;
    vkGetPhysicalDeviceFeatures(this->m_physicalDevice, &supported);
    this->m_features.textureCompressionBC = supported.textureCompressionBC;
    this->m_features.samplerAnisotropy = supported.samplerAnisotropy;
    this->m_features.fillModeNonSolid = supported.fillModeNonSolid;
    this->m_features.largePoints = supported.largePoints;

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

    VkDeviceCreateInfo info = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
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
    if (vkCreateCommandPool(this->m_device, &poolInfo, nullptr, &this->m_commandPool) != VK_SUCCESS) return 0;

    for (VkFrame& frame : this->m_frames) {
        VkCommandBufferAllocateInfo cmdInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        cmdInfo.commandPool = this->m_commandPool;
        cmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdInfo.commandBufferCount = 2;

        VkCommandBuffer cmds[2];
        if (vkAllocateCommandBuffers(this->m_device, &cmdInfo, cmds) != VK_SUCCESS) return 0;
        frame.cmd = cmds[0];
        frame.uploadCmd = cmds[1];

        VkFenceCreateInfo fenceInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        if (vkCreateFence(this->m_device, &fenceInfo, nullptr, &frame.fence) != VK_SUCCESS) return 0;

        VkSemaphoreCreateInfo semaphoreInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        if (vkCreateSemaphore(this->m_device, &semaphoreInfo, nullptr, &frame.imageAvailable) != VK_SUCCESS) return 0;

        VkDescriptorPool pool = this->CreateDescriptorPool();
        if (!pool) return 0;
        frame.descriptorPools.push_back(pool);

        if (!this->CreateBuffer(UniformRingSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, frame.uniforms) || !this->CreateBuffer(StagingRingSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, frame.staging)) {
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
    vkCreateRenderPass(this->m_device, &info, nullptr, &pass);
    return pass;
}

int32_t VkContext::CreateSwapchain(uint32_t width, uint32_t height, int32_t vsync) {
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

    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;

    if (!vsync) {
        for (VkPresentModeKHR mode : modes) {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                presentMode = mode;
                break;
            }

            if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                presentMode = mode;
            }
        }
    }

    uint32_t imageCount = caps.minImageCount + 1;

    if (caps.maxImageCount && imageCount > caps.maxImageCount) {
        imageCount = caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR info = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    info.surface = this->m_surface;
    info.minImageCount = imageCount;
    info.imageFormat = surfaceFormat.format;
    info.imageColorSpace = surfaceFormat.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (caps.supportedUsageFlags & (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT));
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR : static_cast<VkCompositeAlphaFlagBitsKHR>(caps.supportedCompositeAlpha & -caps.supportedCompositeAlpha);
    info.presentMode = presentMode;
    info.clipped = VK_TRUE;
    info.oldSwapchain = this->m_swapchain;

    VkSwapchainKHR swapchain;

    if (vkCreateSwapchainKHR(this->m_device, &info, nullptr, &swapchain) != VK_SUCCESS) {
        return 0;
    }

    this->DestroySwapchain();
    this->m_swapchain = swapchain;
    this->m_extent = extent;

    if (this->m_colorFormat != surfaceFormat.format) {
        for (uint32_t pass = Pass_Clear; pass <= Pass_Load; pass++) {
            if (this->m_passes[pass]) {
                vkDestroyRenderPass(this->m_device, this->m_passes[pass], nullptr);
            }
        }

        this->m_colorFormat = surfaceFormat.format;
        this->m_passes[Pass_Clear] = this->CreateRenderPass(this->m_colorFormat, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_UNDEFINED);
        this->m_passes[Pass_Load] = this->CreateRenderPass(this->m_colorFormat, VK_ATTACHMENT_LOAD_OP_LOAD, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
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
    this->DestroySwapchain();

    for (VkFrame& frame : this->m_frames) {
        this->Collect(frame);
        this->DestroyBuffer(frame.uniforms);
        this->DestroyBuffer(frame.staging);
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

    vkWaitForFences(this->m_device, 1, &frame.fence, VK_TRUE, UINT64_MAX);
    this->Collect(frame);
    for (VkDescriptorPool pool : frame.descriptorPools) vkResetDescriptorPool(this->m_device, pool, 0);
    frame.descriptorPoolCursor = 0;
    frame.sets.clear();
    frame.uniformOffset = 0;
    frame.stagingOffset = 0;
    frame.acquired = 0;
    frame.imageWaited = 0;
    frame.prepared = 1;
}

VkDescriptorPool VkContext::CreateDescriptorPool() {
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, DescriptorSetsPerFrame },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, DescriptorSetsPerFrame * 2 },
    };
    VkDescriptorPoolCreateInfo info = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    info.maxSets = DescriptorSetsPerFrame;
    info.poolSizeCount = 2;
    info.pPoolSizes = poolSizes;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    vkCreateDescriptorPool(this->m_device, &info, nullptr, &pool);
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
    vkBeginCommandBuffer(frame.cmd, &beginInfo);
    frame.recording = 1;

    return 1;
}

VkCommandBuffer VkContext::UploadCmd() {
    this->PrepareFrame();
    VkFrame& frame = this->Frame();

    if (!frame.uploadRecording) {
        VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(frame.uploadCmd, &beginInfo);
        frame.uploadRecording = 1;
    }

    return frame.uploadCmd;
}

void VkContext::Submit(int32_t present) {
    VkFrame& frame = this->Frame();
    VkCommandBuffer cmds[2];
    uint32_t cmdCount = 0;

    if (frame.uploadRecording) {
        vkEndCommandBuffer(frame.uploadCmd);
        cmds[cmdCount++] = frame.uploadCmd;
        frame.uploadRecording = 0;
    }

    if (frame.recording) {
        vkEndCommandBuffer(frame.cmd);
        cmds[cmdCount++] = frame.cmd;
        frame.recording = 0;
    }

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit.commandBufferCount = cmdCount;
    submit.pCommandBuffers = cmds;

    if (frame.acquired && !frame.imageWaited) {
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &frame.imageAvailable;
        submit.pWaitDstStageMask = &waitStage;
        frame.imageWaited = 1;
    }

    if (present) {
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &this->m_renderFinished[this->m_imageIndex];
    }

    vkResetFences(this->m_device, 1, &frame.fence);
    vkQueueSubmit(this->m_queue, 1, &submit, frame.fence);
}

int32_t VkContext::EndFrame() {
    VkFrame& frame = this->Frame();
    int32_t present = frame.acquired;
    this->Submit(present);

    VkResult result = VK_SUCCESS;

    if (present) {
        VkPresentInfoKHR info = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        info.waitSemaphoreCount = 1;
        info.pWaitSemaphores = &this->m_renderFinished[this->m_imageIndex];
        info.swapchainCount = 1;
        info.pSwapchains = &this->m_swapchain;
        info.pImageIndices = &this->m_imageIndex;
        result = vkQueuePresentKHR(this->m_queue, &info);
    }

    frame.prepared = 0;
    this->m_frameIndex = (this->m_frameIndex + 1) % FramesInFlight;

    return result == VK_SUCCESS && !this->m_suboptimal;
}

void VkContext::AbandonFrame() {
    VkFrame& frame = this->Frame();
    this->Submit(0);
    vkWaitForFences(this->m_device, 1, &frame.fence, VK_TRUE, UINT64_MAX);
    // ʕ ◕ᴥ◕ ʔ✿ The next BeginFrame must reset rings/pools and must not wait on an unpresented image ✿ ʕ ◕ᴥ◕ ʔ
    frame.prepared = 0;
    frame.acquired = 0;
}

void VkContext::FlushFrame() {
    VkFrame& frame = this->Frame();
    // ʕ ●ᴥ●ʔ✿ Preserve the acquired image across the reset, or it is never presented and stalls acquire ✿ ʕ ●ᴥ●ʔ
    int32_t acquired = frame.acquired;
    this->AbandonFrame();
    this->PrepareFrame();
    frame.acquired = acquired;
    frame.imageWaited = acquired;

    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    frame.recording = vkBeginCommandBuffer(frame.cmd, &beginInfo) == VK_SUCCESS;
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
    allocInfo.memoryTypeIndex = this->FindMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (allocInfo.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(this->m_device, &allocInfo, nullptr, &out.memory) != VK_SUCCESS) {
        vkDestroyBuffer(this->m_device, out.buffer, nullptr);
        out = {};
        return 0;
    }

    if (vkBindBufferMemory(this->m_device, out.buffer, out.memory, 0) != VK_SUCCESS ||
        vkMapMemory(this->m_device, out.memory, 0, VK_WHOLE_SIZE, 0, reinterpret_cast<void**>(&out.mapped)) != VK_SUCCESS) {
        this->DestroyBuffer(out);
        return 0;
    }

    return 1;
}

void VkContext::DestroyBuffer(VkBufferAlloc& buffer) {
    if (buffer.buffer) {
        vkDestroyBuffer(this->m_device, buffer.buffer, nullptr);
        vkFreeMemory(this->m_device, buffer.memory, nullptr);
    }

    buffer = {};
}

int32_t VkContext::CreateImage(const VkImageCreateInfo& info, VkImageAspectFlags aspect, VkImageViewType viewType, const VkComponentMapping& swizzle, VkImageAlloc& out) {
    out = {};
    out.format = info.format;

    if (vkCreateImage(this->m_device, &info, nullptr, &out.image) != VK_SUCCESS) {
        return 0;
    }

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(this->m_device, out.image, &requirements);

    uint32_t memoryType = this->FindMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

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
    VkFrame& frame = this->Frame();
    VkDeviceSize offset = (frame.stagingOffset + 15) & ~static_cast<VkDeviceSize>(15);

    if (offset + size <= frame.staging.size) {
        frame.stagingOffset = offset + size;
        return { frame.staging.buffer, offset, frame.staging.mapped + offset };
    }

    VkBufferAlloc temp;
    this->CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, temp);
    VkStaging staging = { temp.buffer, 0, temp.mapped };
    this->RetireBuffer(temp);

    return staging;
}

void VkContext::Retire(const VkGarbage& garbage) {
    this->PrepareFrame();
    this->Frame().garbage.push_back(garbage);
}

void VkContext::RetireBuffer(VkBufferAlloc& buffer) {
    if (buffer.buffer) {
        VkGarbage garbage = {};
        garbage.buffer = buffer.buffer;
        garbage.memory = buffer.memory;
        garbage.mapped = buffer.mapped;
        garbage.size = buffer.size;
        garbage.usage = buffer.usage;
        this->Retire(garbage);
    }

    buffer.buffer = VK_NULL_HANDLE;
    buffer.memory = VK_NULL_HANDLE;
    buffer.mapped = nullptr;
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
        bool recyclable = garbage.buffer && garbage.mapped &&
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
    VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = { aspect, 0, levels, 0, VK_REMAINING_ARRAY_LAYERS };

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}
