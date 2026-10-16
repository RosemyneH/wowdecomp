#include <new>
#include "Engine/Source/SteelseriesMouse/HidManager.h"
#include "Engine/Source/Storm/H/storm.h"

HidManager::~HidManager() {
}

class HidManagerImpl : public HidManager {
    public:
    ~HidManagerImpl() override {
    }

    bool EnumerateDevices() override {
        return false;
    }

    void SetWindow(void* window) override {
    }

    uint32_t GetDeviceCount() override {
        return 0;
    }

    bool WriteWait(const void* data, uint32_t size) override {
        return false;
    }

    bool Write(const void* data, uint32_t size) override {
        return false;
    }

    bool SetFeature(void* data, uint32_t size) override {
        return false;
    }
};

HidManager* HidManager::Create() {
    return new (SMemAlloc(sizeof(HidManagerImpl), __FILE__, __LINE__, 0)) HidManagerImpl();
}
