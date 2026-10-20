#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>
#include "Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.h"
#include "Engine/Source/Gx/Blit.h"
#include "Engine/Source/Gx/CGxBuf.h"
#include "Engine/Source/Gx/CGxPool.h"
#include "Engine/Source/Gx/CGxTex.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Base/Profiler.h"
#include "Engine/Source/BlizzardCore/SFile2-Core.h"
#include "Engine/Source/Gx/WowDecompVk.h"
#include "Engine/Source/Storm/H/storm.h"

static const uint32_t s_ffpVertexCode[] =
#include "Ffp.vert.inc"
;

static const uint32_t s_ffpFragmentCode[] =
#include "Ffp.frag.inc"
;

static const uint32_t s_ffpTerrainCode[] =
#include "FfpTerrain.frag.inc"
;

static const uint32_t s_fxaaVertexCode[] =
#include "Fxaa.vert.inc"
;

static const uint32_t s_fxaaFragmentCode[] =
#include "Fxaa.frag.inc"
;

static int32_t ShaderDesaturates(CGxShader* shader) {
    return shader && shader->m_key.m_str && !SStrCmpI(shader->m_key.m_str, "Desaturate", STORM_MAX_STR);
}

static_assert(sizeof(GpuDrawItem) == 448, "GpuDrawItem must match Ffp.vert");
static_assert(offsetof(GpuDrawItem, tex0) == 64, "GpuDrawItem.tex0");
static_assert(offsetof(GpuDrawItem, tex1) == 128, "GpuDrawItem.tex1");
static_assert(offsetof(GpuDrawItem, lightPos) == 192, "GpuDrawItem.lightPos");
static_assert(offsetof(GpuDrawItem, lightAtten) == 384, "GpuDrawItem.lightAtten");

const VkFormat CGxDeviceVulkan::s_gxTexFmtToVkFmt[GxTexFormats_Last] = { VK_FORMAT_UNDEFINED, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_B4G4R4A4_UNORM_PACK16, VK_FORMAT_A1R5G5B5_UNORM_PACK16, VK_FORMAT_R5G6B5_UNORM_PACK16, VK_FORMAT_BC1_RGBA_UNORM_BLOCK, VK_FORMAT_BC2_UNORM_BLOCK, VK_FORMAT_BC3_UNORM_BLOCK, VK_FORMAT_R8G8_SNORM, VK_FORMAT_R16G16_SFLOAT, VK_FORMAT_R32_SFLOAT, VK_FORMAT_UNDEFINED };

const VkPrimitiveTopology CGxDeviceVulkan::s_primitiveConversion[GxPrims_Last] = { VK_PRIMITIVE_TOPOLOGY_POINT_LIST, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, VK_PRIMITIVE_TOPOLOGY_LINE_STRIP, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN };

const VkBlendFactor CGxDeviceVulkan::s_srcBlend[GxBlends_Last] = { VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_DST_COLOR, VK_BLEND_FACTOR_DST_COLOR, VK_BLEND_FACTOR_DST_COLOR, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_CONSTANT_COLOR };

const VkBlendFactor CGxDeviceVulkan::s_dstBlend[GxBlends_Last] = { VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_SRC_COLOR, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR };

const VkCompareOp CGxDeviceVulkan::s_cmpFunc[5] = { VK_COMPARE_OP_LESS_OR_EQUAL, VK_COMPARE_OP_EQUAL, VK_COMPARE_OP_GREATER_OR_EQUAL, VK_COMPARE_OP_LESS, VK_COMPARE_OP_ALWAYS };

const VkFormat CGxDeviceVulkan::s_gxAttribToVkFormat[7] = { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R32G32_SFLOAT, VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R16G16_SSCALED, VK_FORMAT_R32_SFLOAT };

const uint32_t CGxDeviceVulkan::s_gxAttribSize[7] = { 4, 4, 4, 8, 12, 4, 4 };

const int32_t CGxDeviceVulkan::s_gxAttribLocation[GxVAs_Last] = { 0, 5, 6, 1, 2, -1, 3, 4, -1, -1, -1, -1, -1, -1 };

EGxTexFormat CGxDeviceVulkan::s_gxTexFormatToUse[GxTexFormats_Last] = { GxTex_Unknown, GxTex_Abgr8888, GxTex_Argb8888, GxTex_Argb4444, GxTex_Argb1555, GxTex_Rgb565, GxTex_Dxt1, GxTex_Dxt3, GxTex_Dxt5, GxTex_Uv88, GxTex_Gr1616F, GxTex_R32F, GxTex_D24X8 };

static const VkFilter s_filters[GxTexFilters_Last] = { VK_FILTER_NEAREST, VK_FILTER_LINEAR, VK_FILTER_NEAREST, VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_FILTER_LINEAR };

static const VkSamplerMipmapMode s_mipFilters[GxTexFilters_Last] = { VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR };

static const VkSamplerAddressMode s_wrapModes[2] = { VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_REPEAT };

static VkComponentMapping TexSwizzle(EGxTexFormat format) {
    switch (format) {
        case GxTex_Argb4444:
            return { VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_A, VK_COMPONENT_SWIZZLE_B };

        case GxTex_Uv88:
        case GxTex_Gr1616F:
            return { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_ONE, VK_COMPONENT_SWIZZLE_ONE };

        case GxTex_R32F:
            return { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_ONE, VK_COMPONENT_SWIZZLE_ONE, VK_COMPONENT_SWIZZLE_ONE };

        default:
            return {};
    }
}

static void ColorToFloats(uint32_t value, float* out) {
    CImVector color = *reinterpret_cast<const CImVector*>(&value);
    out[0] = color.r * (1.0f / 255.0f);
    out[1] = color.g * (1.0f / 255.0f);
    out[2] = color.b * (1.0f / 255.0f);
    out[3] = color.a * (1.0f / 255.0f);
}

// ʕ •ᴥ•ʔ✿ Borderless fullscreen is already the monitor. Rebuilding the device drops the pipeline cache and stalls every later screen ✿ ʕ •ᴥ•ʔ
static bool NeedsFullVulkanSurfaceRebuild(const CGxFormat& current, const CGxFormat& next) {
    if (current.m_window != next.m_window || current.m_maximize != next.m_maximize) {
        return true;
    }

    if (next.m_window && (current.m_size.x != next.m_size.x || current.m_size.y != next.m_size.y || current.m_pos.x != next.m_pos.x || current.m_pos.y != next.m_pos.y)) {
        return true;
    }

    return current.m_colorFormat != next.m_colorFormat || current.m_depthFormat != next.m_depthFormat || current.m_multisampleCount != next.m_multisampleCount || current.m_multisampleQuality != next.m_multisampleQuality || current.m_stereoEnabled != next.m_stereoEnabled || memcmp(current.m_fixedFunction, next.m_fixedFunction, sizeof(current.m_fixedFunction));
}

static bool NeedsVulkanSwapchainRebuild(const CGxFormat& current, const CGxFormat& next) {
    return current.m_vsync != next.m_vsync || current.m_backBufferCount != next.m_backBufferCount;
}

static void DefaultTexUserFunc(EGxTexCommand command, uint32_t width, uint32_t height, uint32_t face, uint32_t level, void* userArg, uint32_t& texelStrideInBytes, const void*& texels) {
    static uint32_t s_texels[64];

    if (command == GxTex_Lock) {
        for (uint32_t& texel : s_texels) {
            texel = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(userArg));
        }
    } else if (command == GxTex_Latch) {
        texelStrideInBytes = 4 * width;
        texels = s_texels;
    }
}

bool VkPipelineKey::operator==(const VkPipelineKey& key) const {
    return !memcmp(this, &key, sizeof(VkPipelineKey));
}

size_t VkPipelineKeyHash::operator()(const VkPipelineKey& key) const {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&key);
    size_t hash = 2166136261u;

    for (size_t i = 0; i < sizeof(VkPipelineKey); i++) {
        hash = (hash ^ bytes[i]) * 16777619u;
    }

    return hash;
}

CGxDeviceVulkan::CGxDeviceVulkan() {
    this->m_api = GxApi_Vulkan;
    this->m_caps.m_colorFormat = GxCF_argb;
    this->m_caps.unk_0018 = 1;
    this->DeviceCreatePools();
    this->DeviceCreateStreamBufs();
}

CGxDeviceVulkan::~CGxDeviceVulkan() {
}

int32_t CGxDeviceVulkan::DeviceCreate(GxWindowProc windowProc, const CGxFormat& format) {
    this->m_ownWindow = 1;

    if (!this->IPlatformInit()) {
        CGxDevice::Log("CGxDeviceVulkan::DeviceCreate(): platform initialization failed");
        this->DeviceDestroy();
        return 0;
    }

    if (CGxDevice::DeviceCreate(windowProc, format)) {
        return 1;
    }

    this->DeviceDestroy();
    return 0;
}

int32_t CGxDeviceVulkan::DeviceCreate(void* window, const CGxFormat& format) {
    this->m_ownWindow = 0;
    CGxDevice::DeviceCreate(window, format);
    this->m_window = window;

    if (this->IPlatformInit() && this->ICreateVulkan(format) && CGxDevice::DeviceCreate(window, format)) {
        return 1;
    }

    this->DeviceDestroy();
    return 0;
}

void CGxDeviceVulkan::DeviceDestroy() {
    CGxDevice::DeviceDestroy();
    this->IDestroyVulkan();
    this->IDestroyWindow();
    this->IPlatformShutdown();
}

int32_t CGxDeviceVulkan::DeviceSetFormat(const CGxFormat& format) {
    CGxDevice::Log("CGxDeviceVulkan::DeviceSetFormat():");
    this->Log(format);

    if (this->m_context && this->m_vk.m_device && this->m_vk.m_surface && this->m_window && !NeedsFullVulkanSurfaceRebuild(this->m_format, format)) {
        this->ISceneAbort();
        CGxFormat previous = this->m_format;
        this->m_format = format;

        if (!NeedsVulkanSwapchainRebuild(previous, format) || this->IRecreateSwapchain()) {
            this->m_windowFocus = 1;
            return CGxDevice::DeviceSetFormat(format);
        }

        this->m_format = previous;
        CGxDevice::Log("CGxDeviceVulkan::DeviceSetFormat(): swapchain recreation failed");
    }

    // ʕ •ᴥ•ʔ✿ A new window needs a new instance. The loader drops the old device and then aborts in vkCreateSwapchainKHR ✿ ʕ •ᴥ•ʔ
    this->IDestroyVulkan();
    this->m_format = format;

    CGxFormat createFormat = format;

    if (this->ICreateWindow(createFormat) && this->ICreateVulkan(createFormat) && CGxDevice::DeviceSetFormat(createFormat)) {
        this->m_windowFocus = 1;
        return 1;
    }

    CGxDevice::Log("CGxDeviceVulkan::DeviceSetFormat(): unable to set format!");
    this->IDestroyVulkan();

    if (this->m_ownWindow) {
        this->IDestroyWindow();
    }

    return 0;
}

int32_t CGxDeviceVulkan::ICreateVulkan(const CGxFormat& format) {
    if (!this->ICreateInstance()) {
        CGxDevice::Log("CGxDeviceVulkan: vkCreateInstance failed");
        return 0;
    }

    VkSurfaceKHR surface = this->ICreateSurface();

    if (!surface) {
        CGxDevice::Log("CGxDeviceVulkan: surface creation failed");
        this->IDestroyVulkan();
        return 0;
    }

    int32_t firstTime = !this->m_vk.m_device;

    if (firstTime) {
        if (!this->m_vk.CreateDevice(surface)) {
            CGxDevice::Log("CGxDeviceVulkan: no suitable Vulkan 1.1 device");
            this->IDestroyVulkan();
            return 0;
        }

        CGxDevice::Log("CGxDeviceVulkan: using %s", this->m_vk.m_properties.deviceName);

        if (!this->ICreateDeviceObjects()) {
            this->IDestroyVulkan();
            return 0;
        }
    } else if (!this->m_vk.SetSurface(surface)) {
        CGxDevice::Log("CGxDeviceVulkan: device cannot present to the new surface");
        this->IDestroyVulkan();
        return 0;
    }

    this->m_windowVisible = 1;

    if (!this->IRecreateSwapchain()) {
        CGxDevice::Log("CGxDeviceVulkan: swapchain creation failed");
        this->IDestroyVulkan();
        return 0;
    }

    this->m_context = 1;
    this->m_intF5C = 0;

    if (firstTime) {
        this->ISetCaps(format);
        this->Log(this->m_caps);
    }

    this->IStateSetDefaults();

    if (firstTime) {
        this->ICursorCreate(format);

        GxTexCreate(8, 8, GxTex_Argb8888, CGxTexFlags(GxTex_Linear), reinterpret_cast<void*>(static_cast<uintptr_t>(0xFF00FF00)), DefaultTexUserFunc, this->m_defaultTexture);

        if (this->m_defaultTexture) {
            GxTexUpdate(this->m_defaultTexture, 0, 0, 0, 0, 1);
        }

        g_theGxDevicePtr->ShaderCreate(CGxDevice::s_uiVertexShader, GxSh_Vertex, "Shaders\\Vertex", "UI", 2);
        g_theGxDevicePtr->ShaderCreate(&CGxDevice::s_uiPixelShader, GxSh_Pixel, "Shaders\\Pixel", "UI", 1);
    }

    return 1;
}

int32_t CGxDeviceVulkan::ICreateDeviceObjects() {
    VkDevice device = this->m_vk.m_device;

    VkDescriptorSetLayoutBinding bindings[5] = {
        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr },
        { 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr },
    };

    VkDescriptorSetLayoutCreateInfo setInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    setInfo.bindingCount = 5;
    setInfo.pBindings = bindings;

    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(device, &setInfo, nullptr, &setLayout) != VK_SUCCESS) {
        return 0;
    }

    VkPushConstantRange push = {};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push.offset = 0;
    push.size = 12;

    VkPipelineLayoutCreateInfo layoutInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &setLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &push;

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
        return 0;
    }

    VkPipelineCacheCreateInfo cacheInfo = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
    VkPipelineCache pipelineCache = VK_NULL_HANDLE;
    if (vkCreatePipelineCache(device, &cacheInfo, nullptr, &pipelineCache) != VK_SUCCESS) {
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
        return 0;
    }

    VkShaderModuleCreateInfo moduleInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    moduleInfo.codeSize = sizeof(s_ffpVertexCode);
    moduleInfo.pCode = s_ffpVertexCode;

    VkShaderModule vertexModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &moduleInfo, nullptr, &vertexModule) != VK_SUCCESS) {
        vkDestroyPipelineCache(device, pipelineCache, nullptr);
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
        return 0;
    }

    moduleInfo.codeSize = sizeof(s_ffpFragmentCode);
    moduleInfo.pCode = s_ffpFragmentCode;

    VkShaderModule fragmentModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &moduleInfo, nullptr, &fragmentModule) != VK_SUCCESS) {
        vkDestroyShaderModule(device, vertexModule, nullptr);
        vkDestroyPipelineCache(device, pipelineCache, nullptr);
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
        return 0;
    }

    this->m_setLayout = setLayout;
    this->m_pipelineLayout = pipelineLayout;
    this->m_pipelineCache = pipelineCache;
    this->m_vertexModule = vertexModule;
    this->m_fragmentModule = fragmentModule;

    if (this->m_vk.m_bindless) {
        moduleInfo.codeSize = sizeof(s_ffpTerrainCode);
        moduleInfo.pCode = s_ffpTerrainCode;

        if (vkCreateShaderModule(device, &moduleInfo, nullptr, &this->m_terrainFragment) != VK_SUCCESS) {
            this->m_terrainFragment = VK_NULL_HANDLE;
        } else {
            VkDescriptorBindingFlags bindingFlags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
            VkDescriptorSetLayoutBindingFlagsCreateInfo flagInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO };
            flagInfo.bindingCount = 1;
            flagInfo.pBindingFlags = &bindingFlags;
            VkDescriptorSetLayoutBinding terrainBinding = { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, TerrainSlots, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
            VkDescriptorSetLayoutCreateInfo terrainSetInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
            terrainSetInfo.pNext = &flagInfo;
            terrainSetInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
            terrainSetInfo.bindingCount = 1;
            terrainSetInfo.pBindings = &terrainBinding;

            if (vkCreateDescriptorSetLayout(device, &terrainSetInfo, nullptr, &this->m_terrainSetLayout) == VK_SUCCESS) {
                VkDescriptorSetLayout sets[2] = { setLayout, this->m_terrainSetLayout };
                VkPipelineLayoutCreateInfo terrainLayout = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
                terrainLayout.setLayoutCount = 2;
                terrainLayout.pSetLayouts = sets;
                terrainLayout.pushConstantRangeCount = 1;
                terrainLayout.pPushConstantRanges = &push;

                if (vkCreatePipelineLayout(device, &terrainLayout, nullptr, &this->m_terrainPipelineLayout) == VK_SUCCESS) {
                    this->m_bindless = 1;
                }
            }
        }
    }

    if (!this->m_vk.CreateBuffer(32, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, this->m_dummyVertices)) {
        return 0;
    }

    memset(this->m_dummyVertices.mapped, 0, 16);
    memset(this->m_dummyVertices.mapped + 16, 0xFF, 16);

    return 1;
}

void CGxDeviceVulkan::IDestroyVulkan() {
    VkDevice device = this->m_vk.m_device;

    if (device) {
        this->ISceneAbort();
        vkDeviceWaitIdle(device);
        this->IDestroyPost();

        for (uint32_t i = 0; i < 2; i++) {
            if (CGxDevice::s_uiVertexShader[i]) {
                g_theGxDevicePtr->ShaderDestroy(CGxDevice::s_uiVertexShader[i]);
            }
        }

        if (CGxDevice::s_uiPixelShader) {
            g_theGxDevicePtr->ShaderDestroy(CGxDevice::s_uiPixelShader);
        }

        this->ICursorDestroy();

        if (this->m_defaultTexture) {
            GxTexDestroy(this->m_defaultTexture);
            this->m_defaultTexture = nullptr;
        }

        this->ITexForceRecreation(1);
        this->IReleasePools();
        this->m_vk.RetireImage(this->m_offscreenDepth);
        this->m_vk.RetireBuffer(this->m_dummyVertices);
        this->m_offscreenDepthExtent = {};

        for (auto& pipeline : this->m_pipelines) {
            vkDestroyPipeline(device, pipeline.second, nullptr);
        }

        for (auto& sampler : this->m_samplers) {
            vkDestroySampler(device, sampler.second, nullptr);
        }

        this->m_pipelines.clear();
        this->m_samplers.clear();

        for (VkDescriptorPool pool : this->m_terrainPools) {
            if (pool) {
                vkDestroyDescriptorPool(device, pool, nullptr);
            }
        }

        if (this->m_terrainFragment) {
            vkDestroyShaderModule(device, this->m_terrainFragment, nullptr);
        }

        if (this->m_terrainPipelineLayout) {
            vkDestroyPipelineLayout(device, this->m_terrainPipelineLayout, nullptr);
        }

        if (this->m_terrainSetLayout) {
            vkDestroyDescriptorSetLayout(device, this->m_terrainSetLayout, nullptr);
        }

        vkDestroyShaderModule(device, this->m_vertexModule, nullptr);
        vkDestroyShaderModule(device, this->m_fragmentModule, nullptr);
        vkDestroyPipelineCache(device, this->m_pipelineCache, nullptr);
        vkDestroyPipelineLayout(device, this->m_pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, this->m_setLayout, nullptr);

        this->m_vertexModule = VK_NULL_HANDLE;
        this->m_fragmentModule = VK_NULL_HANDLE;
        this->m_terrainFragment = VK_NULL_HANDLE;
        this->m_terrainPipelineLayout = VK_NULL_HANDLE;
        this->m_terrainSetLayout = VK_NULL_HANDLE;
        this->m_terrainSets[0] = VK_NULL_HANDLE;
        this->m_terrainSets[1] = VK_NULL_HANDLE;
        this->m_terrainPools[0] = VK_NULL_HANDLE;
        this->m_terrainPools[1] = VK_NULL_HANDLE;
        this->m_bindless = 0;
        this->m_pipelineCache = VK_NULL_HANDLE;
        this->m_pipelineLayout = VK_NULL_HANDLE;
        this->m_setLayout = VK_NULL_HANDLE;
    }

    this->m_vk.DestroyInstance();
    this->m_context = 0;
    this->m_passTarget = nullptr;
    this->m_textureTarget[GxBuffers_Color].m_apiSpecific = nullptr;
    this->m_textureTarget[GxBuffers_Depth].m_apiSpecific = nullptr;
}

int32_t CGxDeviceVulkan::IRecreateSwapchain() {
    if (this->m_vk.m_device) {
        vkDeviceWaitIdle(this->m_vk.m_device);
    }

    this->IReleasePostTargets();

    uint32_t width;
    uint32_t height;
    this->IClientSize(width, height);

    VkFormat colorFormat = this->m_vk.m_colorFormat;

    if (!width || !height || !this->m_vk.CreateSwapchain(width, height, this->m_format.m_vsync)) {
        return 0;
    }

    this->m_clientExtent = { width, height };

    if (colorFormat != VK_FORMAT_UNDEFINED && colorFormat != this->m_vk.m_colorFormat) {
        for (auto& pipeline : this->m_pipelines) {
            vkDestroyPipeline(this->m_vk.m_device, pipeline.second, nullptr);
        }

        this->m_pipelines.clear();
        this->m_lastPipeline = VK_NULL_HANDLE;
    }

    this->m_swapchainDirty = 0;

    CRect rect;
    rect.minX = 0.0f;
    rect.minY = 0.0f;
    rect.maxX = static_cast<float>(this->m_vk.m_extent.width);
    rect.maxY = static_cast<float>(this->m_vk.m_extent.height);
    this->m_defWindowRect = rect;

    if (!this->m_textureTarget[GxBuffers_Color].m_texture && !this->m_textureTarget[GxBuffers_Depth].m_texture) {
        this->m_curWindowRect = rect;
    }

    this->m_viewportDirty = 1;
    this->m_scissorRectDirty = 1;

    return 1;
}

void CGxDeviceVulkan::ISetCaps(const CGxFormat& format) {
    const VkPhysicalDeviceLimits& limits = this->m_vk.m_properties.limits;

    this->m_caps.m_numTmus = 2;
    this->m_caps.m_pixelCenterOnEdge = 1;
    this->m_caps.m_texelCenterOnEdge = 1;
    this->m_caps.m_numStreams = limits.maxVertexInputBindings < 16 ? limits.maxVertexInputBindings : 16;
    this->m_caps.m_streamOffset = 1;
    this->m_caps.m_maxIndex = limits.maxDrawIndexedIndexValue < 0xFFFFFF ? limits.maxDrawIndexedIndexValue : 0xFFFFFF;
    this->m_caps.m_generateMipMaps = 0;

    for (uint32_t i = 0; i < GxTexTargets_Last; i++) {
        this->m_caps.m_texMaxSize[i] = limits.maxImageDimension2D;
    }

    this->m_caps.m_texTarget[GxTex_2d] = 1;
    this->m_caps.m_texTarget[GxTex_CubeMap] = 0;
    this->m_caps.m_texTarget[GxTex_Rectangle] = 0;
    this->m_caps.m_texTarget[GxTex_NonPow2] = 1;
    this->m_caps.m_texNonPow2 = 1;
    this->m_caps.m_texFilterTrilinear = 1;
    this->m_caps.m_texFilterAnisotropic = this->m_vk.m_features.samplerAnisotropy && limits.maxSamplerAnisotropy >= 2.0f;
    this->m_caps.m_maxTexAnisotropy = this->m_caps.m_texFilterAnisotropic ? static_cast<uint32_t>(limits.maxSamplerAnisotropy) : 1;
    this->m_caps.m_depthBias = 1;
    this->m_caps.m_colorWrite = 1;
    this->m_caps.m_maxClipPlanes = 0;
    this->m_caps.m_hwCursor = 0;
    this->m_caps.m_occlusionQuery = 0;
    this->m_caps.m_pointParameters = 0;
    this->m_caps.m_pointScaleMax = 1.0f;
    this->m_caps.m_pointSprite = 0;
    this->m_caps.m_blendFactor = 1;
    this->m_caps.m_shaderAlphaRef = 1;
    this->m_caps.m_stereoAvailable = 0;

    for (uint32_t i = 0; i < GxShTargets_Last; i++) {
        this->m_caps.m_shaderTargets[i] = 0;
        this->m_caps.m_shaderConstants[i] = 0;
    }

    for (uint32_t i = 0; i < GxTexFormats_Last; i++) {
        this->m_caps.m_texFmt[i] = 0;
        this->m_caps.m_rttFormat[i] = 0;

        if (s_gxTexFmtToVkFmt[i] == VK_FORMAT_UNDEFINED) {
            continue;
        }

        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(this->m_vk.m_physicalDevice, s_gxTexFmtToVkFmt[i], &properties);
        VkFormatFeatureFlags required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;

        // ʕ •ᴥ•ʔ✿ Block copies need a transfer dest, and the samplers are linear ✿ ʕ •ᴥ•ʔ
        if (i >= GxTex_Dxt1 && i <= GxTex_Dxt5) {
            required |= VK_FORMAT_FEATURE_TRANSFER_DST_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        }

        this->m_caps.m_texFmt[i] = (properties.optimalTilingFeatures & required) == required;
    }

    // ʕ •ᴥ•ʔ✿ Keep DXT on the GPU. Expanding it to RGBA is 4–8× the sample traffic ✿ ʕ •ᴥ•ʔ
    if (!this->m_caps.m_texFmt[GxTex_Dxt1]) {
        s_gxTexFormatToUse[GxTex_Dxt1] = GxTex_Argb8888;
    }

    if (!this->m_caps.m_texFmt[GxTex_Dxt3]) {
        s_gxTexFormatToUse[GxTex_Dxt3] = GxTex_Argb8888;
    }

    if (!this->m_caps.m_texFmt[GxTex_Dxt5]) {
        s_gxTexFormatToUse[GxTex_Dxt5] = GxTex_Argb8888;
    }

    this->m_caps.m_rttFormat[GxTex_Argb8888] = 1;
    this->m_caps.unk_0134 = 1;
    this->m_caps.unk_0138 = 1;
}

void CGxDeviceVulkan::IStateSetDefaults() {
    this->IRsForceUpdate();
    this->IRsSync(0);
    this->m_primVertexDirty = 0xFFFFFFFF;
    this->m_primIndexDirty = 0;
    this->InitLights();
    this->m_viewportDirty = 1;
    this->m_scissorRectDirty = 1;
    this->ISceneBegin();
}

void CGxDeviceVulkan::IInvalidateCmdState() {
    this->m_boundPipeline = VK_NULL_HANDLE;
    // ʕ •ᴥ•ʔ✿ New command buffer: the set and its dynamic uniform offset are unbound ✿ ʕ •ᴥ•ʔ
    this->m_boundSet = VK_NULL_HANDLE;
    this->m_boundDynamicOffset = 0xFFFFFFFFu;
    this->m_boundBoneOffset = 0xFFFFFFFFu;
    this->m_pushBase = 0xFFFFFFFFu;
    this->m_pushInfluences = 0xFFFFFFFFu;
    this->m_pushStride = 0xFFFFFFFFu;
    memset(this->m_setViews, 0, sizeof(this->m_setViews));
    memset(this->m_setSamplers, 0, sizeof(this->m_setSamplers));
    memset(this->m_boundBuffers, 0, sizeof(this->m_boundBuffers));
    this->m_boundIndexBuffer = VK_NULL_HANDLE;
    this->m_dynamicValid = 0;
    this->m_uniformsValid = 0;
}

static void ShaderOverrideDir(char* dest, size_t destSize) {
    char base[260];
    SFile::GetBasePath(base, sizeof(base));

    for (char* cursor = base; *cursor; ++cursor) {
        if (*cursor == '\\') {
            *cursor = '/';
        }
    }

    size_t length = strlen(base);

    if (length && base[length - 1] != '/' && length + 1 < sizeof(base)) {
        base[length] = '/';
        base[length + 1] = '\0';
    }

    SStrPrintf(dest, destSize, "%sShaders/Vulkan", base);
}

static int32_t LoadSpirvWords(const char* path, std::vector<uint32_t>& words) {
    FILE* file = fopen(path, "rb");

    if (!file) {
        return 0;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return -1;
    }

    long size = ftell(file);

    if (size < 20 || (size % 4) != 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return -1;
    }

    words.resize(static_cast<size_t>(size / 4));

    if (fread(words.data(), sizeof(uint32_t), words.size(), file) != words.size() || words[0] != 0x07230203u) {
        fclose(file);
        words.clear();
        return -1;
    }

    fclose(file);
    return 1;
}

static VkShaderModule CreateShaderModule(VkDevice device, const uint32_t* code, size_t bytes) {
    VkShaderModuleCreateInfo info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    info.codeSize = bytes;
    info.pCode = code;

    VkShaderModule module = VK_NULL_HANDLE;

    if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    return module;
}

static const char* SwapShaderModule(VkDevice device, VkShaderModule& slot, const uint32_t* builtin, size_t builtinBytes, const char* filename) {
    char directory[300];
    char path[560];
    ShaderOverrideDir(directory, sizeof(directory));
    SStrPrintf(path, sizeof(path), "%s/%s", directory, filename);

    std::vector<uint32_t> words;
    int32_t file = LoadSpirvWords(path, words);
    VkShaderModule module = VK_NULL_HANDLE;
    const char* label = "built-in";

    if (file > 0) {
        module = CreateShaderModule(device, words.data(), words.size() * sizeof(uint32_t));
        label = module ? "override" : "override failed, built-in";
    } else if (file < 0) {
        label = "rejected, built-in";
    }

    if (!module) {
        module = CreateShaderModule(device, builtin, builtinBytes);
    }

    if (!module) {
        return "failed";
    }

    if (slot) {
        vkDestroyShaderModule(device, slot, nullptr);
    }

    slot = module;
    return label;
}

static void AppendShaderLine(char* report, size_t reportSize, const char* filename, const char* label) {
    if (!report || reportSize == 0) {
        return;
    }

    size_t used = strlen(report);

    if (used >= reportSize) {
        return;
    }

    if (used && used + 1 < reportSize) {
        report[used++] = '\n';
        report[used] = '\0';
    }

    if (used >= reportSize || reportSize - used < 2) {
        return;
    }

    SStrPrintf(report + used, reportSize - used, "%s: %s", filename, label);
}

void CGxDeviceVulkan::IReloadExperimentalShaders() {
    VkDevice device = this->m_vk.m_device;

    if (!device) {
        WowDecompVk_SetStatus("Vulkan device is not ready.");
        return;
    }

    // ʕ •ᴥ•ʔ✿ Pipelines still reference the modules until the GPU finishes the last frame ✿ ʕ •ᴥ•ʔ
    if (vkDeviceWaitIdle(device) != VK_SUCCESS) {
        WowDecompVk_SetStatus("GPU was busy. Reload again.");
        return;
    }

    for (auto& pipeline : this->m_pipelines) {
        vkDestroyPipeline(device, pipeline.second, nullptr);
    }

    this->m_pipelines.clear();
    this->m_lastPipeline = VK_NULL_HANDLE;

    char directory[300];
    char report[512];
    ShaderOverrideDir(directory, sizeof(directory));
    report[0] = '\0';

    AppendShaderLine(report, sizeof(report), "Ffp.vert.spv", SwapShaderModule(device, this->m_vertexModule, s_ffpVertexCode, sizeof(s_ffpVertexCode), "Ffp.vert.spv"));
    AppendShaderLine(report, sizeof(report), "Ffp.frag.spv", SwapShaderModule(device, this->m_fragmentModule, s_ffpFragmentCode, sizeof(s_ffpFragmentCode), "Ffp.frag.spv"));

    if (this->m_terrainFragment) {
        AppendShaderLine(report, sizeof(report), "FfpTerrain.frag.spv", SwapShaderModule(device, this->m_terrainFragment, s_ffpTerrainCode, sizeof(s_ffpTerrainCode), "FfpTerrain.frag.spv"));
    }

    WowDecompVk_SetStatus(report);
}

void CGxDeviceVulkan::ISceneBegin() {
    if (!this->m_context || this->m_frameActive || !this->m_windowVisible) {
        return;
    }

    if (WowDecompVk_ConsumeShaderReload()) {
        this->IReloadExperimentalShaders();
    }

    // ʕ •ᴥ•ʔ✿ Pools are recreated per frame; a buffer fallback last frame must not mute this one ✿ ʕ •ᴥ•ʔ
    this->m_intF5C = 0;

    // ʕ ◕ᴥ◕ ʔ✿ Wayland scale changes produce no size event and no Vulkan error; watch the drawable size ✿ ʕ ◕ᴥ◕ ʔ
    {
        uint32_t width;
        uint32_t height;
        this->IClientSize(width, height);

        if (width && height && (width != this->m_clientExtent.width || height != this->m_clientExtent.height)) {
            this->m_swapchainDirty = 1;
        }
    }

    if (this->m_swapchainDirty && !this->IRecreateSwapchain()) {
        return;
    }

    int32_t begun = this->m_vk.BeginFrame();

    // -- ʕ •ᴥ•ʔ✿ The bindless set survives the frame. Resetting it rewrote every terrain texture on each scene. ✿ ʕ •ᴥ•ʔ
    if (begun == 2) {
        // ʕ ●ᴥ●ʔ✿ No image yet (hidden/suspended surface): skip without rebuilding the swapchain ✿ ʕ ●ᴥ●ʔ
        this->m_imageStarved = 1;
        return;
    }

    if (begun == 3 || begun == 4) {
        // ʕ •ᴥ•ʔ✿ Acquired image was presented; do not pace a visible window at the background cap ✿ ʕ •ᴥ•ʔ
        this->m_imageStarved = 0;
        // ʕ •ᴥ•ʔ✿ EndFrame failed, including out of date or suboptimal; the next scene begin recreates ✿ ʕ •ᴥ•ʔ
        if (begun == 4) {
            this->m_swapchainDirty = 1;
        }
        return;
    }

    this->m_imageStarved = 0;

    if (!begun && (!this->IRecreateSwapchain() || this->m_vk.BeginFrame() != 1)) {
        this->m_swapchainDirty = 1;
        return;
    }

    this->m_frameActive = 1;
    this->m_frameCleared = 0;
    this->m_passActive = 0;
    this->IInvalidateCmdState();
    this->ShaderConstantsClear();
}

void CGxDeviceVulkan::IReleasePostTargets() {
    VkDevice device = this->m_vk.m_device;

    if (!device) {
        this->m_postFramebuffers.clear();
        this->m_postColor = {};
        this->m_postColorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        this->m_postExtent = {};
        this->m_postSwapchain = VK_NULL_HANDLE;
        return;
    }

    for (VkFramebuffer framebuffer : this->m_postFramebuffers) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }

    this->m_postFramebuffers.clear();

    if (this->m_postColor.image) {
        vkDestroyImageView(device, this->m_postColor.view, nullptr);
        vkDestroyImage(device, this->m_postColor.image, nullptr);
        this->m_vk.m_heap.Free(this->m_postColor.alloc);
        this->m_postColor = {};
    }

    this->m_postColorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    this->m_postExtent = {};
    this->m_postSwapchain = VK_NULL_HANDLE;
}

void CGxDeviceVulkan::IDestroyPost() {
    VkDevice device = this->m_vk.m_device;
    this->IReleasePostTargets();

    if (!device) {
        return;
    }

    if (this->m_postPipeline) {
        vkDestroyPipeline(device, this->m_postPipeline, nullptr);
    }

    if (this->m_postLayout) {
        vkDestroyPipelineLayout(device, this->m_postLayout, nullptr);
    }

    if (this->m_postSetLayout) {
        vkDestroyDescriptorSetLayout(device, this->m_postSetLayout, nullptr);
    }

    if (this->m_postVertex) {
        vkDestroyShaderModule(device, this->m_postVertex, nullptr);
    }

    if (this->m_postFragment) {
        vkDestroyShaderModule(device, this->m_postFragment, nullptr);
    }

    if (this->m_postSampler) {
        vkDestroySampler(device, this->m_postSampler, nullptr);
    }

    if (this->m_postPass) {
        vkDestroyRenderPass(device, this->m_postPass, nullptr);
    }

    this->m_postPipeline = VK_NULL_HANDLE;
    this->m_postLayout = VK_NULL_HANDLE;
    this->m_postSetLayout = VK_NULL_HANDLE;
    this->m_postVertex = VK_NULL_HANDLE;
    this->m_postFragment = VK_NULL_HANDLE;
    this->m_postSampler = VK_NULL_HANDLE;
    this->m_postPass = VK_NULL_HANDLE;
    this->m_postFormat = VK_FORMAT_UNDEFINED;
}

int32_t CGxDeviceVulkan::IEnsurePost() {
    VkDevice device = this->m_vk.m_device;
    VkExtent2D extent = this->m_vk.m_extent;

    if (!device || !extent.width || !extent.height || this->m_vk.m_colorFormat == VK_FORMAT_UNDEFINED) {
        return 0;
    }

    VkSurfaceCapabilitiesKHR caps = {};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(this->m_vk.m_physicalDevice, this->m_vk.m_surface, &caps);

    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) {
        return 0;
    }

    if (this->m_postFormat != this->m_vk.m_colorFormat) {
        this->IDestroyPost();
    }

    if (this->m_postSwapchain != this->m_vk.m_swapchain || this->m_postExtent.width != extent.width || this->m_postExtent.height != extent.height) {
        this->IReleasePostTargets();
    }

    if (!this->m_postPass) {
        VkAttachmentDescription color = {};
        color.format = this->m_vk.m_colorFormat;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;

        VkSubpassDependency dependencies[2] = {};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo passInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        passInfo.attachmentCount = 1;
        passInfo.pAttachments = &color;
        passInfo.subpassCount = 1;
        passInfo.pSubpasses = &subpass;
        passInfo.dependencyCount = 2;
        passInfo.pDependencies = dependencies;

        if (vkCreateRenderPass(device, &passInfo, nullptr, &this->m_postPass) != VK_SUCCESS) {
            this->m_postPass = VK_NULL_HANDLE;
            return 0;
        }

        this->m_postFormat = this->m_vk.m_colorFormat;
    }

    if (!this->m_postVertex) {
        VkShaderModuleCreateInfo moduleInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        moduleInfo.codeSize = sizeof(s_fxaaVertexCode);
        moduleInfo.pCode = s_fxaaVertexCode;

        if (vkCreateShaderModule(device, &moduleInfo, nullptr, &this->m_postVertex) != VK_SUCCESS) {
            return 0;
        }

        moduleInfo.codeSize = sizeof(s_fxaaFragmentCode);
        moduleInfo.pCode = s_fxaaFragmentCode;

        if (vkCreateShaderModule(device, &moduleInfo, nullptr, &this->m_postFragment) != VK_SUCCESS) {
            vkDestroyShaderModule(device, this->m_postVertex, nullptr);
            this->m_postVertex = VK_NULL_HANDLE;
            return 0;
        }
    }

    if (!this->m_postSampler) {
        VkSamplerCreateInfo samplerInfo = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

        if (vkCreateSampler(device, &samplerInfo, nullptr, &this->m_postSampler) != VK_SUCCESS) {
            return 0;
        }
    }

    if (!this->m_postSetLayout) {
        VkDescriptorSetLayoutBinding binding = { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
        VkDescriptorSetLayoutCreateInfo setInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        setInfo.bindingCount = 1;
        setInfo.pBindings = &binding;

        if (vkCreateDescriptorSetLayout(device, &setInfo, nullptr, &this->m_postSetLayout) != VK_SUCCESS) {
            return 0;
        }
    }

    if (!this->m_postLayout) {
        VkPushConstantRange push = {};
        push.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        push.size = sizeof(float) * 4;

        VkPipelineLayoutCreateInfo layoutInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &this->m_postSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &push;

        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &this->m_postLayout) != VK_SUCCESS) {
            return 0;
        }
    }

    if (!this->m_postPipeline) {
        VkPipelineShaderStageCreateInfo stages[2] = {
            { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, this->m_postVertex, "main", nullptr },
            { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, this->m_postFragment, "main", nullptr },
        };

        VkPipelineVertexInputStateCreateInfo vertexInput = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        VkPipelineInputAssemblyStateCreateInfo inputAssembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewport = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo raster = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisample = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };

        VkPipelineColorBlendAttachmentState attachment = {};
        attachment.colorWriteMask = 0xF;

        VkPipelineColorBlendStateCreateInfo blend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        blend.attachmentCount = 1;
        blend.pAttachments = &attachment;

        VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamic = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamicStates;

        VkGraphicsPipelineCreateInfo info = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = &vertexInput;
        info.pInputAssemblyState = &inputAssembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &multisample;
        info.pDepthStencilState = &depth;
        info.pColorBlendState = &blend;
        info.pDynamicState = &dynamic;
        info.layout = this->m_postLayout;
        info.renderPass = this->m_postPass;

        if (vkCreateGraphicsPipelines(device, this->m_pipelineCache, 1, &info, nullptr, &this->m_postPipeline) != VK_SUCCESS) {
            this->m_postPipeline = VK_NULL_HANDLE;
            return 0;
        }
    }

    if (!this->m_postColor.image) {
        VkImageCreateInfo imageInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = this->m_vk.m_colorFormat;
        imageInfo.extent = { extent.width, extent.height, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

        if (!this->m_vk.CreateImage(imageInfo, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D, {}, this->m_postColor)) {
            return 0;
        }

        this->m_postColorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        this->m_postExtent = extent;
        this->m_postSwapchain = this->m_vk.m_swapchain;
    }

    if (this->m_postFramebuffers.size() != this->m_vk.m_swapViews.size()) {
        for (VkFramebuffer framebuffer : this->m_postFramebuffers) {
            vkDestroyFramebuffer(device, framebuffer, nullptr);
        }

        this->m_postFramebuffers.clear();
        this->m_postFramebuffers.reserve(this->m_vk.m_swapViews.size());

        for (VkImageView view : this->m_vk.m_swapViews) {
            VkFramebufferCreateInfo framebufferInfo = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
            framebufferInfo.renderPass = this->m_postPass;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = &view;
            framebufferInfo.width = extent.width;
            framebufferInfo.height = extent.height;
            framebufferInfo.layers = 1;

            VkFramebuffer framebuffer = VK_NULL_HANDLE;

            if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffer) != VK_SUCCESS) {
                this->IReleasePostTargets();
                return 0;
            }

            this->m_postFramebuffers.push_back(framebuffer);
        }
    }

    return this->m_postPipeline && this->m_postColor.image && this->m_vk.m_imageIndex < this->m_postFramebuffers.size();
}

void CGxDeviceVulkan::IApplyPost() {
    if ((!WowDecompVk_Fxaa() && !WowDecompVk_Sharpen()) || !this->m_vk.Frame().recording || !this->IEnsurePost()) {
        return;
    }

    VkCommandBuffer cmd = this->m_vk.Frame().cmd;
    VkImage swap = this->m_vk.m_swapImages[this->m_vk.m_imageIndex];
    uint32_t width = this->m_vk.m_extent.width;
    uint32_t height = this->m_vk.m_extent.height;

    // ʕ •ᴥ•ʔ✿ The scene pass leaves the swap image in PRESENT_SRC. Copy it, then draw over it ✿ ʕ •ᴥ•ʔ
    VkContext::Barrier(cmd, swap, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkContext::Barrier(cmd, this->m_postColor.image, VK_IMAGE_ASPECT_COLOR_BIT, 1, this->m_postColorLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    this->m_postColorLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    VkImageCopy region = {};
    region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.extent = { width, height, 1 };
    vkCmdCopyImage(cmd, swap, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, this->m_postColor.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkContext::Barrier(cmd, this->m_postColor.image, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    this->m_postColorLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkContext::Barrier(cmd, swap, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    VkDescriptorSet set = this->m_vk.AllocateSet(this->m_postSetLayout);

    if (!set) {
        VkContext::Barrier(cmd, swap, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        return;
    }

    VkDescriptorImageInfo imageInfo = { this->m_postSampler, this->m_postColor.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    write.dstSet = set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(this->m_vk.m_device, 1, &write, 0, nullptr);

    VkRenderPassBeginInfo begin = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    begin.renderPass = this->m_postPass;
    begin.framebuffer = this->m_postFramebuffers[this->m_vk.m_imageIndex];
    begin.renderArea.extent = { width, height };
    vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport = {};
    viewport.width = static_cast<float>(width);
    viewport.height = static_cast<float>(height);
    viewport.maxDepth = 1.0f;
    VkRect2D scissor = {};
    scissor.extent = { width, height };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    float push[4] = { 1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height), WowDecompVk_Fxaa() ? 1.0f : 0.0f, WowDecompVk_Sharpen() * 0.3f };
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, this->m_postPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, this->m_postLayout, 0, 1, &set, 0, nullptr);
    vkCmdPushConstants(cmd, this->m_postLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
    this->m_dynamicValid = 0;
}

void CGxDeviceVulkan::ISceneEnd() {
    if (!this->m_frameActive) {
        return;
    }

    int32_t drew = this->m_frameCleared;
    this->IEnsureFrameImage();
    this->IEndPass();

    if (drew) {
        this->IApplyPost();
    }

    if (!this->m_vk.EndFrame()) {
        this->m_swapchainDirty = 1;
    }

    this->m_frameActive = 0;
}

void CGxDeviceVulkan::ISceneAbort() {
    if (!this->m_frameActive) {
        return;
    }

    // ʕ ◕ᴥ◕ ʔ✿ An acquired image only returns to the swapchain via present, in PRESENT_SRC layout ✿ ʕ ◕ᴥ◕ ʔ
    this->IEnsureFrameImage();
    this->IEndPass();

    if (!this->m_vk.EndFrame()) {
        this->m_swapchainDirty = 1;
    }

    this->m_frameActive = 0;
}

void CGxDeviceVulkan::IBeginPass() {
    if (this->m_passActive) {
        return;
    }

    VkClearValue clears[2] = {};
    clears[1].depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo info = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    info.clearValueCount = 2;
    info.pClearValues = clears;

    if (this->m_passTarget) {
        // ʕ •ᴥ•ʔ✿ The offscreen pass loads SHADER_READ. Do that transition here, on the same buffer as the draw ✿ ʕ •ᴥ•ʔ
        if (this->m_passTarget->layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && this->m_vk.Frame().recording) {
            VkContext::Barrier(this->m_vk.Frame().cmd, this->m_passTarget->image.image, VK_IMAGE_ASPECT_COLOR_BIT, this->m_passTarget->levels, this->m_passTarget->layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            this->m_passTarget->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }

        info.renderPass = this->m_vk.m_passes[VkContext::Pass_Offscreen];
        info.framebuffer = this->ITexFramebuffer(this->m_passTarget);
        this->m_targetExtent = { this->m_passTarget->width, this->m_passTarget->height };
    } else {
        info.renderPass = this->m_vk.m_passes[this->m_frameCleared ? VkContext::Pass_Load : VkContext::Pass_Clear];
        info.framebuffer = this->m_vk.m_swapFramebuffers[this->m_vk.m_imageIndex];
        this->m_targetExtent = this->m_vk.m_extent;
        this->m_frameCleared = 1;
    }

    if (!info.framebuffer) {
        return;
    }

    info.renderArea.extent = this->m_targetExtent;
    vkCmdBeginRenderPass(this->m_vk.Frame().cmd, &info, VK_SUBPASS_CONTENTS_INLINE);

    this->m_passActive = 1;
    this->m_dynamicValid = 0;
}

void CGxDeviceVulkan::IEndPass() {
    if (this->m_passActive) {
        vkCmdEndRenderPass(this->m_vk.Frame().cmd);
        this->m_passActive = 0;
    }
}

void CGxDeviceVulkan::IEnsureFrameImage() {
    if (this->m_frameCleared) {
        return;
    }

    VkTexture* target = this->m_passTarget;
    this->IEndPass();
    this->m_passTarget = nullptr;
    this->IBeginPass();
    this->IEndPass();
    this->m_passTarget = target;
}

void CGxDeviceVulkan::ScenePresent() {
    if (this->m_context) {
        int32_t scrShotClick = this->m_scrShotClick;

        CGxDevice::ScenePresent();

        if (this->m_frameActive) {
            this->ICursorDraw();

            if (scrShotClick) {
                this->DeviceScreenShot();
            }
        }

        if (!this->m_windowVisible) {
            int32_t savedFocus = this->m_windowFocus;
            this->m_windowFocus = 0;
            this->WaitForFPSCap();
            this->m_windowFocus = savedFocus;

            if (!GxGetMaxFPSBk()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(33));
            }
        } else if (!this->m_imageStarved) {
            this->WaitForFPSCap();
        }

        if (this->m_format.m_fixLag) {
            vkQueueWaitIdle(this->m_vk.m_queue);
        }

        this->ISceneEnd();
        // ʕ •ᴥ•ʔ✿ While invisible no frame ends; don't let uploads and garbage accumulate ✿ ʕ •ᴥ•ʔ
        this->m_vk.FlushUploads();
    }

    this->ISceneBegin();
}

void CGxDeviceVulkan::SceneClear(uint32_t mask, CImVector color) {
    if (!this->m_context || !this->m_frameActive || this->m_targetInvalid) {
        return;
    }

    if (this->m_viewportDirty) {
        this->IXformSetViewport();
    }

    this->IBeginPass();

    if (!this->m_passActive) {
        return;
    }

    VkClearAttachment attachments[2];
    uint32_t count = 0;

    if (mask & 0x1) {
        VkClearAttachment& attachment = attachments[count++];
        attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        attachment.colorAttachment = 0;
        attachment.clearValue.color = { { color.r * (1.0f / 255.0f), color.g * (1.0f / 255.0f), color.b * (1.0f / 255.0f), color.a * (1.0f / 255.0f) } };
    }

    if (mask & 0x2) {
        VkClearAttachment& attachment = attachments[count++];
        attachment.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        attachment.colorAttachment = 0;
        attachment.clearValue.depthStencil = { 1.0f, 0 };
    }

    int32_t minX = this->m_viewportRect.offset.x;
    int32_t minY = this->m_viewportRect.offset.y;
    int32_t maxX = minX + static_cast<int32_t>(this->m_viewportRect.extent.width);
    int32_t maxY = minY + static_cast<int32_t>(this->m_viewportRect.extent.height);

    if (this->m_appRenderStates[GxRs_ScissorTest].m_value.m_data.i[0]) {
        if (this->m_scissorRectDirty) {
            this->IStateSyncScissorRect();
        }

        minX = std::max(minX, this->m_scissorVk.offset.x);
        minY = std::max(minY, this->m_scissorVk.offset.y);
        maxX = std::min(maxX, this->m_scissorVk.offset.x + static_cast<int32_t>(this->m_scissorVk.extent.width));
        maxY = std::min(maxY, this->m_scissorVk.offset.y + static_cast<int32_t>(this->m_scissorVk.extent.height));
    }

    maxX = std::min(maxX, static_cast<int32_t>(this->m_targetExtent.width));
    maxY = std::min(maxY, static_cast<int32_t>(this->m_targetExtent.height));

    if (!count || maxX <= minX || maxY <= minY) {
        return;
    }

    VkClearRect rect = {};
    rect.rect = { { minX, minY }, { static_cast<uint32_t>(maxX - minX), static_cast<uint32_t>(maxY - minY) } };
    rect.layerCount = 1;

    vkCmdClearAttachments(this->m_vk.Frame().cmd, count, attachments, 1, &rect);
}

void CGxDeviceVulkan::XformSetProjection(const C44Matrix& matrix) {
    CGxDevice::XformSetProjection(matrix);

    C44Matrix projNative = matrix;

    if (!(fabs(projNative.c3 - 1.0f) < 0.00000023841858f) && !(fabs(projNative.c3) < 0.00000023841858f)) {
        projNative *= 1.0f / projNative.c3;
    }

    if (projNative.d3 == 0.0f) {
        float minZ = -(projNative.d2 / (projNative.c2 + 1.0f));
        float maxZ = -(projNative.d2 / (projNative.c2 - 1.0f));
        projNative.c2 = maxZ / (maxZ - minZ);
        projNative.d2 = maxZ * minZ / (minZ - maxZ);
    } else {
        float invC2 = 1.0f / projNative.c2;
        float minZ = (-1.0f - projNative.d2) * invC2;
        float maxZ = (1.0f - projNative.d2) * invC2;
        projNative.c2 = 1.0f / (maxZ - minZ);
        projNative.d2 = minZ / (minZ - maxZ);
    }

    if (!(this->m_appMasterEnables & (1 << GxMasterEnable_NormalProjection)) && projNative.d3 != 1.0f) {
        C44Matrix scale;
        scale.a0 = 0.2f;
        scale.b1 = 0.2f;
        scale.c2 = 0.2f;
        projNative = projNative * scale;
    }

    this->m_xforms[GxXform_Projection].m_dirty = 1;
    this->m_projNative = projNative;
}

void CGxDeviceVulkan::IXformSetViewport() {
    const CRect& window = this->DeviceCurWindow();

    uint32_t x = static_cast<uint32_t>(this->m_viewport.m_x.m_l * window.maxX + 0.5f);
    uint32_t y;
    uint32_t width = static_cast<uint32_t>(this->m_viewport.m_x.m_h * window.maxX - x + 0.5f);
    uint32_t height;

    // ʕ •ᴥ•ʔ✿ Render targets are top-left. Replacing only Y left the height measured from the flipped edge, so the draw landed outside the texture ✿ ʕ •ᴥ•ʔ
    if (this->m_textureTarget[GxBuffers_Color].m_apiSpecific || this->m_textureTarget[GxBuffers_Depth].m_apiSpecific) {
        y = static_cast<uint32_t>(window.maxY * this->m_viewport.m_y.m_l + 0.5f);
        height = static_cast<uint32_t>(this->m_viewport.m_y.m_h * window.maxY - y + 0.5f);
    } else {
        y = static_cast<uint32_t>((1.0f - this->m_viewport.m_y.m_h) * window.maxY + 0.5f);
        height = static_cast<uint32_t>((1.0f - this->m_viewport.m_y.m_l) * window.maxY - y + 0.5f);
    }

    this->m_viewportRect = { { static_cast<int32_t>(x), static_cast<int32_t>(y) }, { width, height } };
    this->m_viewportVk.x = static_cast<float>(x);
    this->m_viewportVk.y = static_cast<float>(y + height);
    this->m_viewportVk.width = static_cast<float>(width);
    this->m_viewportVk.height = -static_cast<float>(height);
    this->m_viewportVk.minDepth = this->m_viewport.m_z.m_l;
    this->m_viewportVk.maxDepth = this->m_viewport.m_z.m_h;

    this->m_viewportDirty = 0;
}

void CGxDeviceVulkan::IStateSyncScissorRect() {
    const CRect& window = this->DeviceCurWindow();

    int32_t left = static_cast<int32_t>(this->m_scissorRect.minX * window.maxX + 0.5f);
    int32_t right = static_cast<int32_t>(this->m_scissorRect.maxX * window.maxX + 1.0f);
    int32_t top;
    int32_t bottom;

    if (this->m_textureTarget[GxBuffers_Color].m_apiSpecific || this->m_textureTarget[GxBuffers_Depth].m_apiSpecific) {
        top = static_cast<int32_t>(this->m_scissorRect.minY * window.maxY + 0.5f);
        bottom = static_cast<int32_t>(1.0f + this->m_scissorRect.maxY * window.maxY);
    } else {
        top = static_cast<int32_t>((1.0f - this->m_scissorRect.maxY) * window.maxY + 0.5f);
        bottom = static_cast<int32_t>(1.0f + (1.0f - this->m_scissorRect.minY) * window.maxY);
    }

    left = std::max(left, 0);
    top = std::max(top, 0);
    right = std::min(right, static_cast<int32_t>(window.maxX));
    bottom = std::min(bottom, static_cast<int32_t>(window.maxY));

    this->m_scissorVk = { { left, top }, { static_cast<uint32_t>(std::max(right - left, 0)), static_cast<uint32_t>(std::max(bottom - top, 0)) } };
    this->m_scissorRectDirty = 0;
}

void CGxDeviceVulkan::ISetDynamicState() {
    VkCommandBuffer cmd = this->m_vk.Frame().cmd;

    VkRect2D scissor = { { 0, 0 }, this->m_targetExtent };

    if (this->m_appRenderStates[GxRs_ScissorTest].m_value.m_data.i[0]) {
        scissor = this->m_scissorVk;
    }

    float depthBias = -this->m_appRenderStates[GxRs_PolygonOffset].m_value.m_data.f[0] * 16777216.0f;
    float blendFactor = static_cast<uint32_t>(this->m_appRenderStates[Unk84].m_value.m_data.f[0] * 255.0f) * (1.0f / 255.0f);

    if (!this->m_dynamicValid || memcmp(&this->m_viewportVk, &this->m_boundViewport, sizeof(VkViewport))) {
        vkCmdSetViewport(cmd, 0, 1, &this->m_viewportVk);
        this->m_boundViewport = this->m_viewportVk;
    }

    if (!this->m_dynamicValid || memcmp(&scissor, &this->m_boundScissor, sizeof(VkRect2D))) {
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        this->m_boundScissor = scissor;
    }

    if (!this->m_dynamicValid || depthBias != this->m_boundDepthBias) {
        vkCmdSetDepthBias(cmd, depthBias, 0.0f, 0.0f);
        this->m_boundDepthBias = depthBias;
    }

    if (!this->m_dynamicValid || blendFactor != this->m_boundBlendFactor) {
        float constants[4] = { blendFactor, blendFactor, blendFactor, blendFactor };
        vkCmdSetBlendConstants(cmd, constants);
        this->m_boundBlendFactor = blendFactor;
    }

    this->m_dynamicValid = 1;
}

void CGxDeviceVulkan::ISetTexGen(uint32_t tmu, int32_t texGen) {
    if (tmu >= static_cast<uint32_t>(this->m_caps.m_numTmus)) {
        return;
    }

    C44Matrix matrix;

    if (texGen == GxTexGen_ObjectLinear || texGen == GxTexGen_EyeLinear) {
        CGxMatrixStack& view = this->m_xforms[GxXform_View];
        matrix = view.m_mtx[view.m_level].AffineInverse();

        if (texGen == GxTexGen_ObjectLinear) {
            CGxMatrixStack& world = this->m_xforms[GxXform_World];
            matrix = matrix * world.m_mtx[world.m_level].Inverse();
        }
    } else if (texGen == GxTexGen_SphereMap) {
        matrix.a0 = 0.5f;
        matrix.b1 = 0.5f;
        matrix.d0 = 0.5f;
        matrix.d1 = 0.5f;
    } else {
        this->m_texGen[tmu].Identity();
        return;
    }

    CGxMatrixStack& stack = this->m_texGen[tmu];
    stack.m_dirty = 1;
    stack.m_flags[stack.m_level] &= ~CGxMatrixStack::F_Identity;
    stack.m_mtx[stack.m_level] = matrix;
}

void CGxDeviceVulkan::ResolveTexMatrix(uint32_t tmu, C44Matrix& matrix) {
    int32_t mode = 0;
    this->IXformTex(tmu, mode, matrix);
}

void CGxDeviceVulkan::IXformTex(uint32_t tmu, int32_t& mode, C44Matrix& matrix) {
    CGxMatrixStack& texGen = this->m_texGen[tmu];
    CGxMatrixStack& xform = this->m_xforms[tmu];
    mode = 0;

    switch (this->m_appRenderStates[GxRs_TextureShader0 + tmu].m_value.m_data.i[0]) {
        case 0:
            matrix = texGen.m_mtx[texGen.m_level];
            mode = (texGen.m_flags[texGen.m_level] & CGxMatrixStack::F_Identity) ? 0 : 1;
            break;

        case 1: {
            C44Matrix product = texGen.m_mtx[texGen.m_level] * xform.m_mtx[xform.m_level];
            matrix = product;

            if (!this->m_appRenderStates[GxRs_TexGen0 + tmu].m_value.m_data.i[0]) {
                matrix.c0 = product.d0;
                matrix.c1 = product.d1;
            }

            mode = 1;
            break;
        }

        case 2:
            matrix = texGen.m_mtx[texGen.m_level] * xform.m_mtx[xform.m_level];
            mode = 2;
            break;

        default:
            break;
    }

    xform.m_dirty = 0;
    texGen.m_dirty = 0;
}

void CGxDeviceVulkan::IBuildUniforms(VkDrawUniforms& uniforms) {
    memset(&uniforms, 0, sizeof(VkDrawUniforms));

    const CGxAppRenderState* rs = this->m_appRenderStates.m_data;
    CGxMatrixStack& world = this->m_xforms[GxXform_World];
    CGxMatrixStack& view = this->m_xforms[GxXform_View];

    uniforms.world = world.m_mtx[world.m_level];
    uniforms.view = view.m_mtx[view.m_level];
    uniforms.proj = this->m_projNative;

    for (uint32_t tmu = 0; tmu < 2; tmu++) {
        int32_t enabled = rs[GxRs_Texture0 + tmu].m_value.m_data.p != nullptr;
        uniforms.stageColorOp[tmu] = enabled ? rs[GxRs_ColorOp0 + tmu].m_value.m_data.i[0] : -1;
        uniforms.stageAlphaOp[tmu] = enabled ? rs[GxRs_AlphaOp0 + tmu].m_value.m_data.i[0] : -1;
        uniforms.stageTexGen[tmu] = rs[GxRs_TexGen0 + tmu].m_value.m_data.i[0];
        uniforms.stageCoord[tmu] = rs[GxRs_TextureCoord0 + tmu].m_value.m_data.u[0] != 0;
        this->IXformTex(tmu, uniforms.stageTexMode[tmu], uniforms.texMatrix[tmu]);
    }

    uniforms.stageColorOp[2] = WowDecompVk_MsdfView();
    uniforms.stageColorOp[3] = -1;

    // ʕ •ᴥ•ʔ✿ stageColorOp.z is unused by the combiners. It flags the Desaturate stand-in ✿ ʕ •ᴥ•ʔ
    if (ShaderDesaturates(static_cast<CGxShader*>(rs[GxRs_PixelShader].m_value.m_data.p))) {
        uniforms.stageColorOp[2] = 1;
    }
    uniforms.stageAlphaOp[2] = -1;
    uniforms.stageAlphaOp[3] = -1;

    int32_t hasColor = (this->m_primVertexMask & (1 << GxVA_Color0)) != 0;
    uint32_t matDiffuse = rs[GxRs_MatDiffuse].m_value.m_data.u[0];

    ColorToFloats(matDiffuse, uniforms.matDiffuse);
    ColorToFloats(rs[GxRs_MatEmissive].m_value.m_data.u[0], uniforms.matEmissive);

    if (rs[GxRs_Lighting].m_value.m_data.i[0]) {
        uniforms.flags[0] = this->m_appMasterEnables & 0x1;

        for (uint32_t i = 0; i < 4; i++) {
            const CGxApiLight& light = this->m_lights[i];

            if (!light.m_enable) {
                continue;
            }

            uniforms.lightPos[i][0] = light.m_dir.x;
            uniforms.lightPos[i][1] = light.m_dir.y;
            uniforms.lightPos[i][2] = light.m_dir.z;
            uniforms.lightPos[i][3] = light.m_dir.w == 1.0f ? 1.0f : 0.0f;
            uniforms.lightDiffuse[i][0] = light.m_dirColor.x;
            uniforms.lightDiffuse[i][1] = light.m_dirColor.y;
            uniforms.lightDiffuse[i][2] = light.m_dirColor.z;
            uniforms.lightDiffuse[i][3] = 1.0f;
            uniforms.lightAmbient[i][0] = light.m_ambColor.x;
            uniforms.lightAmbient[i][1] = light.m_ambColor.y;
            uniforms.lightAmbient[i][2] = light.m_ambColor.z;
            uniforms.lightAtten[i][0] = light.m_constantAttenuation;
            uniforms.lightAtten[i][1] = light.m_linearAttenuation;
            uniforms.lightAtten[i][2] = light.m_quadraticAttenuation;
        }
    } else if (!hasColor && matDiffuse != 0xFFFFFFFF) {
        uniforms.flags[0] = 1;
        uniforms.ambient[0] = 1.0f;
        uniforms.ambient[1] = 1.0f;
        uniforms.ambient[2] = 1.0f;
        uniforms.ambient[3] = 1.0f;
    }

    uniforms.flags[1] = hasColor ? rs[GxRs_ColorMaterial].m_value.m_data.i[0] : -1;
    uniforms.flags[2] = (this->m_appMasterEnables & 0x2) && rs[GxRs_Fog].m_value.m_data.i[0];
    uniforms.flags[3] = rs[GxRs_AlphaRef].m_value.m_data.i[0] > 0 ? rs[GxRs_AlphaRef].m_value.m_data.i[0] : 0;

    ColorToFloats(rs[GxRs_FogColor].m_value.m_data.u[0], uniforms.fogColor);
    uniforms.fogParams[0] = rs[GxRs_FogStart].m_value.m_data.f[0];
    uniforms.fogParams[1] = rs[GxRs_FogEnd].m_value.m_data.f[0];
    uniforms.fogParams[2] = WowDecompVk_MsdfRange();
    uniforms.fogParams[3] = WowDecompVk_MsdfSharp();

    float pointSize = rs[GxRs_PointScale].m_value.m_data.f[0];
    uniforms.misc[0] = pointSize > 1.0f && this->m_vk.m_features.largePoints ? pointSize : 1.0f;
    uniforms.misc[1] = static_cast<float>(this->m_gpuDrawMode);
    uniforms.misc[2] = this->m_skinThisDraw ? 1.0f : 0.0f;
    uniforms.misc[3] = this->m_skinThisDraw ? static_cast<float>(this->m_skinInfluences) : 0.0f;

    if (WowDecompVk_Unlit()) {
        uniforms.flags[0] = 0;
    }
}

void CGxDeviceVulkan::IRsSendToHw(EGxRenderState which) {
    // ʕ •ᴥ•ʔ✿ One tick per state IRsSync actually pushes. Draw rebuilds when this moves ✿ ʕ •ᴥ•ʔ
    this->m_uniformEpoch++;

    if (which >= GxRs_TexGen0 && which <= GxRs_TexGen7) {
        this->ISetTexGen(which - GxRs_TexGen0, this->m_appRenderStates[which].m_value.m_data.i[0]);
    }
}

VkPipeline CGxDeviceVulkan::IPipeline(const VkPipelineKey& key) {
    if (this->m_lastPipeline && this->m_lastPipelineKey == key) {
        return this->m_lastPipeline;
    }

    auto it = this->m_pipelines.find(key);

    if (it != this->m_pipelines.end()) {
        this->m_lastPipelineKey = key;
        this->m_lastPipeline = it->second;
        return it->second;
    }

    VkVertexInputBindingDescription bindings[MaxBindings];

    for (uint32_t i = 0; i < key.bindingCount; i++) {
        bindings[i] = { i, key.bindingStride[i], VK_VERTEX_INPUT_RATE_VERTEX };
    }

    VkVertexInputAttributeDescription attribs[7];

    for (uint32_t i = 0; i < 7; i++) {
        VkFormat format = s_gxAttribToVkFormat[key.attribType[i]];

        if (i == 5) {
            format = VK_FORMAT_R8G8B8A8_UNORM;
        } else if (i == 6) {
            // ʕ •ᴥ•ʔ✿ Type 1 in s_gxAttribToVkFormat is unorm. Bone indices are raw ids ✿ ʕ •ᴥ•ʔ
            format = VK_FORMAT_R8G8B8A8_UINT;
        }
        attribs[i] = { i, key.attribBinding[i], format, key.attribOffset[i] };
    }

    VkPipelineVertexInputStateCreateInfo vertexInput = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vertexInput.vertexBindingDescriptionCount = key.bindingCount;
    vertexInput.pVertexBindingDescriptions = bindings;
    vertexInput.vertexAttributeDescriptionCount = 7;
    vertexInput.pVertexAttributeDescriptions = attribs;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    inputAssembly.topology = s_primitiveConversion[key.topology];

    VkPipelineViewportStateCreateInfo viewport = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    raster.polygonMode = key.fill ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
    raster.cullMode = key.cull ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
    raster.frontFace = key.cull == 1 ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
    raster.depthBiasEnable = VK_TRUE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    depth.depthTestEnable = key.unused;
    depth.depthWriteEnable = key.depthWrite;
    depth.depthCompareOp = s_cmpFunc[key.depthFunc];

    VkPipelineColorBlendAttachmentState attachment = {};
    attachment.blendEnable = key.blend >= GxBlend_Alpha;
    attachment.srcColorBlendFactor = s_srcBlend[key.blend];
    attachment.dstColorBlendFactor = s_dstBlend[key.blend];
    attachment.colorBlendOp = VK_BLEND_OP_ADD;
    attachment.srcAlphaBlendFactor = s_srcBlend[key.blend];
    attachment.dstAlphaBlendFactor = s_dstBlend[key.blend];
    attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    attachment.colorWriteMask = key.colorMask;

    VkPipelineColorBlendStateCreateInfo blend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;

    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_BIAS, VK_DYNAMIC_STATE_BLEND_CONSTANTS };

    VkPipelineDynamicStateCreateInfo dynamic = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynamic.dynamicStateCount = 4;
    dynamic.pDynamicStates = dynamicStates;

    VkShaderModule fragment = key.bindless ? this->m_terrainFragment : this->m_fragmentModule;
    VkPipelineShaderStageCreateInfo stages[2] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, this->m_vertexModule, "main", nullptr },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragment, "main", nullptr },
    };

    VkGraphicsPipelineCreateInfo info = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &inputAssembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = key.bindless ? this->m_terrainPipelineLayout : this->m_pipelineLayout;
    info.renderPass = this->m_vk.m_passes[key.pass ? VkContext::Pass_Offscreen : VkContext::Pass_Load];

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(this->m_vk.m_device, this->m_pipelineCache, 1, &info, nullptr, &pipeline) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    this->m_pipelines[key] = pipeline;
    this->m_lastPipelineKey = key;
    this->m_lastPipeline = pipeline;

    return pipeline;
}

VkSampler CGxDeviceVulkan::ISampler(const CGxTexFlags& flags) {
    uint32_t anisotropy = 1;

    if (flags.m_filter == GxTex_Anisotropic && this->m_caps.m_texFilterAnisotropic) {
        anisotropy = std::min<uint32_t>(std::max<uint32_t>(flags.m_maxAnisotropy, 1), this->m_caps.m_maxTexAnisotropy);
    }

    uint32_t key = flags.m_filter | (flags.m_wrapU << 3) | (flags.m_wrapV << 4) | (anisotropy << 5);
    auto it = this->m_samplers.find(key);

    if (it != this->m_samplers.end()) {
        return it->second;
    }

    VkSamplerCreateInfo info = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    info.magFilter = s_filters[flags.m_filter];
    info.minFilter = s_filters[flags.m_filter];
    info.mipmapMode = s_mipFilters[flags.m_filter];
    info.addressModeU = s_wrapModes[flags.m_wrapU];
    info.addressModeV = s_wrapModes[flags.m_wrapV];
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.anisotropyEnable = anisotropy > 1;
    info.maxAnisotropy = static_cast<float>(anisotropy);
    info.maxLod = flags.m_filter <= GxTex_Linear ? 0.25f : VK_LOD_CLAMP_NONE;

    VkSampler sampler = VK_NULL_HANDLE;
    if (vkCreateSampler(this->m_vk.m_device, &info, nullptr, &sampler) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    this->m_samplers[key] = sampler;

    return sampler;
}

int32_t CGxDeviceVulkan::GpuSkinSupported() const {
    return 1;
}

static int32_t ReserveBonePalettes(VkContext& vk, VkFrame& frame, uint32_t count, VkDeviceSize& cursor, VkDeviceSize& slot) {
    if (!frame.bones.mapped || !count) {
        return 0;
    }

    VkDeviceSize align = vk.m_properties.limits.minStorageBufferOffsetAlignment;

    if (align < 1) {
        align = 1;
    }

    VkDeviceSize visible = frame.bones.size;
    VkDeviceSize limit = vk.m_properties.limits.maxStorageBufferRange;

    if (limit && visible > limit) {
        visible = limit;
    }

    slot = (256 * sizeof(C44Matrix) + align - 1) / align * align;
    cursor = (frame.boneCursor + align - 1) / align * align;

    if (cursor + slot * count > visible) {
        return 0;
    }

    return 1;
}

static void WriteBonePalette(uint8_t* dst, const C44Matrix* bones, uint32_t boneCount) {
    memcpy(dst, bones, boneCount * sizeof(C44Matrix));

    if (boneCount < 256) {
        memset(dst + boneCount * sizeof(C44Matrix), 0, (256 - boneCount) * sizeof(C44Matrix));
    }
}

// ʕ •ᴥ•ʔ✿ The ring cursor returns to 0 when the frame slot is prepared. That is the only generation the device file can see ✿ ʕ •ᴥ•ʔ
struct BonePaletteCache {
    const C44Matrix* bones = nullptr;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    uint32_t count = 0;
    uint32_t base = 0;
    uint32_t epoch = 0;
};

static BonePaletteCache s_bonePalette;
static uint32_t s_boneEpoch = 0;
static int32_t s_boneRingOpen = 0;

static void NoteBoneRing(VkDeviceSize cursor) {
    if (cursor != 0) {
        return;
    }

    if (s_boneRingOpen) {
        s_boneEpoch++;
    }

    s_boneRingOpen = 1;
}

int32_t CGxDeviceVulkan::GpuSkinBind(const C44Matrix* bones, uint32_t boneCount) {
    this->m_skinActive = 0;
    this->m_skinStride = 0;
    this->m_drawInstances = 0;

    if (!bones || !boneCount || boneCount > 256 || !this->m_context) {
        return 0;
    }

    this->m_vk.PrepareFrame();
    VkFrame& frame = this->m_vk.Frame();
    NoteBoneRing(frame.boneCursor);

    // ʕ •ᴥ•ʔ✿ The next M2 geoset binds this same skeleton. Keep the slot so Draw's push matches and is skipped ✿ ʕ •ᴥ•ʔ
    if (s_bonePalette.bones == bones && s_bonePalette.count == boneCount && s_bonePalette.epoch == s_boneEpoch && s_bonePalette.buffer == frame.bones.buffer && frame.bones.mapped) {
        this->m_skinOffset = s_bonePalette.offset;
        this->m_skinMatrixBase = s_bonePalette.base;
        this->m_skinActive = 1;
        return 1;
    }

    VkDeviceSize cursor = 0;
    VkDeviceSize slot = 0;

    if (!ReserveBonePalettes(this->m_vk, frame, 1, cursor, slot)) {
        return 0;
    }

    WriteBonePalette(frame.bones.mapped + cursor, bones, boneCount);
    frame.boneCursor = cursor + slot;
    this->m_skinOffset = cursor;
    this->m_skinMatrixBase = static_cast<uint32_t>(cursor / sizeof(C44Matrix));
    s_bonePalette.bones = bones;
    s_bonePalette.buffer = frame.bones.buffer;
    s_bonePalette.offset = cursor;
    s_bonePalette.count = boneCount;
    s_bonePalette.base = this->m_skinMatrixBase;
    s_bonePalette.epoch = s_boneEpoch;
    this->m_skinActive = 1;
    return 1;
}

int32_t CGxDeviceVulkan::GpuSkinBindInstances(const C44Matrix* const* bones, uint32_t instances, uint32_t boneCount) {
    this->m_skinActive = 0;
    this->m_skinStride = 0;
    this->m_drawInstances = 0;

    if (!bones || !instances || !boneCount || boneCount > 256 || !this->m_context) {
        return 0;
    }

    this->m_vk.PrepareFrame();
    VkFrame& frame = this->m_vk.Frame();
    NoteBoneRing(frame.boneCursor);
    VkDeviceSize cursor = 0;
    VkDeviceSize slot = 0;

    if (!ReserveBonePalettes(this->m_vk, frame, instances, cursor, slot)) {
        return 0;
    }

    for (uint32_t i = 0; i < instances; ++i) {
        if (!bones[i]) {
            return 0;
        }

        WriteBonePalette(frame.bones.mapped + cursor + slot * i, bones[i], boneCount);
    }

    frame.boneCursor = cursor + slot * instances;
    this->m_skinOffset = cursor;
    this->m_skinMatrixBase = static_cast<uint32_t>(cursor / sizeof(C44Matrix));
    this->m_skinStride = static_cast<uint32_t>(slot / sizeof(C44Matrix));
    this->m_drawInstances = instances;
    this->m_skinActive = 1;
    return 1;
}

void CGxDeviceVulkan::GpuSkinMesh(int32_t bindPose) {
    this->m_bindPoseMesh = bindPose;
}

void CGxDeviceVulkan::GpuSkinInfluences(uint32_t count) {
    this->m_skinInfluences = count;
}

int32_t CGxDeviceVulkan::GpuSkinMesh() const {
    return this->m_bindPoseMesh;
}

void CGxDeviceVulkan::Draw(CGxBatch* batch, int32_t count) {
    Profiler::Scope profile(Profiler::GxDraw);
    struct ResetGpuDraw {
        CGxDeviceVulkan* self;
        ~ResetGpuDraw() {
            self->m_gpuDrawMode = 0;
            self->m_drawInstances = 0;
            self->m_firstInstance = 0;
            self->m_indirectCount = 0;
        }
    } resetGpuDraw{this};

    if (!this->m_context || this->m_intF5C || !this->m_frameActive || this->m_targetInvalid) {
        return;
    }

    this->m_skinThisDraw = this->m_skinActive;
    this->m_skinOffsetThis = this->m_skinOffset;
    this->m_skinMatrixBaseThis = this->m_skinMatrixBase;
    this->m_skinStrideThis = this->m_skinStride;
    this->m_skinActive = 0;

    // ʕ •ᴥ•ʔ✿ Copies are illegal inside a render pass. End it, upload, and the next begin loads color and depth ✿ ʕ •ᴥ•ʔ
    if (!this->m_vk.m_residentUploads.empty() && this->m_vk.Frame().recording) {
        this->IEndPass();
        this->m_vk.FlushResidentUploads(this->m_vk.Frame().cmd);
    }

    this->IRsSync(0);

    const CGxAppRenderState* rs = this->m_appRenderStates.m_data;
    uint32_t enables = this->m_appMasterEnables;

    VkPipelineKey key;
    memset(&key, 0, sizeof(key));

    CGxBuf* streamBufs[MaxBindings];
    uint32_t strides[MaxBindings] = {};
    uint32_t streamCount = 0;
    uint32_t locations = 0;

    for (uint32_t i = 0; i < GxVAs_Last; i++) {
        if (!(this->m_primVertexMask & (1 << i))) {
            continue;
        }

        CGxBuf* buf = this->m_primVertexFormatBuf[i];
        uint32_t stream = 0;

        while (stream < streamCount && streamBufs[stream] != buf) {
            stream++;
        }

        if (stream == streamCount) {
            if (streamCount == MaxBindings - 1) {
                return;
            }

            streamBufs[streamCount++] = buf;
        }

        const CGxVertexAttrib& attrib = this->m_primVertexFormatAttrib[i];
        strides[stream] += s_gxAttribSize[attrib.m_type];

        int32_t location = s_gxAttribLocation[i];

        if (location >= 0) {
            key.attribBinding[location] = static_cast<uint8_t>(stream);
            key.attribType[location] = static_cast<uint8_t>(attrib.m_type);
            key.attribOffset[location] = static_cast<uint16_t>(attrib.m_offset);
            locations |= 1 << location;
        }
    }

    if (!(locations & 0x1)) {
        return;
    }

    VkBuffer buffers[MaxBindings];
    VkDeviceSize offsets[MaxBindings];

    for (uint32_t i = 0; i < streamCount; i++) {
        if (!streamBufs[i] || !streamBufs[i]->m_pool || !streamBufs[i]->m_valid) {
            return;
        }

        VkBufferAlloc* alloc = static_cast<VkBufferAlloc*>(streamBufs[i]->m_pool->m_apiSpecific);

        if (!alloc) {
            return;
        }

        buffers[i] = alloc->buffer;
        offsets[i] = this->m_indirectCount ? 0 : streamBufs[i]->m_index;
        key.bindingStride[i] = static_cast<uint16_t>(strides[i]);
    }

    uint32_t bindingCount = streamCount;

    if (locations != 0x7F) {
        for (uint32_t location = 1; location < 7; location++) {
            if (!(locations & (1 << location))) {
                key.attribBinding[location] = static_cast<uint8_t>(bindingCount);
                if (location >= 5) {
                    key.attribType[location] = location == 6 ? 1 : 2;
                    key.attribOffset[location] = 0;
                } else {
                    key.attribType[location] = location == 2 ? 0 : 4;
                    key.attribOffset[location] = location == 2 ? 16 : 0;
                }
            }
        }

        buffers[bindingCount] = this->m_dummyVertices.buffer;
        offsets[bindingCount] = 0;
        bindingCount++;
    }

    key.bindingCount = static_cast<uint8_t>(bindingCount);
    key.topology = static_cast<uint8_t>(batch->m_primType);
    key.blend = static_cast<uint8_t>(std::min<uint32_t>(rs[GxRs_BlendingMode].m_value.m_data.u[0], GxBlends_Last - 1));
    int32_t depthTestOn = (enables & 0x4) && rs[GxRs_DepthTest].m_value.m_data.i[0];
    int32_t depthFunc = rs[GxRs_DepthFunc].m_value.m_data.i[0];

    if (depthFunc < 0) {
        depthFunc = 0;
    } else if (depthFunc > 3) {
        depthFunc = 3;
    }

    key.unused = static_cast<uint8_t>(depthTestOn);
    key.depthFunc = depthTestOn ? static_cast<uint8_t>(depthFunc) : DepthFunc_Always;
    key.depthWrite = (enables & 0x8) && rs[GxRs_DepthWrite].m_value.m_data.i[0];
    key.colorMask = (enables & 0x10) ? static_cast<uint8_t>(rs[GxRs_ColorWrite].m_value.m_data.u[0] & 0xF) : 0;
    key.cull = (enables & 0x20) ? static_cast<uint8_t>(std::min<uint32_t>(rs[GxRs_Culling].m_value.m_data.u[0], 2)) : 0;
    // ʕ •ᴥ•ʔ✿ UI draws with depth off, so the experiment does not turn the options frame into lines ✿ ʕ •ᴥ•ʔ
    int32_t gameWire = (enables & (1 << GxMasterEnable_PolygonFill)) == 0;
    int32_t forceWire = WowDecompVk_Wireframe() && depthTestOn;
    key.fill = (gameWire || forceWire) && this->m_vk.m_features.fillModeNonSolid;
    key.pass = this->m_passTarget != nullptr;
    key.bindless = this->m_gpuDrawMode >= 2 && this->m_bindless;

    VkPipeline pipeline = this->IPipeline(key);

    if (!pipeline) {
        return;
    }

    if (this->m_viewportDirty) {
        this->IXformSetViewport();
    }

    if (this->m_scissorRectDirty) {
        this->IStateSyncScissorRect();
    }

    if (!this->m_viewportRect.extent.width || !this->m_viewportRect.extent.height) {
        return;
    }

    VkImageView views[2];
    VkSampler samplers[2];
    int32_t disableTexStage[2] = {};

    for (uint32_t tmu = 0; tmu < 2; tmu++) {
        CGxTex* texId = static_cast<CGxTex*>(rs[GxRs_Texture0 + tmu].m_value.m_data.p);
        VkTexture* texture = nullptr;

        if (texId && !GxCpuPtrOk(texId)) {
            texId = nullptr;
        }

        if (texId) {
            if (texId->m_apiSpecificData && !GxCpuPtrOk(texId->m_apiSpecificData)) {
                texId->m_apiSpecificData = nullptr;
                texId->m_needsCreation = 1;
            }

            if (texId->m_needsCreation || !texId->m_apiSpecificData) {
                this->ITexReady(texId);
            }

            if (texId->m_needsUpdate) {
                this->ITexMarkAsUpdated(texId);
            }
            texture = static_cast<VkTexture*>(texId->m_apiSpecificData);

            if (!texture || !GxCpuPtrOk(texture)) {
                texture = nullptr;
                disableTexStage[tmu] = 1;
            }
        }

        if (!texture) {
            texture = this->ITexReady(this->m_defaultTexture);
        }

        if (!texture) {
            return;
        }

        views[tmu] = texture->image.view;
        samplers[tmu] = this->ISampler(texId ? texId->m_flags : CGxTexFlags());

        if (!samplers[tmu]) {
            return;
        }
    }

    // ʕ •ᴥ•ʔ✿ Snapshot before the build. IXformTex clears tex dirty while it fills the block ✿ ʕ •ᴥ•ʔ
    int32_t disableTex = disableTexStage[0] | (disableTexStage[1] << 1);
    int32_t matrixDirty = this->m_xforms[GxXform_World].m_dirty
        || this->m_xforms[GxXform_View].m_dirty
        || this->m_xforms[GxXform_Projection].m_dirty
        || this->m_xforms[GxXform_Tex0].m_dirty
        || this->m_xforms[GxXform_Tex1].m_dirty
        || this->m_texGen[0].m_dirty
        || this->m_texGen[1].m_dirty;
    int32_t rebuildUniforms = !this->m_uniformsValid
        || matrixDirty
        || this->m_uniformEpoch != this->m_uniformEpochBuilt
        || this->m_uniformPrimMask != this->m_primVertexMask
        || this->m_uniformMasterEnables != this->m_appMasterEnables
        || this->m_uniformGpuDrawMode != this->m_gpuDrawMode
        || this->m_uniformSkin != this->m_skinThisDraw
        || this->m_uniformInfluences != static_cast<int32_t>(this->m_skinInfluences)
        || this->m_uniformDisableTex != disableTex;

    if (!rebuildUniforms && memcmp(this->m_lights, this->m_uniformLights, sizeof(this->m_lights))) {
        rebuildUniforms = 1;
    }

    this->IBeginPass();

    if (!this->m_passActive) {
        return;
    }

    VkFrame& frame = this->m_vk.Frame();
    VkCommandBuffer cmd = frame.cmd;

    if (rebuildUniforms) {
        VkDrawUniforms uniforms;
        this->IBuildUniforms(uniforms);

        for (uint32_t tmu = 0; tmu < 2; tmu++) {
            if (disableTexStage[tmu]) {
                uniforms.stageColorOp[tmu] = -1;
                uniforms.stageAlphaOp[tmu] = -1;
            }
        }

        if (!this->m_uniformsValid || memcmp(&uniforms, &this->m_uniforms, sizeof(VkDrawUniforms))) {
            VkDeviceSize alignment = this->m_vk.m_properties.limits.minUniformBufferOffsetAlignment;
            VkDeviceSize offset = (frame.uniformOffset + alignment - 1) / alignment * alignment;

            if (offset + sizeof(VkDrawUniforms) > frame.uniforms.size) {
                // ʕ •ᴥ•ʔ✿ Never draw with the previous draw's matrices. Tex dirty was consumed, so the next draw must rebuild ✿ ʕ •ᴥ•ʔ
                this->m_uniformsValid = 0;
                return;
            }
            memcpy(frame.uniforms.mapped + offset, &uniforms, sizeof(VkDrawUniforms));
            frame.uniformOffset = offset + sizeof(VkDrawUniforms);
            this->m_uniformOffset = offset;
            this->m_uniforms = uniforms;
            this->m_uniformsValid = 1;
        }

        this->m_uniformEpochBuilt = this->m_uniformEpoch;
        this->m_uniformPrimMask = this->m_primVertexMask;
        this->m_uniformMasterEnables = this->m_appMasterEnables;
        this->m_uniformGpuDrawMode = this->m_gpuDrawMode;
        this->m_uniformSkin = this->m_skinThisDraw;
        this->m_uniformInfluences = static_cast<int32_t>(this->m_skinInfluences);
        this->m_uniformDisableTex = disableTex;
        memcpy(this->m_uniformLights, this->m_lights, sizeof(this->m_lights));
        // ʕ •ᴥ•ʔ✿ Dirty means changed since the last landed build. Tex stacks are cleared in IXformTex ✿ ʕ •ᴥ•ʔ
        this->m_xforms[GxXform_World].m_dirty = 0;
        this->m_xforms[GxXform_View].m_dirty = 0;
        this->m_xforms[GxXform_Projection].m_dirty = 0;
    }

    if (pipeline != this->m_boundPipeline) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        this->m_boundPipeline = pipeline;
    }

    this->ISetDynamicState();

    int32_t setChanged = !this->m_boundSet || memcmp(views, this->m_setViews, sizeof(views)) || memcmp(samplers, this->m_setSamplers, sizeof(samplers));

    if (setChanged) {
        VkSetKey setKey = { { views[0], views[1] }, { samplers[0], samplers[1] } };
        int32_t created = 0;
        VkDescriptorSet set = this->m_vk.CachedSet(setKey, this->m_setLayout, created);

        if (!set) {
            return;
        }

        if (created) {
        VkDescriptorBufferInfo bufferInfo = { frame.uniforms.buffer, 0, sizeof(VkDrawUniforms) };
        VkDescriptorBufferInfo itemInfo = { frame.items.buffer, 0, VK_WHOLE_SIZE };
        VkDescriptorImageInfo imageInfo[2] = {
            { samplers[0], views[0], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            { samplers[1], views[1], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        };

        VkWriteDescriptorSet writes[5] = {};

        for (uint32_t i = 0; i < 4; i++) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = set;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
        }

        VkDeviceSize boneRange = frame.bones.size;
        VkDeviceSize boneLimit = this->m_vk.m_properties.limits.maxStorageBufferRange;

        if (boneLimit && boneRange > boneLimit) {
            boneRange = boneLimit;
        }

        VkDescriptorBufferInfo boneInfo = { frame.bones.buffer, 0, boneRange };
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet = set;
        writes[4].dstBinding = 4;
        writes[4].descriptorCount = 1;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[0].pBufferInfo = &bufferInfo;
        writes[1].pImageInfo = &imageInfo[0];
        writes[2].pImageInfo = &imageInfo[1];
        writes[3].pBufferInfo = &itemInfo;
        writes[4].pBufferInfo = &boneInfo;
        vkUpdateDescriptorSets(this->m_vk.m_device, 5, writes, 0, nullptr);
        }

        this->m_boundSet = set;
        memcpy(this->m_setViews, views, sizeof(views));
        memcpy(this->m_setSamplers, samplers, sizeof(samplers));
    }

    uint32_t dynamicOffset = static_cast<uint32_t>(this->m_uniformOffset);

    // ʕ •ᴥ•ʔ✿ The bone ring stays bound. A push constant picks the palette, so a new skeleton does not rebind the set ✿ ʕ •ᴥ•ʔ
    if (key.bindless) {
        VkDescriptorSet terrainSet = this->m_terrainSets[this->m_vk.m_frameIndex];

        if (!terrainSet) {
            return;
        }

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, this->m_terrainPipelineLayout, 0, 1, &this->m_boundSet, 1, &dynamicOffset);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, this->m_terrainPipelineLayout, 1, 1, &terrainSet, 0, nullptr);
        this->m_boundDynamicOffset = dynamicOffset;
    } else if (setChanged || dynamicOffset != this->m_boundDynamicOffset) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, this->m_pipelineLayout, 0, 1, &this->m_boundSet, 1, &dynamicOffset);
        this->m_boundDynamicOffset = dynamicOffset;
    }

    uint32_t pushBase = this->m_skinThisDraw ? this->m_skinMatrixBaseThis : 0;
    uint32_t pushInfluences = this->m_skinThisDraw ? this->m_skinInfluences : 0;
    uint32_t pushStride = this->m_skinThisDraw ? this->m_skinStrideThis : 0;

    if (pushBase != this->m_pushBase || pushInfluences != this->m_pushInfluences || pushStride != this->m_pushStride) {
        uint32_t push[3] = { pushBase, pushInfluences, pushStride };
        VkPipelineLayout pushLayout = key.bindless ? this->m_terrainPipelineLayout : this->m_pipelineLayout;
        vkCmdPushConstants(cmd, pushLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), push);
        this->m_pushBase = pushBase;
        this->m_pushInfluences = pushInfluences;
        this->m_pushStride = pushStride;
    }

    if (memcmp(buffers, this->m_boundBuffers, sizeof(VkBuffer) * bindingCount) || memcmp(offsets, this->m_boundOffsets, sizeof(VkDeviceSize) * bindingCount)) {
        vkCmdBindVertexBuffers(cmd, 0, bindingCount, buffers, offsets);
        memcpy(this->m_boundBuffers, buffers, sizeof(VkBuffer) * bindingCount);
        memcpy(this->m_boundOffsets, offsets, sizeof(VkDeviceSize) * bindingCount);
    }

    if (count) {
        if (!this->m_primIndexBuf || !this->m_primIndexBuf->m_pool || !this->m_primIndexBuf->m_valid) {
            return;
        }

        VkBufferAlloc* indices = static_cast<VkBufferAlloc*>(this->m_primIndexBuf->m_pool->m_apiSpecific);

        if (!indices) {
            return;
        }

        if (indices->buffer != this->m_boundIndexBuffer) {
            vkCmdBindIndexBuffer(cmd, indices->buffer, 0, VK_INDEX_TYPE_UINT16);
            this->m_boundIndexBuffer = indices->buffer;
        }

        if (this->m_indirectCount) {
            vkCmdDrawIndexedIndirect(cmd, frame.indirect.buffer, this->m_indirectOffset, this->m_indirectCount, sizeof(VkDrawIndexedIndirectCommand));
        } else {
            uint32_t instances = this->m_drawInstances ? this->m_drawInstances : 1;
            vkCmdDrawIndexed(cmd, batch->m_count, instances, batch->m_start + (this->m_primIndexBuf->m_index >> 1), 0, this->m_firstInstance);
        }
    } else {
        uint32_t instances = this->m_drawInstances ? this->m_drawInstances : 1;
        vkCmdDraw(cmd, batch->m_count, instances, 0, this->m_firstInstance);
    }
}

void CGxDeviceVulkan::DrawInstanced(CGxBatch* batch, const C44Matrix* worlds, uint32_t count) {
    if (!batch || !worlds || !count) {
        return;
    }

    uint32_t first = 0;
    GpuDrawItem* items = nullptr;

    if (!this->m_vk.AllocDrawItems(count, first, items)) {
        CGxDevice::DrawInstanced(batch, worlds, count);
        return;
    }

    for (uint32_t i = 0; i < count; ++i) {
        items[i] = {};
        memcpy(items[i].world, &worlds[i], sizeof(items[i].world));
    }

    this->m_gpuDrawMode = 1;
    this->m_drawInstances = count;
    this->m_firstInstance = first;
    this->Draw(batch, 1);
}

int32_t CGxDeviceVulkan::TerrainBindless() {
    return this->m_bindless;
}

void CGxDeviceVulkan::ResetTerrainTable() {
    this->m_terrainEpoch++;
    this->m_terrainCount = 0;
    uint32_t frame = this->m_vk.m_frameIndex;
    this->m_terrainSets[frame] = VK_NULL_HANDLE;

    if (this->m_terrainPools[frame]) {
        vkResetDescriptorPool(this->m_vk.m_device, this->m_terrainPools[frame], 0);
    }
}

int32_t CGxDeviceVulkan::EnsureTerrainSet() {
    uint32_t frame = this->m_vk.m_frameIndex;

    if (this->m_terrainSets[frame]) {
        return 1;
    }

    VkDevice device = this->m_vk.m_device;

    if (!this->m_terrainPools[frame]) {
        VkDescriptorPoolSize size = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, TerrainSlots };
        VkDescriptorPoolCreateInfo info = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        info.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        info.maxSets = 1;
        info.poolSizeCount = 1;
        info.pPoolSizes = &size;

        if (vkCreateDescriptorPool(device, &info, nullptr, &this->m_terrainPools[frame]) != VK_SUCCESS) {
            this->m_terrainPools[frame] = VK_NULL_HANDLE;
            return 0;
        }
    }

    VkDescriptorSetAllocateInfo alloc = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    alloc.descriptorPool = this->m_terrainPools[frame];
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &this->m_terrainSetLayout;

    if (vkAllocateDescriptorSets(device, &alloc, &this->m_terrainSets[frame]) != VK_SUCCESS) {
        this->m_terrainSets[frame] = VK_NULL_HANDLE;
        return 0;
    }

    return 1;
}

uint64_t CGxDeviceVulkan::ResidentHeapBytes() const {
    if (this->m_vk.m_rebarHeap) {
        return this->m_vk.m_rebarHeap;
    }

    return this->m_vk.m_localHeap;
}

uint32_t CGxDeviceVulkan::TerrainTextureSlot(CGxTex* tex) {
    if (!this->m_bindless) {
        return ~0u;
    }

    if (!tex || !GxCpuPtrOk(tex)) {
        tex = this->m_defaultTexture;
    }

    if (!tex || !GxCpuPtrOk(tex)) {
        return ~0u;
    }

    // ʕ •ᴥ•ʔ✿ The slot for this frame is already written. Skip the sampler and descriptor walk ✿ ʕ •ᴥ•ʔ
    VkTexture* cached = static_cast<VkTexture*>(tex->m_apiSpecificData);
    uint32_t frame = this->m_vk.m_frameIndex;

    if (cached && !tex->m_needsUpdate && cached->terrainEpoch == this->m_terrainEpoch && cached->terrainSlot != ~0u && (cached->terrainFrames & (1u << frame))) {
        return cached->terrainSlot;
    }

    this->ITexMarkAsUpdated(tex);
    VkTexture* texture = this->ITexReady(tex);

    if (!texture || !texture->image.view) {
        return ~0u;
    }

    VkSampler sampler = this->ISampler(tex->m_flags);

    if (!sampler) {
        return ~0u;
    }

    // -- ʕ •ᴥ•ʔ✿ Two frames are in flight, and each has its own set. A slot written on one is empty on the other. ✿ ʕ •ᴥ•ʔ
    int32_t sameSlot = texture->terrainEpoch == this->m_terrainEpoch && texture->terrainSlot != ~0u && texture->terrainView == texture->image.view && texture->terrainSampler == sampler;

    if (sameSlot && (texture->terrainFrames & (1u << frame))) {
        return texture->terrainSlot;
    }

    if (!sameSlot && this->m_terrainCount >= TerrainSlots) {
        return ~0u;
    }

    if (!this->EnsureTerrainSet()) {
        return ~0u;
    }

    uint32_t slot = sameSlot ? texture->terrainSlot : this->m_terrainCount++;
    VkDescriptorImageInfo image = { sampler, texture->image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    write.dstSet = this->m_terrainSets[frame];
    write.dstBinding = 0;
    write.dstArrayElement = slot;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image;
    vkUpdateDescriptorSets(this->m_vk.m_device, 1, &write, 0, nullptr);
    texture->terrainEpoch = this->m_terrainEpoch;
    texture->terrainSlot = slot;
    texture->terrainView = texture->image.view;
    texture->terrainSampler = sampler;
    texture->terrainFrames = sameSlot ? (texture->terrainFrames | (1u << frame)) : (1u << frame);
    return slot;
}

static float TerrainSlotFloat(uint32_t index) {
    return index == ~0u ? -1.0f : static_cast<float>(index);
}

static_assert(offsetof(GxTerrainDraw, indexCount) == sizeof(GpuDrawItem), "terrain record");

int32_t CGxDeviceVulkan::DrawTerrainBatch(const GxTerrainDraw* draws, uint32_t count, int32_t mode) {
    if (!draws || !count || !this->m_primIndexBuf || !this->m_vk.m_features.drawIndirectFirstInstance) {
        return 0;
    }

    uint32_t first = 0;
    GpuDrawItem* items = nullptr;
    VkDeviceSize indirectOffset = 0;
    VkDrawIndexedIndirectCommand* commands = nullptr;

    if (!this->m_vk.AllocDrawItems(count, first, items) || !this->m_vk.AllocIndirect(count, indirectOffset, commands)) {
        return 0;
    }

    for (uint32_t i = 0; i < count; ++i) {
        // ʕ •ᴥ•ʔ✿ The ring is coherent and uncached. Patching one float there reads a whole line back off the bus ✿ ʕ •ᴥ•ʔ
        GpuDrawItem item;
        memcpy(&item, &draws[i].world, sizeof(item));

        uint32_t layers = draws[i].layerCount > 4 ? 4 : draws[i].layerCount;

        if (layers) {
            for (uint32_t k = 0; k < 4; ++k) {
                item.lightAmbient[k][3] = k < layers ? TerrainSlotFloat(draws[i].layerTex[k]) : -1.0f;
                item.lightAtten[k][3] = k < layers ? TerrainSlotFloat(draws[i].layerAlpha[k]) : -1.0f;
            }
        } else {
            item.lightAmbient[0][3] = TerrainSlotFloat(draws[i].texIndex0);
            item.lightAmbient[1][3] = -1.0f;
            item.lightAmbient[2][3] = -1.0f;
            item.lightAmbient[3][3] = -1.0f;
            item.lightAtten[0][3] = TerrainSlotFloat(draws[i].texIndex1);
            item.lightAtten[1][3] = -1.0f;
            item.lightAtten[2][3] = -1.0f;
            item.lightAtten[3][3] = -1.0f;
        }

        memcpy(&items[i], &item, sizeof(item));
        commands[i].indexCount = draws[i].indexCount;
        commands[i].instanceCount = 1;
        commands[i].firstIndex = draws[i].firstIndex;
        commands[i].vertexOffset = draws[i].baseVertex;
        commands[i].firstInstance = first + i;
    }

    this->m_gpuDrawMode = mode == 3 ? 3 : 2;
    this->m_indirectCount = count;
    this->m_indirectOffset = indirectOffset;

    CGxBatch batch = {};
    batch.m_primType = GxPrim_Triangles;
    this->Draw(&batch, 1);
    return 1;
}

VkBufferAlloc* CGxDeviceVulkan::IPoolBuffer(CGxPool* pool) {
    if (!pool->m_apiSpecific && pool->m_size > 0) {
        VkBufferAlloc* alloc = new VkBufferAlloc();

        int32_t created = pool->m_resident
            ? this->m_vk.CreateResidentBuffer(pool->m_size, *alloc)
            : this->m_vk.AcquireBuffer(pool->m_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, *alloc);

        if (created) {
            pool->m_apiSpecific = alloc;
        } else {
            delete alloc;
        }
    }

    return static_cast<VkBufferAlloc*>(pool->m_apiSpecific);
}

void CGxDeviceVulkan::IReleasePool(CGxPool* pool) {
    VkBufferAlloc* alloc = static_cast<VkBufferAlloc*>(pool->m_apiSpecific);

    if (alloc) {
        if (alloc->buffer == this->m_boundIndexBuffer) {
            this->m_boundIndexBuffer = VK_NULL_HANDLE;
        }

        for (uint32_t i = 0; i < MaxBindings; ++i) {
            if (this->m_boundBuffers[i] == alloc->buffer) {
                this->m_boundBuffers[i] = VK_NULL_HANDLE;
            }
        }

        auto& uploads = this->m_vk.m_residentUploads;
        for (size_t i = 0; i < uploads.size();) {
            if (uploads[i].buffer == alloc->buffer) {
                uploads[i] = uploads.back();
                uploads.pop_back();
            } else {
                ++i;
            }
        }

        if (!alloc->borrowed) {
            this->m_vk.RetireBuffer(*alloc);
        }
        delete alloc;
        pool->m_apiSpecific = nullptr;
    }
}

void CGxDeviceVulkan::IReleasePools() {
    for (CGxPool* pool = this->m_poolList.Head(); pool; pool = this->m_poolList.Next(pool)) {
        this->IReleasePool(pool);
        // ʕ •ᴥ•ʔ✿ Static model buffers must upload again on the new device ✿ ʕ •ᴥ•ʔ
        pool->Discard();
    }
}

void CGxDeviceVulkan::PoolSizeSet(CGxPool* pool, uint32_t size) {
    this->IReleasePool(pool);
    pool->m_size = size;
}

void CGxDeviceVulkan::PoolDestroy(CGxPool* pool) {
    if (pool) {
        this->IReleasePool(pool);
    }

    CGxDevice::PoolDestroy(pool);
}

char* CGxDeviceVulkan::IBufLock(CGxBuf* buf) {
    CGxPool* pool = buf->m_pool;

    if (!this->m_context) {
        return static_cast<char*>(this->m_emergencyMem[pool->m_target].Lock(buf->m_size));
    }

    if (pool->m_usage == GxPoolUsage_Stream) {
        // ʕ •ᴥ•ʔ✿ Appending into the frame ring. Releasing the pool here allocated a new 2.75MB buffer every wrap ✿ ʕ •ᴥ•ʔ
        if (!buf->m_itemSize || !buf->m_size) {
            return static_cast<char*>(this->m_emergencyMem[pool->m_target].Lock(buf->m_size));
        }

        uint32_t index = 0;
        VkBufferAlloc slice = {};

        if (!this->m_vk.StreamLock(buf->m_itemSize, buf->m_size, index, slice)) {
            return static_cast<char*>(this->m_emergencyMem[pool->m_target].Lock(buf->m_size));
        }

        VkBufferAlloc* alloc = static_cast<VkBufferAlloc*>(pool->m_apiSpecific);

        if (slice.borrowed) {
            if (alloc && !alloc->borrowed) {
                this->m_vk.RetireBuffer(*alloc);
                delete alloc;
                alloc = nullptr;
            }

            if (!alloc) {
                alloc = new VkBufferAlloc();
                pool->m_apiSpecific = alloc;
            }

            *alloc = slice;
            alloc->memory = VK_NULL_HANDLE;
            alloc->borrowed = 1;
        } else {
            if (alloc) {
                if (!alloc->borrowed) {
                    this->m_vk.RetireBuffer(*alloc);
                }
                delete alloc;
            }

            alloc = new VkBufferAlloc(slice);
            pool->m_apiSpecific = alloc;
        }

        buf->m_index = index;
        pool->m_offset = static_cast<int32_t>(index + buf->m_size);
        return reinterpret_cast<char*>(slice.mapped) + index;
    }

    VkBufferAlloc* alloc = this->IPoolBuffer(pool);

    if (!alloc) {
        return static_cast<char*>(this->m_emergencyMem[pool->m_target].Lock(buf->m_size));
    }

    return reinterpret_cast<char*>(alloc->mapped) + buf->m_index;
}

void CGxDeviceVulkan::IBufUnlock(CGxBuf* buf) {
    EmergencyMem& emergencyMem = this->m_emergencyMem[buf->m_pool->m_target];

    if (emergencyMem.m_lock) {
        const uint8_t* src = emergencyMem.m_data.Ptr();
        VkBufferAlloc* alloc = this->IPoolBuffer(buf->m_pool);

        if (alloc && src && buf->m_size) {
            memcpy(reinterpret_cast<char*>(alloc->mapped) + buf->m_index, src, buf->m_size);
            buf->m_valid = 1;
        } else {
            buf->m_valid = 0;
            this->m_intF5C = 1;
        }

        emergencyMem.Unlock();
    } else {
        buf->m_valid = 1;
    }

    VkBufferAlloc* resident = buf->m_pool ? static_cast<VkBufferAlloc*>(buf->m_pool->m_apiSpecific) : nullptr;

    if (buf->m_valid && resident && resident->shadow && resident->mapped && buf->m_size) {
        this->m_vk.QueueResidentUpload(resident->buffer, resident->mapped + buf->m_index, buf->m_index, buf->m_size);
    }
}

char* CGxDeviceVulkan::BufLock(CGxBuf* buf) {
    CGxDevice::BufLock(buf);
    return this->IBufLock(buf);
}

int32_t CGxDeviceVulkan::BufUnlock(CGxBuf* buf, uint32_t size) {
    CGxDevice::BufUnlock(buf, size);
    this->IBufUnlock(buf);
    return 1;
}

void CGxDeviceVulkan::BufData(CGxBuf* buf, const void* data, uint32_t size, uint32_t offset) {
    CGxDevice::BufData(buf, data, size, offset);

    // ʕ •ᴥ•ʔ✿ A null source with a positive size, or a null lock, must not reach memcpy ✿ ʕ •ᴥ•ʔ
    if (!data && size > 0) {
        return;
    }

    char* dst = this->IBufLock(buf);

    if (!dst) {
        this->IBufUnlock(buf);
        return;
    }

    memcpy(dst + offset, data, size);
    this->IBufUnlock(buf);
}

VkTexture* CGxDeviceVulkan::ITexReady(CGxTex* texId) {
    if (!texId) {
        return nullptr;
    }

    if (texId->m_needsCreation || !texId->m_apiSpecificData) {
        this->ITexCreate(texId);
    }

    return static_cast<VkTexture*>(texId->m_apiSpecificData);
}

void CGxDeviceVulkan::ITexCreate(CGxTex* texId) {
    if (!this->m_context || texId->m_target == GxTex_CubeMap) {
        return;
    }

    // ʕ •ᴥ•ʔ✿ Stored 0 is an invalid extent, and a 0×0 non-DXT mip chain never returns ✿ ʕ •ᴥ•ʔ
    if (!texId->m_width || !texId->m_height) {
        return;
    }

    uint32_t width;
    uint32_t height;
    uint32_t baseMip;
    uint32_t mipCount;
    this->ITexWHDStartEnd(texId, width, height, baseMip, mipCount);

    // ʕ ◕ᴥ◕ ʔ✿ Base mip can shift a 1px side to 0; vkCreateImage rejects that extent ✿ ʕ ◕ᴥ◕ ʔ
    if (!width || !height) {
        return;
    }

    texId->m_format = s_gxTexFormatToUse[texId->m_format];

    // ʕ •ᴥ•ʔ✿ BC images reject extents that are not a multiple of the 4×4 block ✿ ʕ •ᴥ•ʔ
    if (texId->m_format >= GxTex_Dxt1 && texId->m_format <= GxTex_Dxt5 && ((width | height) & 3)) {
        texId->m_format = GxTex_Argb8888;
    }

    VkFormat format = s_gxTexFmtToVkFmt[texId->m_format];
    int32_t renderTarget = texId->m_flags.m_renderTarget;

    if (format == VK_FORMAT_UNDEFINED || !this->m_caps.m_texFmt[texId->m_format] || (renderTarget && format != VkContext::s_offscreenFormat)) {
        return;
    }

    VkImageCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = { width, height, 1 };
    info.mipLevels = renderTarget ? 1 : mipCount - baseMip;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    if (renderTarget) {
        info.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }

    VkTexture* texture = new VkTexture{};

    if (!this->m_vk.CreateImage(info, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D, TexSwizzle(texId->m_format), texture->image)) {
        delete texture;
        return;
    }

    texture->width = width;
    texture->height = height;
    texture->levels = info.mipLevels;
    texture->renderTarget = renderTarget;

    VkCommandBuffer upload = this->m_vk.UploadCmd();

    // ʕ •ᴥ•ʔ✿ Draw binds the view with no layout check, so an UNDEFINED image would be sampled ✿ ʕ •ᴥ•ʔ
    if (!upload) {
        this->m_vk.RetireImage(texture->image);
        delete texture;
        texId->m_needsCreation = 1;
        return;
    }

    if (renderTarget) {
        texture->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkContext::Barrier(upload, texture->image.image, VK_IMAGE_ASPECT_COLOR_BIT, texture->levels, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    } else {
        texture->layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        VkContext::Barrier(upload, texture->image.image, VK_IMAGE_ASPECT_COLOR_BIT, texture->levels, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        // ʕ •ᴥ•ʔ✿ vkCmdClearColorImage rejects block-compressed images ✿ ʕ •ᴥ•ʔ
        if (texId->m_format < GxTex_Dxt1 || texId->m_format > GxTex_Dxt5) {
            VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, texture->levels, 0, 1 };
            VkClearColorValue clear = {};
            vkCmdClearColorImage(upload, texture->image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);
        }

        VkContext::Barrier(upload, texture->image.image, VK_IMAGE_ASPECT_COLOR_BIT, texture->levels, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        texture->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        texture->initialized = 1;
    }

    texId->m_apiSpecificData = texture;
    texId->m_needsCreation = 0;
}

int32_t CGxDeviceVulkan::ITexUpload(CGxTex* texId) {
    uint32_t texelStrideInBytes;
    const void* texels = nullptr;
    texId->m_userFunc(GxTex_Lock, texId->m_width, texId->m_height, 0, 0, texId->m_userArg, texelStrideInBytes, texels);

    uint32_t width;
    uint32_t height;
    uint32_t baseMip;
    uint32_t mipCount;
    this->ITexWHDStartEnd(texId, width, height, baseMip, mipCount);

    VkTexture* texture = static_cast<VkTexture*>(texId->m_apiSpecificData);

    // ʕ •ᴥ•ʔ✿ A latch renders on the frame buffer. Transfer barriers on the upload buffer are a second command buffer in the same submit, so they overlap the draw and the sample comes back clear ✿ ʕ •ᴥ•ʔ
    if (texId->m_flags.m_renderTarget) {
        uint32_t mipWidth = width;
        uint32_t mipHeight = height;

        for (uint32_t mip = baseMip, level = 0; mip != mipCount && level < texture->levels; mip++, level++) {
            texels = nullptr;
            texId->m_userFunc(GxTex_Latch, mipWidth, mipHeight, 0, mip, texId->m_userArg, texelStrideInBytes, texels);
            mipWidth = std::max(mipWidth >> 1, 1u);
            mipHeight = std::max(mipHeight >> 1, 1u);
        }

        texels = nullptr;
        texId->m_userFunc(GxTex_Unlock, texId->m_width, texId->m_height, 0, 0, texId->m_userArg, texelStrideInBytes, texels);
        return 1;
    }

    VkCommandBuffer cmd = this->m_vk.UploadCmd();
    if (!cmd) {
        texels = nullptr;
        texId->m_userFunc(GxTex_Unlock, texId->m_width, texId->m_height, 0, 0, texId->m_userArg, texelStrideInBytes, texels);
        return 0;
    }
    int32_t compressed = texId->m_dataFormat >= GxTex_Dxt1 && texId->m_dataFormat <= GxTex_Dxt5;
    int32_t destCompressed = texId->m_format >= GxTex_Dxt1 && texId->m_format <= GxTex_Dxt5;
    int32_t copied = 0;

    VkContext::Barrier(cmd, texture->image.image, VK_IMAGE_ASPECT_COLOR_BIT, texture->levels, texture->layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    texture->layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    uint32_t mipWidth = width;
    uint32_t mipHeight = height;

    for (uint32_t mip = baseMip, level = 0; mip != mipCount && level < texture->levels; mip++, level++) {
        texels = nullptr;
        texId->m_userFunc(GxTex_Latch, mipWidth, mipHeight, 0, mip, texId->m_userArg, texelStrideInBytes, texels);

        if (texels && !texId->m_flags.m_renderTarget) {
            int32_t left = texId->m_updateRect.minX >> mip;
            int32_t top = texId->m_updateRect.minY >> mip;
            int32_t right = std::max(left + 1, texId->m_updateRect.maxX >> mip);
            int32_t bottom = std::max(top + 1, texId->m_updateRect.maxY >> mip);

            if (destCompressed) {
                left &= ~3;
                top &= ~3;
                right = (right + 3) & ~3;
                bottom = (bottom + 3) & ~3;
            }

            right = std::min(right, static_cast<int32_t>(mipWidth));
            bottom = std::min(bottom, static_cast<int32_t>(mipHeight));

            const uint8_t* src = static_cast<const uint8_t*>(texels);

            if (!texId->m_flags.m_bit15) {
                if (compressed) {
                    src += CGxDevice::s_texFormatBytesPerBlock[texId->m_dataFormat] * (left >> 2) + texelStrideInBytes * (top >> 2);
                } else {
                    src += ((left * CGxDevice::s_texFormatBitDepth[texId->m_dataFormat]) >> 3) + texelStrideInBytes * top;
                }
            }

            C2iVector size;
            size.x = right - left;
            size.y = bottom - top;

            uint32_t destStride;
            uint32_t destRows;

            if (destCompressed) {
                destStride = ((size.x + 3) >> 2) * CGxDevice::s_texFormatBytesPerBlock[texId->m_format];
                destRows = (size.y + 3) >> 2;
            } else {
                destStride = (size.x * CGxDevice::s_texFormatBitDepth[texId->m_format]) >> 3;
                destRows = size.y;
            }

            VkStaging staging = this->m_vk.Stage(destStride * destRows);

            if (staging.data && size.x > 0 && size.y > 0 && Blit(size, BlitAlpha_0, src, texelStrideInBytes, GxGetBlitFormat(texId->m_dataFormat), staging.data, destStride, GxGetBlitFormat(texId->m_format))) {
                VkBufferImageCopy region = {};
                region.bufferOffset = staging.offset;
                region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1 };
                region.imageOffset = { left, top, 0 };
                region.imageExtent = { static_cast<uint32_t>(size.x), static_cast<uint32_t>(size.y), 1 };
                vkCmdCopyBufferToImage(cmd, staging.buffer, texture->image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
                copied = 1;
            }
        }

        mipWidth = std::max(mipWidth >> 1, 1u);
        mipHeight = std::max(mipHeight >> 1, 1u);
    }

    if (copied) {
        VkContext::Barrier(cmd, texture->image.image, VK_IMAGE_ASPECT_COLOR_BIT, texture->levels, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        texture->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        texture->initialized = 1;
    } else if (texture->layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        VkContext::Barrier(cmd, texture->image.image, VK_IMAGE_ASPECT_COLOR_BIT, texture->levels, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        texture->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    texels = nullptr;
    texId->m_userFunc(GxTex_Unlock, texId->m_width, texId->m_height, 0, 0, texId->m_userArg, texelStrideInBytes, texels);

    return copied;
}

void CGxDeviceVulkan::ITexMarkAsUpdated(CGxTex* texId) {
    if (!texId->m_needsUpdate || !this->m_context) {
        return;
    }

    if (texId->m_needsCreation || !texId->m_apiSpecificData) {
        this->ITexCreate(texId);
    }

    if (!texId->m_needsCreation && texId->m_apiSpecificData) {
        if (texId->m_userFunc) {
            if (!this->ITexUpload(texId)) {
                return;
            }
        }

        CGxDevice::ITexMarkAsUpdated(texId);
    }
}

void CGxDeviceVulkan::ITexRelease(CGxTex* texId) {
    VkTexture* texture = static_cast<VkTexture*>(texId->m_apiSpecificData);

    if (!texture) {
        return;
    }

    if (this->m_passTarget == texture) {
        this->IEndPass();
        this->m_passTarget = nullptr;
    }

    for (CGxDevice::TextureTarget& target : this->m_textureTarget) {
        if (target.m_apiSpecific == texture) {
            target.m_apiSpecific = nullptr;
        }
    }

    if (texture->framebuffer) {
        VkGarbage garbage = {};
        garbage.framebuffer = texture->framebuffer;
        this->m_vk.Retire(garbage);
    }

    this->m_vk.RetireImage(texture->image);
    delete texture;
    texId->m_apiSpecificData = nullptr;
}

void CGxDeviceVulkan::ITexForceRecreation(int32_t all) {
    for (CGxTex* texId = this->m_textures.Head(); texId; texId = this->m_textures.Next(texId)) {
        if (!texId->m_apiSpecificData || !(all || texId->m_flags.m_renderTarget)) {
            continue;
        }

        this->ITexRelease(texId);
        texId->m_needsCreation = 1;

        CiRect updateRect = {};
        this->TexMarkForUpdate(texId, updateRect, 0);

        uint32_t texelStrideInBytes;
        const void* texels;
        texId->m_userFunc(GxTex_3, texId->m_width, texId->m_height, 0, 0, texId->m_userArg, texelStrideInBytes, texels);
    }

    if (this->m_window && this->m_vk.m_device) {
        this->NotifyOnTextureRecreation();
    }
}

VkFramebuffer CGxDeviceVulkan::ITexFramebuffer(VkTexture* texture) {
    if (this->m_offscreenDepthExtent.width < texture->width || this->m_offscreenDepthExtent.height < texture->height) {
        this->m_vk.RetireImage(this->m_offscreenDepth);

        VkExtent2D extent = { std::max(this->m_offscreenDepthExtent.width, texture->width), std::max(this->m_offscreenDepthExtent.height, texture->height) };

        VkImageCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = this->m_vk.m_depthFormat;
        info.extent = { extent.width, extent.height, 1 };
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

        if (!this->m_vk.CreateImage(info, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_VIEW_TYPE_2D, {}, this->m_offscreenDepth)) {
            this->m_offscreenDepthExtent = {};
            return VK_NULL_HANDLE;
        }

        VkFrame& frame = this->m_vk.Frame();
        VkCommandBuffer layoutCmd = frame.recording ? frame.cmd : this->m_vk.UploadCmd();
        if (!layoutCmd) {
            this->m_vk.RetireImage(this->m_offscreenDepth);
            this->m_offscreenDepthExtent = {};
            return VK_NULL_HANDLE;
        }

        VkContext::Barrier(layoutCmd, this->m_offscreenDepth.image, VK_IMAGE_ASPECT_DEPTH_BIT, 1, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

        this->m_offscreenDepthExtent = extent;
        this->m_offscreenDepthGeneration++;
    }

    if (texture->framebuffer && texture->depthGeneration == this->m_offscreenDepthGeneration) {
        return texture->framebuffer;
    }

    if (texture->framebuffer) {
        VkGarbage garbage = {};
        garbage.framebuffer = texture->framebuffer;
        this->m_vk.Retire(garbage);
        texture->framebuffer = VK_NULL_HANDLE;
    }

    VkImageView attachments[2] = { texture->image.view, this->m_offscreenDepth.view };

    VkFramebufferCreateInfo info = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    info.renderPass = this->m_vk.m_passes[VkContext::Pass_Offscreen];
    info.attachmentCount = 2;
    info.pAttachments = attachments;
    info.width = texture->width;
    info.height = texture->height;
    info.layers = 1;

    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    if (vkCreateFramebuffer(this->m_vk.m_device, &info, nullptr, &framebuffer) != VK_SUCCESS) {
        if (texture->framebuffer) {
            VkGarbage garbage = {};
            garbage.framebuffer = texture->framebuffer;
            this->m_vk.Retire(garbage);
            texture->framebuffer = VK_NULL_HANDLE;
        }
        return VK_NULL_HANDLE;
    }

    texture->framebuffer = framebuffer;
    texture->depthGeneration = this->m_offscreenDepthGeneration;

    return texture->framebuffer;
}

void CGxDeviceVulkan::TexDestroy(CGxTex* texId) {
    this->ITexRelease(texId);
    CGxDevice::TexDestroy(texId);
}

void CGxDeviceVulkan::DeviceSetBaseMipLevel(uint32_t level) {
    this->ITexForceRecreation(1);
    CGxDevice::DeviceSetBaseMipLevel(level);
}

void CGxDeviceVulkan::DeviceSetRenderTarget(EGxBuffer buffer, CGxTex* texture, uint32_t plane) {
    if (!this->m_context) {
        return;
    }

    CGxDevice::TextureTarget& target = this->m_textureTarget[buffer];

    if (target.m_texture == texture && target.m_plane == plane) {
        return;
    }

    CGxDevice::DeviceSetRenderTarget(buffer, texture, plane);

    target.m_apiSpecific = buffer == GxBuffers_Color ? this->ITexReady(texture) : nullptr;

    VkTexture* color = static_cast<VkTexture*>(this->m_textureTarget[GxBuffers_Color].m_apiSpecific);
    this->m_targetInvalid = this->m_textureTarget[GxBuffers_Color].m_texture && (!color || !color->renderTarget);

    VkTexture* passTarget = this->m_targetInvalid ? nullptr : color;

    if (passTarget != this->m_passTarget) {
        this->IEndPass();
        this->m_passTarget = passTarget;
    }

    GxXformSetViewport(this->m_viewport.m_x.m_l, this->m_viewport.m_x.m_h, this->m_viewport.m_y.m_l, this->m_viewport.m_y.m_h, this->m_viewport.m_z.m_l, this->m_viewport.m_z.m_h);

    this->m_viewportDirty = 1;
    this->m_scissorRectDirty = 1;
}

int32_t CGxDeviceVulkan::IBlitFromBackBuffer(CGxTex* dest, const VkOffset3D* destBounds, const VkOffset3D* sourceBounds, uint32_t level, VkFilter filter) {
    if (!this->m_context || !this->m_frameActive) {
        return 0;
    }

    VkTexture* texture = this->ITexReady(dest);

    if (!texture || level >= texture->levels) {
        return 0;
    }

    VkFormatProperties properties;
    vkGetPhysicalDeviceFormatProperties(this->m_vk.m_physicalDevice, texture->image.format, &properties);

    if (!(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT)) {
        return 0;
    }

    vkGetPhysicalDeviceFormatProperties(this->m_vk.m_physicalDevice, this->m_vk.m_colorFormat, &properties);

    if (!(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT)) {
        return 0;
    }

    VkImageBlit region = {};
    region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1 };

    int32_t sourceMax[2] = { static_cast<int32_t>(this->m_vk.m_extent.width), static_cast<int32_t>(this->m_vk.m_extent.height) };
    int32_t destMax[2] = { static_cast<int32_t>(std::max(texture->width >> level, 1u)), static_cast<int32_t>(std::max(texture->height >> level, 1u)) };

    for (uint32_t i = 0; i < 2; i++) {
        region.srcOffsets[i] = { std::min(std::max(sourceBounds[i].x, 0), sourceMax[0]), std::min(std::max(sourceBounds[i].y, 0), sourceMax[1]), static_cast<int32_t>(i) };
        region.dstOffsets[i] = { std::min(std::max(destBounds[i].x, 0), destMax[0]), std::min(std::max(destBounds[i].y, 0), destMax[1]), static_cast<int32_t>(i) };
    }

    if (region.srcOffsets[0].x == region.srcOffsets[1].x || region.srcOffsets[0].y == region.srcOffsets[1].y || region.dstOffsets[0].x == region.dstOffsets[1].x || region.dstOffsets[0].y == region.dstOffsets[1].y) {
        return 0;
    }

    this->IEnsureFrameImage();
    this->IEndPass();

    VkCommandBuffer cmd = this->m_vk.Frame().cmd;
    VkImage backBuffer = this->m_vk.m_swapImages[this->m_vk.m_imageIndex];

    VkContext::Barrier(cmd, backBuffer, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkContext::Barrier(cmd, texture->image.image, VK_IMAGE_ASPECT_COLOR_BIT, texture->levels, texture->layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vkCmdBlitImage(cmd, backBuffer, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, texture->image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, filter);
    VkContext::Barrier(cmd, texture->image.image, VK_IMAGE_ASPECT_COLOR_BIT, texture->levels, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    texture->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkContext::Barrier(cmd, backBuffer, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    return 1;
}

int32_t CGxDeviceVulkan::TexCopy(CGxTex* dest, const C2iVector& destPos, const C2iVector& sourcePos, const C2iVector& size, uint32_t level, uint32_t plane) {
    VkOffset3D destBounds[2] = { { destPos.x, destPos.y, 0 }, { destPos.x + size.x, destPos.y + size.y, 1 } };
    VkOffset3D sourceBounds[2] = { { sourcePos.x, sourcePos.y, 0 }, { sourcePos.x + size.x, sourcePos.y + size.y, 1 } };

    return this->IBlitFromBackBuffer(dest, destBounds, sourceBounds, level, VK_FILTER_NEAREST);
}

int32_t CGxDeviceVulkan::TexStretch(CGxTex* dest, const CiRect* destRect, const CiRect* sourceRect, uint32_t level, uint32_t face) {
    VkOffset3D destBounds[2] = { { 0, 0, 0 }, { static_cast<int32_t>(std::max(dest->m_width >> level, 1u)), static_cast<int32_t>(std::max(dest->m_height >> level, 1u)), 1 } };
    VkOffset3D sourceBounds[2] = { { 0, 0, 0 }, { static_cast<int32_t>(this->m_vk.m_extent.width), static_cast<int32_t>(this->m_vk.m_extent.height), 1 } };

    if (destRect) {
        destBounds[0] = { destRect->minX, destRect->minY, 0 };
        destBounds[1] = { destRect->maxX, destRect->maxY, 1 };
    }

    if (sourceRect) {
        sourceBounds[0] = { sourceRect->minX, sourceRect->minY, 0 };
        sourceBounds[1] = { sourceRect->maxX, sourceRect->maxY, 1 };
    }

    VkFilter filter = VK_FILTER_LINEAR;

    if (destRect && sourceRect && destRect->maxX - destRect->minX == sourceRect->maxX - sourceRect->minX && destRect->maxY - destRect->minY == sourceRect->maxY - sourceRect->minY) {
        filter = VK_FILTER_NEAREST;
    }

    return this->IBlitFromBackBuffer(dest, destBounds, sourceBounds, level, filter);
}

void CGxDeviceVulkan::DeviceReadPixels(CiRect& rect, TSGrowableArray<CImVector>& pixels) {
    this->ClampRectToWindow(rect);

    if (rect.minX < 0) {
        rect.minX = 0;
    }

    if (rect.minY < 0) {
        rect.minY = 0;
    }

    rect.maxX = std::min(rect.maxX, static_cast<int32_t>(this->m_vk.m_extent.width));
    rect.maxY = std::min(rect.maxY, static_cast<int32_t>(this->m_vk.m_extent.height));

    uint32_t width = rect.maxX > rect.minX ? static_cast<uint32_t>(rect.maxX - rect.minX) : 0;
    uint32_t height = rect.maxY > rect.minY ? static_cast<uint32_t>(rect.maxY - rect.minY) : 0;

    if (height != 0 && width > UINT32_MAX / height) {
        pixels.SetCount(0);
        return;
    }

    uint32_t count = width * height;
    pixels.SetCount(count);

    if (!count) {
        return;
    }

    memset(pixels.Ptr(), 0, sizeof(CImVector) * count);

    VkBufferAlloc readback;

    if (!this->m_frameActive || !this->m_vk.CreateBuffer(sizeof(CImVector) * count, VK_BUFFER_USAGE_TRANSFER_DST_BIT, readback)) {
        return;
    }

    this->IEnsureFrameImage();
    this->IEndPass();

    VkCommandBuffer cmd = this->m_vk.Frame().cmd;
    VkImage backBuffer = this->m_vk.m_swapImages[this->m_vk.m_imageIndex];

    VkBufferImageCopy region = {};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageOffset = { rect.minX, rect.minY, 0 };
    region.imageExtent = { width, height, 1 };

    VkContext::Barrier(cmd, backBuffer, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    vkCmdCopyImageToBuffer(cmd, backBuffer, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &region);
    VkContext::Barrier(cmd, backBuffer, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    this->m_vk.FlushFrame();
    this->IInvalidateCmdState();

    switch (this->m_vk.m_colorFormat) {
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
            memcpy(pixels.Ptr(), readback.mapped, sizeof(CImVector) * count);
            break;

        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB: {
            const uint8_t* source = readback.mapped;
            CImVector* dest = pixels.Ptr();

            for (uint32_t i = 0; i < count; i++, source += 4) {
                dest[i] = CImVector(source[2], source[1], source[0], source[3]);
            }

            break;
        }

        default:
            memset(pixels.Ptr(), 0xFF, sizeof(CImVector) * count);
            break;
    }

    this->m_vk.DestroyBuffer(readback);
}

void CGxDeviceVulkan::DeviceReadDepths(CiRect& rect, TSGrowableArray<float>& depths) {
    this->ClampRectToWindow(rect);

    uint32_t width = rect.maxX > rect.minX ? static_cast<uint32_t>(rect.maxX - rect.minX) : 0;
    uint32_t height = rect.maxY > rect.minY ? static_cast<uint32_t>(rect.maxY - rect.minY) : 0;

    if (height != 0 && width > UINT32_MAX / height) {
        depths.SetCount(0);
        return;
    }

    uint32_t count = width * height;
    depths.SetCount(count);
    memset(depths.Ptr(), 0, sizeof(float) * count);
}

int32_t CGxDeviceVulkan::DeviceWM(EGxWM wm, uintptr_t param1, uintptr_t param2) {
    switch (wm) {
        case GxWM_Size:
            if (param2 == 1 || param2 == 2) {
                this->m_windowVisible = 0;
                break;
            }

            this->m_windowVisible = 1;
            {
                // ʕ •ᴥ•ʔ✿ A size message can repeat for the same drawable. Rebuilding the swapchain for that is the frame cap ✿ ʕ •ᴥ•ʔ
                const CRect& next = *reinterpret_cast<const CRect*>(param1);
                uint32_t width = next.maxX > next.minX ? static_cast<uint32_t>(next.maxX - next.minX) : 0;
                uint32_t height = next.maxY > next.minY ? static_cast<uint32_t>(next.maxY - next.minY) : 0;
                int32_t changed = width != this->m_clientExtent.width || height != this->m_clientExtent.height;
                this->DeviceSetDefWindow(next);

                if (changed) {
                    this->m_swapchainDirty = 1;
                    this->m_viewportDirty = 1;
                    this->m_scissorRectDirty = 1;
                }
            }
            break;

        case GxWM_DisplayChange:
            if (this->m_windowVisible) {
                this->DeviceSetDefWindow(*reinterpret_cast<const CRect*>(param1));
                this->m_swapchainDirty = 1;
                this->m_viewportDirty = 1;
                this->m_scissorRectDirty = 1;
            }

            break;

        case GxWM_Destroy:
        case GxWM_KillFocus:
            this->m_windowFocus = 0;
            break;

        case GxWM_SetFocus:
            this->m_windowFocus = 1;
            break;

        default:
            break;
    }

    return 0;
}

void* CGxDeviceVulkan::DeviceWindow() {
    return this->m_window;
}

void CGxDeviceVulkan::DeviceOverride(EGxOverride override, uint32_t value) {
    CGxDevice::DeviceOverride(override, value);

    if (override == GxOverride_Unk7) {
        this->m_caps.m_texNonPow2 = value != 0;
    }
}

void CGxDeviceVulkan::CapsWindowSize(CRect& rect) {
    rect = this->DeviceCurWindow();
}

void CGxDeviceVulkan::LogCrashInfo(char* buffer, uint32_t size) {
    const VkPhysicalDeviceProperties& properties = this->m_vk.m_properties;

    char line[260];
    SStrPrintf(line, sizeof(line), "Vulkan Device: %s\r\n", properties.deviceName);
    SStrPack(buffer, line, size);
    SStrPrintf(line, sizeof(line), "  API: %d.%d.%d\r\n", VK_API_VERSION_MAJOR(properties.apiVersion), VK_API_VERSION_MINOR(properties.apiVersion), VK_API_VERSION_PATCH(properties.apiVersion));
    SStrPack(buffer, line, size);
    SStrPrintf(line, sizeof(line), "  Driver: 0x%08x VID: %04x DID: %04x\r\n", properties.driverVersion, properties.vendorID, properties.deviceID);
    SStrPack(buffer, line, size);
}

bool CGxDeviceVulkan::QueryBegin(CGxQuery* query) {
    return false;
}

bool CGxDeviceVulkan::QueryEnd(CGxQuery* query) {
    return false;
}

bool CGxDeviceVulkan::QueryGetParam(CGxQuery* query, EGxQueryParam param, uint32_t& data) {
    data = 0;
    return false;
}

bool CGxDeviceVulkan::QueryGetData(CGxQuery* query, uint32_t& data) {
    data = 0;
    return false;
}

void CGxDeviceVulkan::IShaderCreate(CGxShader* shader) {
    shader->m_valid = 0;
    shader->m_loaded = this->m_context != 0;

    // ʕ •ᴥ•ʔ✿ .bls never loads here. SetDesaturated has to succeed or FrameXML just darkens the icon ✿ ʕ •ᴥ•ʔ
    if (shader->m_target == GxSh_Pixel && ShaderDesaturates(shader)) {
        shader->m_valid = 1;
    }
}

void CGxDeviceVulkan::IShaderReload(CGxShader* shader, const char* path, const char* name) {
    if (shader->m_target != GxSh_Vertex && shader->m_target != GxSh_Pixel) {
        return;
    }

    if (shader->m_refCount == 1) {
        shader->m_loaded = 0;
    }

    this->IShaderLoad(&shader, static_cast<EGxShTarget>(shader->m_target), path, name, 1);
    this->IShaderCreate(shader);
}

void CGxDeviceVulkan::StereoSetConvergence(float convergence) {
    this->m_stereoConvergence = convergence;
}

float CGxDeviceVulkan::StereoGetConvergence() {
    return this->m_stereoConvergence;
}

void CGxDeviceVulkan::StereoSetSeparation(float separation) {
    this->m_stereoSeparation = separation;
}

float CGxDeviceVulkan::StereoGetSeparation() {
    return this->m_stereoSeparation;
}

bool CGxDeviceVulkan::StereoEnabled() {
    return false;
}

void CGxDeviceVulkan::DeviceUnk81(uint32_t a, uint32_t b) {
}

void CGxDeviceVulkan::DeviceUnk82() {
}

void CGxDeviceVulkan::DeviceUnk83(uint32_t a, uint32_t b) {
}

int32_t CGxDevice::VulkanAdapterFormats(TSGrowableArray<CGxFormat>& formats) {
    static const CGxFormat::Format colorFormats[] = { CGxFormat::Fmt_ArgbX888, CGxFormat::Fmt_Argb8888 };
    static const CGxFormat::Format depthFormats[] = { CGxFormat::Fmt_Ds24X, CGxFormat::Fmt_Ds248, CGxFormat::Fmt_Ds320 };

    for (CGxFormat::Format color : colorFormats) {
        for (CGxFormat::Format depth : depthFormats) {
            CGxFormat format;
            format.m_colorFormat = color;
            format.m_depthFormat = depth;
            format.m_multisampleCount = 1;

            if (!CGxDevice::FormatExists(formats, format)) {
                *formats.New() = format;
            }
        }
    }

    return formats.Count() != 0;
}

CGxDevice* CreateCGxDeviceVulkan() {
    void* mem = SMemAlloc(sizeof(CGxDeviceVulkan), __FILE__, __LINE__, 0);

    if (!mem) {
        return nullptr;
    }

    return new (mem) CGxDeviceVulkan();
}
