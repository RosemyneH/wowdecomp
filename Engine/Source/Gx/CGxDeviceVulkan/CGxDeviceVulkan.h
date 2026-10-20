#ifndef ENGINE_SOURCE_GX_CGXDEVICEVULKAN_CGXDEVICEVULKAN_H
#define ENGINE_SOURCE_GX_CGXDEVICEVULKAN_CGXDEVICEVULKAN_H

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxDeviceVulkan/VkContext.h"

struct VkTexture {
    VkImageAlloc image;
    uint32_t width;
    uint32_t height;
    uint32_t levels;
    int32_t renderTarget;
    VkFramebuffer framebuffer;
    uint32_t depthGeneration;
    VkImageLayout layout;
    uint8_t initialized;
    uint32_t terrainSlot = ~0u;
    uint32_t terrainEpoch = 0;
    uint32_t terrainFrames = 0;
    VkImageView terrainView = VK_NULL_HANDLE;
    VkSampler terrainSampler = VK_NULL_HANDLE;
};

struct VkDrawUniforms {
    C44Matrix world;
    C44Matrix view;
    C44Matrix proj;
    C44Matrix texMatrix[2];
    float matDiffuse[4];
    float matEmissive[4];
    float ambient[4];
    float lightPos[4][4];
    float lightDiffuse[4][4];
    float lightAmbient[4][4];
    float lightAtten[4][4];
    float fogColor[4];
    float fogParams[4];
    int32_t stageColorOp[4];
    int32_t stageAlphaOp[4];
    int32_t stageTexGen[4];
    int32_t stageTexMode[4];
    int32_t stageCoord[4];
    int32_t flags[4];
    float misc[4];
};

// ʕ •ᴥ•ʔ✿ Must match the std140 layout of Shaders/FfpUniforms.glsl (checked by Tools/spirv-pin-compare.py) ✿ ʕ •ᴥ•ʔ
static_assert(sizeof(VkDrawUniforms) == 768, "VkDrawUniforms must stay 768 bytes");

struct VkPipelineKey {
    uint8_t topology;
    uint8_t blend;
    uint8_t depthFunc;
    uint8_t depthWrite;
    uint8_t colorMask;
    uint8_t cull;
    uint8_t fill;
    uint8_t pass;
    uint8_t bindingCount;
    uint8_t attribBinding[7];
    uint8_t attribType[7];
    uint8_t unused;
    uint8_t bindless;
    uint16_t attribOffset[7];
    uint16_t bindingStride[6];

    bool operator==(const VkPipelineKey& key) const;
};

struct VkPipelineKeyHash {
    size_t operator()(const VkPipelineKey& key) const;
};

class CGxDeviceVulkan : public CGxDevice {
    public:
    enum {
        MaxBindings = 6,
        TerrainSlots = 65536,
        DepthFunc_Always = 4,
        BonePaletteCount = 256,
    };

    static const VkFormat s_gxTexFmtToVkFmt[GxTexFormats_Last];
    static const VkPrimitiveTopology s_primitiveConversion[GxPrims_Last];
    static const VkBlendFactor s_srcBlend[GxBlends_Last];
    static const VkBlendFactor s_dstBlend[GxBlends_Last];
    static const VkCompareOp s_cmpFunc[5];
    static const VkFormat s_gxAttribToVkFormat[7];
    static const uint32_t s_gxAttribSize[7];
    static const int32_t s_gxAttribLocation[GxVAs_Last];
    static EGxTexFormat s_gxTexFormatToUse[GxTexFormats_Last];

    void* m_window = nullptr;
    int32_t m_ownWindow = 0;
    uintptr_t m_windowClass = 0;
    VkContext m_vk;
    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipelineCache m_pipelineCache = VK_NULL_HANDLE;
    VkShaderModule m_vertexModule = VK_NULL_HANDLE;
    VkShaderModule m_fragmentModule = VK_NULL_HANDLE;
    VkShaderModule m_terrainFragment = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_terrainSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_terrainPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_terrainPools[2] = {};
    VkDescriptorSet m_terrainSets[2] = {};
    uint32_t m_terrainEpoch = 0;
    uint32_t m_terrainCount = 0;
    int32_t m_bindless = 0;
    std::unordered_map<VkPipelineKey, VkPipeline, VkPipelineKeyHash> m_pipelines;
    VkPipelineKey m_lastPipelineKey = {};
    VkPipeline m_lastPipeline = VK_NULL_HANDLE;
    std::unordered_map<uint32_t, VkSampler> m_samplers;
    VkBufferAlloc m_dummyVertices = {};
    VkImageAlloc m_offscreenDepth = {};
    VkExtent2D m_offscreenDepthExtent = {};
    uint32_t m_offscreenDepthGeneration = 0;
    CGxTex* m_defaultTexture = nullptr;
    VkRenderPass m_postPass = VK_NULL_HANDLE;
    VkPipeline m_postPipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_postLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_postSetLayout = VK_NULL_HANDLE;
    VkShaderModule m_postVertex = VK_NULL_HANDLE;
    VkShaderModule m_postFragment = VK_NULL_HANDLE;
    VkSampler m_postSampler = VK_NULL_HANDLE;
    VkPipeline m_atmosPipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_atmosLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_atmosSetLayout = VK_NULL_HANDLE;
    VkShaderModule m_atmosFragment = VK_NULL_HANDLE;
    VkSampler m_atmosDepthSampler = VK_NULL_HANDLE;
    int32_t m_atmosApplied = 0;
    int32_t m_atmosWorldDraw = 0;
    int32_t m_atmosSamples = 0;
    float m_atmosEyeError = 1.0e30f;
    C44Matrix m_atmosView;
    C44Matrix m_atmosProj;
    float m_atmosNear = 0.2f;
    float m_atmosFar = 1000.0f;
    VkImageAlloc m_postColor = {};
    VkImageLayout m_postColorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkExtent2D m_postExtent = {};
    VkFormat m_postFormat = VK_FORMAT_UNDEFINED;
    VkSwapchainKHR m_postSwapchain = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_postFramebuffers;
    int32_t m_swapchainDirty = 0;
    VkExtent2D m_clientExtent = {};
    int32_t m_imageStarved = 0;
    int32_t m_frameActive = 0;
    int32_t m_frameCleared = 0;
    int32_t m_passActive = 0;
    VkTexture* m_passTarget = nullptr;
    int32_t m_targetInvalid = 0;
    VkExtent2D m_targetExtent = {};
    VkRect2D m_viewportRect = {};
    VkViewport m_viewportVk = {};
    VkRect2D m_scissorVk = {};
    VkDrawUniforms m_uniforms = {};
    VkDeviceSize m_uniformOffset = 0;
    int32_t m_uniformsValid = 0;
    uint32_t m_uniformEpoch = 0;
    uint32_t m_uniformEpochBuilt = 0;
    uint32_t m_uniformPrimMask = 0;
    uint32_t m_uniformMasterEnables = 0;
    int32_t m_uniformGpuDrawMode = 0;
    int32_t m_uniformSkin = -1;
    int32_t m_uniformDisableTex = -1;
    int32_t m_uniformDrawKind = 0;
    int32_t m_skinActive = 0;
    int32_t m_skinThisDraw = 0;
    int32_t m_bindPoseMesh = 0;
    uint32_t m_skinInfluences = 0;
    int32_t m_uniformInfluences = -1;
    VkDeviceSize m_skinOffset = 0;
    VkDeviceSize m_skinOffsetThis = 0;
    uint32_t m_skinMatrixBase = 0;
    uint32_t m_skinMatrixBaseThis = 0;
    uint32_t m_skinStride = 0;
    uint32_t m_skinStrideThis = 0;
    uint32_t m_pushBase = 0xFFFFFFFFu;
    uint32_t m_pushInfluences = 0xFFFFFFFFu;
    uint32_t m_pushStride = 0xFFFFFFFFu;
    uint32_t m_boundBoneOffset = 0xFFFFFFFFu;
    CGxApiLight m_uniformLights[4] = {};
    VkPipeline m_boundPipeline = VK_NULL_HANDLE;
    VkDescriptorSet m_boundSet = VK_NULL_HANDLE;
    uint32_t m_boundDynamicOffset = 0xFFFFFFFFu;
    VkImageView m_setViews[2] = {};
    VkSampler m_setSamplers[2] = {};
    VkBuffer m_boundBuffers[MaxBindings] = {};
    VkDeviceSize m_boundOffsets[MaxBindings] = {};
    VkBuffer m_boundIndexBuffer = VK_NULL_HANDLE;
    int32_t m_gpuDrawMode = 0;
    uint32_t m_drawInstances = 0;
    uint32_t m_firstInstance = 0;
    uint32_t m_indirectCount = 0;
    VkDeviceSize m_indirectOffset = 0;
    VkViewport m_boundViewport = {};
    VkRect2D m_boundScissor = {};
    float m_boundDepthBias = 0.0f;
    float m_boundBlendFactor = 0.0f;
    int32_t m_dynamicValid = 0;
    float m_stereoConvergence = 1.0f;
    float m_stereoSeparation = 25.0f;

    virtual void ITexMarkAsUpdated(CGxTex* texId);
    virtual void IRsSendToHw(EGxRenderState which);
    virtual int32_t DeviceCreate(GxWindowProc windowProc, const CGxFormat& format);
    virtual int32_t DeviceCreate(void* window, const CGxFormat& format);
    virtual void DeviceDestroy();
    virtual int32_t DeviceSetFormat(const CGxFormat& format);
    virtual void DeviceSetBaseMipLevel(uint32_t level);
    virtual void* DeviceWindow();
    virtual void DeviceReadPixels(CiRect& rect, TSGrowableArray<CImVector>& pixels);
    virtual void DeviceReadDepths(CiRect& rect, TSGrowableArray<float>& depths);
    virtual int32_t DeviceWM(EGxWM wm, uintptr_t param1, uintptr_t param2);
    virtual void DeviceSetRenderTarget(EGxBuffer buffer, CGxTex* texture, uint32_t plane);
    virtual void DeviceOverride(EGxOverride override, uint32_t value);
    virtual void CapsWindowSize(CRect& rect);
    virtual void CapsWindowSizeInScreenCoords(CRect& rect);
    virtual void LogCrashInfo(char* buffer, uint32_t size);
    virtual void ScenePresent();
    virtual void SceneClear(uint32_t mask, CImVector color);
    virtual void XformSetProjection(const C44Matrix& matrix);
    virtual void Draw(CGxBatch* batch, int32_t count);
    virtual int32_t GpuSkinBind(const C44Matrix* bones, uint32_t boneCount);
    virtual int32_t GpuSkinBindInstances(const C44Matrix* const* bones, uint32_t instances, uint32_t boneCount);
    virtual int32_t GpuSkinSupported() const;
    virtual void DrawInstanced(CGxBatch* batch, const C44Matrix* worlds, uint32_t count);
    virtual void GpuSkinMesh(int32_t bindPose);
    virtual int32_t GpuSkinMesh() const;
    virtual void GpuSkinInfluences(uint32_t count);
    virtual int32_t DrawTerrainBatch(const GxTerrainDraw* draws, uint32_t count, int32_t mode = 2);
    virtual int32_t TerrainBindless();
    virtual uint32_t TerrainTextureSlot(CGxTex* tex);
    virtual uint64_t ResidentHeapBytes() const;
    virtual void ResolveTexMatrix(uint32_t tmu, C44Matrix& matrix);
    virtual void PoolSizeSet(CGxPool* pool, uint32_t size);
    virtual void PoolDestroy(CGxPool* pool);
    virtual char* BufLock(CGxBuf* buf);
    virtual int32_t BufUnlock(CGxBuf* buf, uint32_t size);
    virtual void BufData(CGxBuf* buf, const void* data, uint32_t size, uint32_t offset);
    virtual void TexDestroy(CGxTex* texId);
    virtual int32_t TexCopy(CGxTex* dest, const C2iVector& destPos, const C2iVector& sourcePos, const C2iVector& size, uint32_t level, uint32_t plane);
    virtual int32_t TexStretch(CGxTex* dest, const CiRect* destRect, const CiRect* sourceRect, uint32_t level, uint32_t face);
    virtual bool QueryBegin(CGxQuery* query);
    virtual bool QueryEnd(CGxQuery* query);
    virtual bool QueryGetParam(CGxQuery* query, EGxQueryParam param, uint32_t& data);
    virtual bool QueryGetData(CGxQuery* query, uint32_t& data);
    virtual void IShaderReload(CGxShader* shader, const char* path, const char* name);
    virtual void IShaderCreate(CGxShader* shader);
    virtual void StereoSetConvergence(float convergence);
    virtual float StereoGetConvergence();
    virtual void StereoSetSeparation(float separation);
    virtual float StereoGetSeparation();
    virtual bool StereoEnabled();
    virtual void DeviceUnk81(uint32_t a, uint32_t b);
    virtual void DeviceUnk82();
    virtual void DeviceUnk83(uint32_t a, uint32_t b);

    CGxDeviceVulkan();
    virtual ~CGxDeviceVulkan();

    int32_t IPlatformInit();
    void IPlatformShutdown();
    int32_t ICreateWindow(CGxFormat& format);
    void IDestroyWindow();
    int32_t ICreateInstance();
    VkSurfaceKHR ICreateSurface();
    void IClientSize(uint32_t& width, uint32_t& height);

    int32_t ICreateVulkan(const CGxFormat& format);
    int32_t ICreateDeviceObjects();
    void IDestroyVulkan();
    int32_t IRecreateSwapchain();
    void ISetCaps(const CGxFormat& format);
    void IStateSetDefaults();
    void ISceneBegin();
    void IReloadExperimentalShaders();
    void ISceneEnd();
    void IApplyPost();
    void IApplyAtmosphere();
    int32_t IEnsurePost();
    void IReleasePostTargets();
    void IDestroyPost();
    void ISceneAbort();
    void IInvalidateCmdState();
    void IBeginPass();
    void IEndPass();
    void IEnsureFrameImage();
    VkBufferAlloc* IPoolBuffer(CGxPool* pool);
    void IReleasePool(CGxPool* pool);
    void IReleasePools();
    char* IBufLock(CGxBuf* buf);
    void IBufUnlock(CGxBuf* buf);
    void ITexCreate(CGxTex* texId);
    int32_t ITexUpload(CGxTex* texId);
    void ITexRelease(CGxTex* texId);
    void ITexForceRecreation(int32_t all);
    VkTexture* ITexReady(CGxTex* texId);
    VkFramebuffer ITexFramebuffer(VkTexture* texture);
    VkSampler ISampler(const CGxTexFlags& flags);
    VkPipeline IPipeline(const VkPipelineKey& key);
    void ResetTerrainTable();
    int32_t EnsureTerrainSet();
    void ISetTexGen(uint32_t tmu, int32_t texGen);
    void IXformTex(uint32_t tmu, int32_t& mode, C44Matrix& matrix);
    void IBuildUniforms(VkDrawUniforms& uniforms);
    void IXformSetViewport();
    void IStateSyncScissorRect();
    void ISetDynamicState();
    int32_t IBlitFromBackBuffer(CGxTex* dest, const VkOffset3D* destBounds, const VkOffset3D* sourceBounds, uint32_t level, VkFilter filter);
};

CGxDevice* CreateCGxDeviceVulkan();

#endif
