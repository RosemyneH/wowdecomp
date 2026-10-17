#include <windows.h>
#include <volk.h>
#include "Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.h"
#include "Engine/Source/Gx/Gx.h"

static const char* s_windowClassName = "GxWindowClassVulkan";

static void SendSizeMessage(CGxDeviceVulkan* device, LPARAM lparam, uint32_t resizeType) {
    CRect rect;
    rect.minY = 0.0f;
    rect.minX = 0.0f;
    rect.maxY = HIWORD(lparam);
    rect.maxX = LOWORD(lparam);
    device->DeviceWM(GxWM_Size, reinterpret_cast<uintptr_t>(&rect), resizeType);
}

static LRESULT CALLBACK WindowProcVulkan(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    CGxDeviceVulkan* device = reinterpret_cast<CGxDeviceVulkan*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));

    switch (message) {
        case WM_CREATE:
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTA*>(lparam)->lpCreateParams));
            return 0;

        case WM_DESTROY:
            if (device) {
                device->DeviceWM(GxWM_Destroy, 0, 0);
            }
            return 0;

        case WM_SIZE:
            if (device) {
                SendSizeMessage(device, lparam, wparam == SIZE_MINIMIZED ? 1 : wparam == SIZE_MAXHIDE ? 2 : 0);
            }
            break;

        case WM_SETFOCUS:
            if (device) {
                device->DeviceWM(GxWM_SetFocus, 0, 0);
            }
            return 0;

        case WM_KILLFOCUS:
            if (device) {
                device->DeviceWM(GxWM_KillFocus, 0, 0);
            }
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT paint;
            BeginPaint(hwnd, &paint);
            EndPaint(hwnd, &paint);
            return 0;
        }

        case WM_ERASEBKGND:
            return 0;

        case WM_SETCURSOR:
            if (!device) {
                return DefWindowProcA(hwnd, message, wparam, lparam);
            }

            if (device->m_context && LOWORD(lparam) == HTCLIENT) {
                SetCursor(nullptr);
                return 1;
            }

            break;

        case WM_DISPLAYCHANGE:
            if (device && !device->IDevIsWindowed()) {
                CRect rect;
                rect.minY = 0.0f;
                rect.minX = 0.0f;
                rect.maxY = HIWORD(lparam);
                rect.maxX = LOWORD(lparam);
                device->DeviceWM(GxWM_DisplayChange, reinterpret_cast<uintptr_t>(&rect), 0);
            }

            break;

        case WM_SYSCOMMAND:
            if (wparam == SC_SCREENSAVE || wparam == SC_MONITORPOWER) {
                return 0;
            }

            break;

        default:
            break;
    }

    if (device && device->m_windowProc) {
        return device->m_windowProc(hwnd, message, wparam, lparam);
    }

    return DefWindowProcA(hwnd, message, wparam, lparam);
}

int32_t CGxDeviceVulkan::IPlatformInit() {
    if (!this->m_ownWindow || this->m_windowClass) {
        return 1;
    }

    HINSTANCE instance = GetModuleHandleA(nullptr);

    WNDCLASSEXA windowClass;
    memset(&windowClass, 0, sizeof(windowClass));
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_OWNDC;
    windowClass.lpfnWndProc = WindowProcVulkan;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = s_windowClassName;
    windowClass.hIcon = static_cast<HICON>(LoadImageA(instance, "BlizzardIcon.ico", IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
    windowClass.hCursor = LoadCursorA(instance, "BlizzardCursor.cur");

    if (!windowClass.hCursor) {
        windowClass.hCursor = LoadCursorA(instance, IDC_ARROW);
    }

    this->m_windowClass = RegisterClassExA(&windowClass);

    return this->m_windowClass != 0;
}

void CGxDeviceVulkan::IPlatformShutdown() {
    if (this->m_windowClass) {
        UnregisterClassA(s_windowClassName, GetModuleHandleA(nullptr));
        this->m_windowClass = 0;
    }
}

int32_t CGxDeviceVulkan::ICreateWindow(CGxFormat& format) {
    if (!this->m_ownWindow) {
        return this->m_window != nullptr;
    }

    this->IDestroyWindow();

    DWORD style;

    if (!format.m_window) {
        style = WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_SYSMENU;
    } else if (format.m_maximize == 1) {
        style = WS_POPUP | WS_VISIBLE;
    } else if (format.m_maximize == 2) {
        style = WS_POPUP;
    } else {
        style = WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    }

    float aspectRatio = 0.0f;

    if (format.m_aspect && format.m_size.x && format.m_size.y) {
        aspectRatio = static_cast<float>(format.m_size.x) / format.m_size.y;
    }

    RECT clientRect = { 0, 0, format.m_size.x, format.m_size.y };
    AdjustWindowRectEx(&clientRect, style, FALSE, 0);

    CGxDevice::s_clientAdjust.x = clientRect.right - format.m_size.x - clientRect.left;
    CGxDevice::s_clientAdjust.y = clientRect.bottom - format.m_size.y - clientRect.top;

    if (!format.m_window || format.m_maximize == 1) {
        format.m_pos.x = 0;
        format.m_pos.y = 0;
        format.m_size.x = GetSystemMetrics(SM_CXSCREEN);
        format.m_size.y = GetSystemMetrics(SM_CYSCREEN);
    } else {
        RECT windowRect = { 0, 0, format.m_size.x, format.m_size.y };

        if (CGxDevice::AdjustWindowRectToWorkArea(aspectRatio, &CGxDevice::s_clientAdjust, &windowRect)) {
            format.m_pos.x = windowRect.left;
            format.m_pos.y = windowRect.top;
            format.m_size.x = windowRect.right - windowRect.left;
            format.m_size.y = windowRect.bottom - windowRect.top;
        } else {
            format.m_pos.x = 0;
            format.m_pos.y = 0;
        }
    }

    int32_t width = format.m_size.x ? format.m_size.x : CW_USEDEFAULT;
    int32_t height = format.m_size.y ? format.m_size.y : CW_USEDEFAULT;

    if (format.m_window && format.m_maximize != 1 && format.m_size.x && format.m_size.y) {
        width += CGxDevice::s_clientAdjust.x;
        height += CGxDevice::s_clientAdjust.y;
    }

    HWND hwnd = CreateWindowExA(WS_EX_APPWINDOW, s_windowClassName, "World of Warcraft", style, format.m_pos.x, format.m_pos.y, width, height, nullptr, nullptr, GetModuleHandleA(nullptr), this);

    if (hwnd && format.m_maximize != 2) {
        ShowWindow(hwnd, SW_SHOWNORMAL);
    }

    this->m_window = hwnd;

    return hwnd != nullptr;
}

void CGxDeviceVulkan::IDestroyWindow() {
    if (this->m_ownWindow && this->m_window) {
        HWND hwnd = static_cast<HWND>(this->m_window);
        DestroyWindow(hwnd);
        this->m_window = nullptr;
    }
}

int32_t CGxDeviceVulkan::ICreateInstance() {
    if (this->m_vk.m_instance) {
        return 1;
    }

    static const char* extensions[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME };

    return this->m_vk.CreateInstance(extensions, 2);
}

VkSurfaceKHR CGxDeviceVulkan::ICreateSurface() {
    if (!this->m_window || !this->m_vk.m_instance) {
        return VK_NULL_HANDLE;
    }

    VkWin32SurfaceCreateInfoKHR info = { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
    info.hinstance = GetModuleHandleA(nullptr);
    info.hwnd = static_cast<HWND>(this->m_window);

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (vkCreateWin32SurfaceKHR(this->m_vk.m_instance, &info, nullptr, &surface) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    return surface;
}

void CGxDeviceVulkan::IClientSize(uint32_t& width, uint32_t& height) {
    RECT rect = {};
    if (this->m_window) {
        GetClientRect(static_cast<HWND>(this->m_window), &rect);
    }
    width = rect.right - rect.left;
    height = rect.bottom - rect.top;
}

void CGxDeviceVulkan::CapsWindowSizeInScreenCoords(CRect& rect) {
    const CRect& window = this->DeviceCurWindow();

    if (!this->IDevIsWindowed()) {
        rect = window;
        return;
    }

    POINT points[2] = { { 0, 0 }, { static_cast<LONG>(window.maxX), static_cast<LONG>(window.maxY) } };
    MapWindowPoints(static_cast<HWND>(this->m_window), nullptr, points, 2);
    rect.minY = static_cast<float>(points[0].y);
    rect.minX = static_cast<float>(points[0].x);
    rect.maxY = static_cast<float>(points[1].y);
    rect.maxX = static_cast<float>(points[1].x);
}
