#ifndef ENGINE_SOURCE_GX_CGXDEVICEVULKAN_VKCONTEXT_H
#define ENGINE_SOURCE_GX_CGXDEVICEVULKAN_VKCONTEXT_H

#include <cstdint>
#include <cstring>
#include <map>
#include <unordered_map>
#include <vector>
#include <volk.h>

struct VkAllocation {
    VkDeviceMemory memory;
    VkDeviceSize offset;
    VkDeviceSize size;
    int32_t block;
};

struct VkBufferAlloc {
    VkBuffer buffer;
    VkDeviceMemory memory;
    uint8_t* mapped;
    VkDeviceSize size;
    VkBufferUsageFlags usage = 0;
};

struct VkImageAlloc {
    VkImage image;
    VkAllocation alloc;
    VkImageView view;
    VkFormat format;
};

struct VkGarbage {
    VkBuffer buffer;
    VkDeviceMemory memory;
    uint8_t* mapped = nullptr;
    VkDeviceSize size = 0;
    VkBufferUsageFlags usage = 0;
    VkImage image;
    VkImageView view;
    VkFramebuffer framebuffer;
    VkAllocation alloc;
};

struct VkStaging {
    VkBuffer buffer;
    VkDeviceSize offset;
    uint8_t* data;
};

class VkHeap {
    public:
    enum {
        BlockSize = 64 << 20,
    };

    struct Block {
        VkDeviceMemory memory;
        VkDeviceSize size;
        uint32_t type;
        std::map<VkDeviceSize, VkDeviceSize> free;
    };

    std::vector<Block> m_blocks;

    int32_t Alloc(VkDevice device, const VkMemoryRequirements& requirements, uint32_t type, VkAllocation& out);
    void Free(const VkAllocation& alloc);
    void Destroy(VkDevice device);
};

struct VkSetKey {
    VkImageView views[2];
    VkSampler samplers[2];
    bool operator==(const VkSetKey& key) const { return !memcmp(this, &key, sizeof(VkSetKey)); }
};

struct VkSetKeyHash {
    size_t operator()(const VkSetKey& key) const {
        const uint64_t* words = reinterpret_cast<const uint64_t*>(&key);
        size_t hash = 0;
        for (size_t i = 0; i < sizeof(VkSetKey) / sizeof(uint64_t); i++) hash = (hash ^ words[i]) * 0x100000001B3ull;
        return hash;
    }
};

struct VkFrame {
    VkCommandBuffer cmd;
    VkCommandBuffer uploadCmd;
    VkFence fence;
    VkSemaphore imageAvailable;
    std::vector<VkDescriptorPool> descriptorPools;
    uint32_t descriptorPoolCursor = 0;
    std::unordered_map<VkSetKey, VkDescriptorSet, VkSetKeyHash> sets;
    VkBufferAlloc uniforms;
    VkDeviceSize uniformOffset;
    VkBufferAlloc staging;
    VkDeviceSize stagingOffset;
    std::vector<VkGarbage> garbage;
    int32_t prepared;
    int32_t recording;
    int32_t uploadRecording;
    int32_t acquired;
    int32_t imageWaited;
    int32_t fenceUnsignaled;
};

class VkContext {
    public:
    enum {
        FramesInFlight = 2,
        UniformRingSize = 32 << 20,
        StagingRingSize = 16 << 20,
        DescriptorSetsPerFrame = 8192,
    };

    enum EPass {
        Pass_Clear = 0,
        Pass_Load = 1,
        Pass_Offscreen = 2,
        Passes_Last = 3,
    };

    static constexpr VkFormat s_offscreenFormat = VK_FORMAT_B8G8R8A8_UNORM;

    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties m_properties = {};
    VkPhysicalDeviceFeatures m_features = {};
    VkPhysicalDeviceMemoryProperties m_memoryProperties = {};
    VkDevice m_device = VK_NULL_HANDLE;
    uint32_t m_queueFamily = 0;
    VkQueue m_queue = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkFormat m_colorFormat = VK_FORMAT_UNDEFINED;
    VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D m_extent = {};
    int32_t m_suboptimal = 0;
    std::vector<VkImage> m_swapImages;
    std::vector<VkImageView> m_swapViews;
    std::vector<VkFramebuffer> m_swapFramebuffers;
    std::vector<VkSemaphore> m_renderFinished;
    VkImageAlloc m_depth = {};
    VkRenderPass m_passes[Passes_Last] = {};
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkFrame m_frames[FramesInFlight] = {};
    std::vector<VkBufferAlloc> m_spareBuffers;
    static constexpr size_t MaxSpareBuffers = 16;
    static constexpr uint64_t MaxSpareBytes = 16ull * 1024 * 1024;
    VkHeap m_heap;
    uint32_t m_frameIndex = 0;
    uint32_t m_imageIndex = 0;

    int32_t CreateInstance(const char* const* extensions, uint32_t extensionCount);
    int32_t CreateDevice(VkSurfaceKHR surface);
    int32_t CreateDeviceResources();
    int32_t SetSurface(VkSurfaceKHR surface);
    void DestroySurface();
    int32_t CreateSwapchain(uint32_t width, uint32_t height, int32_t vsync);
    void DestroySwapchain();
    void DestroyDevice();
    void DestroyInstance();

    VkFrame& Frame();
    void PrepareFrame();
    void FlushUploads();
    int32_t AcquireBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBufferAlloc& out);
    VkDescriptorPool CreateDescriptorPool();
    VkDescriptorSet AllocateSet(VkDescriptorSetLayout layout);
    VkDescriptorSet CachedSet(const VkSetKey& key, VkDescriptorSetLayout layout, int32_t& created);
    // ʕ ◕ᴥ◕ ʔ✿ 1 acquired, 0 recreate needed, 2 skip this frame (timeout) ✿ ʕ ◕ᴥ◕ ʔ
    int32_t BeginFrame();
    VkCommandBuffer UploadCmd();
    int32_t Submit(int32_t present);
    int32_t EndFrame();
    void AbandonFrame();
    void FlushFrame();

    uint32_t FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags flags);
    int32_t CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBufferAlloc& out);
    void DestroyBuffer(VkBufferAlloc& buffer);
    int32_t CreateImage(const VkImageCreateInfo& info, VkImageAspectFlags aspect, VkImageViewType viewType, const VkComponentMapping& swizzle, VkImageAlloc& out);
    VkStaging Stage(VkDeviceSize size);
    void Retire(const VkGarbage& garbage);
    void RetireBuffer(VkBufferAlloc& buffer);
    void RetireImage(VkImageAlloc& image);
    void Collect(VkFrame& frame);

    static void Barrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, uint32_t levels, VkImageLayout oldLayout, VkImageLayout newLayout);

    VkRenderPass CreateRenderPass(VkFormat color, VkAttachmentLoadOp load, VkImageLayout colorInitial, VkImageLayout colorFinal, VkImageLayout depthInitial);
};

#endif
