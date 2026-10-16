#if defined(WOWDECOMP_WIN_ARM64)

#include <windows.h>
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxDeviceD3d/CGxDeviceD3d.h"
#include "Engine/Source/Gx/CGxDeviceD3d9Ex/CGxDeviceD3d9Ex.h"

CGxDevice* CreateCGxDeviceD3d() {
    return nullptr;
}

CGxDevice* CreateCGxDeviceD3d9Ex() {
    return nullptr;
}

CGxDevice* CreateCGxDeviceOpenGl() {
    return nullptr;
}

int32_t CGxDevice::OpenGlAdapterFormats(TSGrowableArray<CGxFormat>& formats) {
    formats.SetCount(0);
    return 0;
}

int32_t CGxDevice::D3dAdapterFormats(TSGrowableArray<CGxFormat>& formats) {
    formats.SetCount(0);
    return 0;
}

int32_t CGxDevice::D3d9ExAdapterFormats(TSGrowableArray<CGxFormat>& formats) {
    formats.SetCount(0);
    return 0;
}

int32_t CGxDeviceD3d::ILoadD3dLib(HINSTANCE& d3dLib, IDirect3D9*& d3d) {
    d3dLib = nullptr;
    d3d = nullptr;
    return 0;
}

void CGxDeviceD3d::IUnloadD3dLib(HINSTANCE& d3dLib, IDirect3D9*& d3d) {
    d3dLib = nullptr;
    d3d = nullptr;
}

int32_t CGxDeviceD3d9Ex::ILoadD3dLib(HINSTANCE& d3dLib, IDirect3D9*& d3d) {
    d3dLib = nullptr;
    d3d = nullptr;
    return 0;
}

void CGxDeviceD3d9Ex::IUnloadD3dLib(HINSTANCE& d3dLib, IDirect3D9*& d3d) {
    d3dLib = nullptr;
    d3d = nullptr;
}

#endif
