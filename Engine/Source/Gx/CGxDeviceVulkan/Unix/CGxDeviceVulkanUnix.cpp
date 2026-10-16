#include "Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.h"
#include "Engine/Source/Os/Gui.h"

static void WindowProcVulkan(void* param, OSGUIWINDOWMSG message, int32_t width, int32_t height) {
    CGxDeviceVulkan* device = static_cast<CGxDeviceVulkan*>(param);

    CRect rect;
    rect.minY = 0.0f;
    rect.minX = 0.0f;
    rect.maxY = static_cast<float>(height);
    rect.maxX = static_cast<float>(width);

    switch (message) {
        case OS_GUI_WM_SIZE:
            device->DeviceWM(GxWM_Size, reinterpret_cast<uintptr_t>(&rect), width && height ? 0 : 1);
            break;

        case OS_GUI_WM_MINIMIZE:
            device->DeviceWM(GxWM_Size, reinterpret_cast<uintptr_t>(&rect), 1);
            break;

        case OS_GUI_WM_DISPLAYCHANGE:
            device->DeviceWM(GxWM_DisplayChange, reinterpret_cast<uintptr_t>(&rect), 0);
            break;

        case OS_GUI_WM_SETFOCUS:
            device->DeviceWM(GxWM_SetFocus, 0, 0);
            break;

        case OS_GUI_WM_KILLFOCUS:
            device->DeviceWM(GxWM_KillFocus, 0, 0);
            break;
    }
}

int32_t CGxDeviceVulkan::IPlatformInit() {
    return 1;
}

void CGxDeviceVulkan::IPlatformShutdown() {
}

int32_t CGxDeviceVulkan::ICreateWindow(CGxFormat& format) {
    if (!this->m_ownWindow) {
        return this->m_window != nullptr;
    }

    this->IDestroyWindow();

    int32_t width = format.m_size.x ? format.m_size.x : 1024;
    int32_t height = format.m_size.y ? format.m_size.y : 768;

    this->m_window = OsGuiCreateGxWindow("World of Warcraft", width, height, !format.m_window);

    if (!this->m_window) {
        return 0;
    }

    OsGuiSetGxWindowProc(this->m_window, WindowProcVulkan, this);
    OsGuiSetGxWindow(this->m_window);

    return 1;
}

void CGxDeviceVulkan::IDestroyWindow() {
    if (!this->m_ownWindow) {
        return;
    }

    if (this->m_window && OsGuiGetWindow(0) == this->m_window) {
        OsGuiSetGxWindowProc(nullptr, nullptr, nullptr);
        OsGuiSetGxWindow(nullptr);
    }

    if (this->m_window) {
        OsGuiDestroyGxWindow(this->m_window);
        this->m_window = nullptr;
    }
}

int32_t CGxDeviceVulkan::ICreateInstance() {
    if (this->m_vk.m_instance) {
        return 1;
    }

    const char* extensions[16];
    uint32_t count = 16;

    if (!OsGuiGetVulkanInstanceExtensions(this->m_window, &count, extensions)) {
        return 0;
    }

    return this->m_vk.CreateInstance(extensions, count);
}

VkSurfaceKHR CGxDeviceVulkan::ICreateSurface() {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    OsGuiCreateVulkanSurface(this->m_window, this->m_vk.m_instance, &surface);

    return surface;
}

void CGxDeviceVulkan::IClientSize(uint32_t& width, uint32_t& height) {
    int32_t drawableWidth;
    int32_t drawableHeight;
    OsGuiGetDrawableSize(this->m_window, &drawableWidth, &drawableHeight);
    width = drawableWidth > 0 ? drawableWidth : 0;
    height = drawableHeight > 0 ? drawableHeight : 0;
}

void CGxDeviceVulkan::CapsWindowSizeInScreenCoords(CRect& rect) {
    rect = this->DeviceCurWindow();
}
