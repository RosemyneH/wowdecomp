#ifndef ENGINE_SOURCE_BASE_PLATFORMTYPES_H
#define ENGINE_SOURCE_BASE_PLATFORMTYPES_H

#include "Engine/Source/Base/OsTypes.h"

#if !defined(_WIN32)

#ifndef STORMAPI
#define STORMAPI
#endif

#ifndef __fastcall
#define __fastcall
#endif

#ifndef __thiscall
#define __thiscall
#endif

#ifndef _alloca
#define _alloca __builtin_alloca
#endif

typedef void* HMODULE;
typedef void* HKEY;
typedef void* HDC;
typedef void* HGLRC;
typedef void* HICON;
typedef void* HCURSOR;
typedef void* HMENU;
typedef void* HACCEL;
typedef void* HFONT;
typedef void* HBRUSH;
typedef void* HPEN;
typedef void* HBITMAP;
typedef void* HRGN;
typedef void* HIMC;
typedef void* HKL;
typedef void* LPVOID;
typedef void* PVOID;
typedef const void* LPCVOID;
typedef char* LPSTR;
typedef const char* LPCSTR;
typedef wchar_t* LPWSTR;
typedef const wchar_t* LPCWSTR;
typedef uint8_t BYTE;
typedef int32_t INT;
typedef uint32_t UINT;
typedef uintptr_t UINT_PTR;
typedef intptr_t LONG_PTR;
typedef uintptr_t ULONG_PTR;
typedef LONG_PTR LPARAM;
typedef UINT_PTR WPARAM;
typedef LONG_PTR LRESULT;
typedef DWORD COLORREF;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

struct tagRECT {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
};

typedef struct tagRECT RECT;

#define CP_UTF8 65001

inline UINT GetACP() {
    return CP_UTF8;
}

#endif

#endif
