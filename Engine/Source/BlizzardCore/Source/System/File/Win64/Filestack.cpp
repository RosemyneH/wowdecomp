#if defined(WOWDECOMP_ARCH_64)

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <windows.h>
#include <sddl.h>
#include "Engine/Source/BlizzardCore/Source/System/Debug/Debug.h"
#include "Engine/Source/BlizzardCore/Source/System/File/File.h"
#include "Engine/Source/BlizzardCore/Source/System/Lock/Lock.h"
#include "Engine/Source/BlizzardCore/Source/System/Memory/MemoryStorm.h"
#include "Engine/Source/BlizzardCore/Source/System/Memory/TempBuffer.h"
#include "Engine/Source/BlizzardCore/Source/System/String/Path.h"
#include "Engine/Source/BlizzardCore/Source/System/String/String.h"
#include "Engine/Source/BlizzardCore/Source/System/Time/Time.h"
#include "Engine/Source/BlizzardCore/Source/System/Unicode/Unicode.h"
#include "Engine/Source/Storm/H/storm.h"

#undef CreateDirectory
#undef GetFreeSpace
#undef RemoveDirectory

// STATIC: 0x00AB912C
System_File::IOFunc System_File::read_func = System_File::read_first;

// STATIC: 0x00AB91B4
System_File::IOFunc System_File::write_func = System_File::write_first;

// STATIC: 0x00B38A5C
bool System_File::s_read_init;

// STATIC: 0x00B38A60
Blizzard::Lock::Atomic System_File::s_readInitCount;

// STATIC: 0x00B38A64
bool System_File::s_write_init;

// STATIC: 0x00B38A68
Blizzard::Lock::Atomic System_File::s_writeInitCount;

// STATIC: 0x00B332B0
Blizzard::Lock::Mutex System_File::s_ioMutex;

// STATIC: 0x00B3327C
Blizzard::Lock::Mutex* System_File::s_ioLock;

// OFFSET: 0x00427C20
int32_t System_File::ToNativeAttributes(int32_t attributes) {
    int32_t result = (attributes & 0x1) != 0 ? FILE_ATTRIBUTE_READONLY : 0;

    if (attributes & 0x2) {
        result |= FILE_ATTRIBUTE_HIDDEN;
    }

    if (attributes & 0x4) {
        result |= FILE_ATTRIBUTE_SYSTEM;
    }

    if (attributes & 0x8) {
        result |= FILE_ATTRIBUTE_ARCHIVE;
    }

    if (attributes & 0x10) {
        result |= FILE_ATTRIBUTE_TEMPORARY;
    }

    if (attributes & 0x20) {
        result |= FILE_ATTRIBUTE_NORMAL;
    }

    if (attributes & 0x40) {
        result |= FILE_ATTRIBUTE_DIRECTORY;
    }

    return result;
}

// OFFSET: 0x00427C70
int32_t System_File::FromNativeAttributes(int32_t attributes) {
    int32_t result = (attributes & FILE_ATTRIBUTE_READONLY) != 0 ? 0x1 : 0;

    if (attributes & FILE_ATTRIBUTE_HIDDEN) {
        result |= 0x2;
    }

    if (attributes & FILE_ATTRIBUTE_SYSTEM) {
        result |= 0x4;
    }

    if (attributes & FILE_ATTRIBUTE_ARCHIVE) {
        result |= 0x8;
    }

    if (attributes & FILE_ATTRIBUTE_TEMPORARY) {
        result |= 0x10;
    }

    if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
        result |= 0x40;
    }

    if ((attributes & FILE_ATTRIBUTE_NORMAL) || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        result |= 0x20;
    }

    return result;
}

// OFFSET: 0x00427CC0
bool System_File::ReadP(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    OVERLAPPED overlapped;
    overlapped.Offset = static_cast<DWORD>(parms->m_pos);
    overlapped.OffsetHigh = static_cast<DWORD>(parms->m_pos >> 32);
    overlapped.Internal = 0;
    overlapped.InternalHigh = 0;
    overlapped.hEvent = nullptr;

    return read_func(parms->m_stream, parms->m_buffer, &overlapped, &parms->m_amount);
}

// OFFSET: 0x00427D10
bool System_File::WriteP(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    OVERLAPPED overlapped;
    overlapped.Offset = static_cast<DWORD>(parms->m_pos);
    overlapped.OffsetHigh = static_cast<DWORD>(parms->m_pos >> 32);
    overlapped.Internal = 0;
    overlapped.InternalHigh = 0;
    overlapped.hEvent = nullptr;

    return write_func(parms->m_stream, parms->m_buffer, &overlapped, &parms->m_amount);
}

// OFFSET: 0x0042D620
bool System_File::Close(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    Blizzard::File::StreamRecord* stream = parms->m_stream;

    if (stream->m_fileHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(stream->m_fileHandle);
    }

    SMemFree(stream, __FILE__, __LINE__, 0);

    return true;
}

// OFFSET: 0x004325C0
bool System_File::GetRootChars(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    const char* path = parms->m_path;
    Blizzard::Memory::TempBuffer<char, 256> buffer((path ? static_cast<uint32_t>(strlen(path)) : 0) + 1);

    if (buffer.m_count < 2) {
        parms->m_pos = 0;
        return true;
    }

    Blizzard::String::MakeUnivPath(path, buffer.m_data, buffer.m_count);
    char* univ = buffer.m_data;

    if (univ[1] == ':') {
        parms->m_pos = (univ[2] == '/') + 2;
    } else if (univ[0] == '/' && univ[1] == '/') {
        const char* separator = univ + 1;
        int32_t count = 0;

        for (;;) {
            separator = strchr(separator + 1, '/');

            if (!separator) {
                parms->m_pos = buffer.m_count;
                break;
            }

            if (++count >= 2) {
                parms->m_pos = separator - univ + 1;
                break;
            }
        }
    } else {
        parms->m_pos = 0;
    }

    return true;
}

// OFFSET: 0x00435580
void System_File::GetFileInfoByFile(Blizzard::File::StreamRecord* file) {
    if (file->m_infoValid) {
        return;
    }

    file->m_infoValid = false;
    DWORD sizeHigh;
    DWORD sizeLow = GetFileSize(file->m_fileHandle, &sizeHigh);

    if (sizeLow == INVALID_FILE_SIZE && GetLastError()) {
        return;
    }

    if (!file->m_streaming || !file->m_streaming->m_entry) {
        file->m_info.m_size = (static_cast<int64_t>(sizeHigh) << 32) | sizeLow;
    }

    WIN32_FILE_ATTRIBUTE_DATA data;
    BOOL found;

    {
        Blizzard::StringConversion_CodepageToUTF8::ToNative name(file->m_pathname);
        found = GetFileAttributesExA(name.m_data, GetFileExInfoStandard, &data);
    }

    if (found) {
        file->m_info.m_createTime = Blizzard::Time::FromFileTime((static_cast<uint64_t>(data.ftCreationTime.dwHighDateTime) << 32) | data.ftCreationTime.dwLowDateTime);
        file->m_info.m_modTime = Blizzard::Time::FromFileTime((static_cast<uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime);
        file->m_info.m_accessTime = Blizzard::Time::FromFileTime((static_cast<uint64_t>(data.ftLastAccessTime.dwHighDateTime) << 32) | data.ftLastAccessTime.dwLowDateTime);
        int32_t attributes = FromNativeAttributes(data.dwFileAttributes);
        file->m_info.m_attributes = attributes;
        file->m_info.m_regularFile = (attributes & 0x20) != 0;

        if (data.dwFileAttributes == INVALID_FILE_ATTRIBUTES) {
            file->m_info.m_existence = 0;
        } else {
            file->m_info.m_existence = ((attributes & 0x40) != 0) + 1;
        }
    }

    file->m_infoValid = true;
}

// OFFSET: 0x004356B0
bool System_File::Exists(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    char path[1024];
    Blizzard::String::MakeBackslashPath(parms->m_path, path, sizeof(path));

    DWORD attributes;

    {
        Blizzard::StringConversion_CodepageToUTF8::ToNative name(path);
        attributes = GetFileAttributesA(name.m_data);
    }

    if (attributes == INVALID_FILE_ATTRIBUTES) {
        parms->m_info->m_existence = 0;
    } else if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
        parms->m_info->m_existence = 2;
    } else {
        parms->m_info->m_existence = 1;
    }

    return true;
}

// OFFSET: 0x00435790
bool System_File::MakeAbsolutePath(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    char buffer[1024];

    {
        Blizzard::StringConversion_CodepageToUTF8::ToNative name(parms->m_path);
        _fullpath(buffer, name.m_data, sizeof(buffer));
    }

    FromNativeName(parms->m_cwdbuf, parms->m_cwdbuflen, buffer);
    Blizzard::String::MakeUnivPath(parms->m_cwdbuf, parms->m_cwdbuf, parms->m_cwdbuflen);

    const char* path = parms->m_path;

    if (path[(path ? strlen(path) : 0) - 1] == '/') {
        Blizzard::String::ForceTrailingSeparator(parms->m_cwdbuf, parms->m_cwdbuflen, '/');
    }

    return true;
}

// OFFSET: 0x00435890
bool System_File::Move(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    char src[1024];
    char dst[1024];
    Blizzard::String::MakeBackslashPath(parms->m_path, src, sizeof(src));
    Blizzard::String::MakeBackslashPath(parms->m_newpath, dst, sizeof(dst));

    Blizzard::StringConversion_CodepageToUTF8::ToNative newName(dst);
    Blizzard::StringConversion_CodepageToUTF8::ToNative oldName(src);

    return MoveFileA(oldName.m_data, newName.m_data) != 0;
}

// OFFSET: 0x004359C0
bool System_File::Delete(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    char path[1024];
    Blizzard::String::MakeBackslashPath(parms->m_path, path, sizeof(path));

    Blizzard::StringConversion_CodepageToUTF8::ToNative name(path);

    return DeleteFileA(name.m_data) != 0;
}

// OFFSET: 0x00454870
bool System_File::read_overlapped(Blizzard::File::StreamRecord* file, void* buffer, OVERLAPPED* overlapped, int32_t* count) {
    if (file->m_fileHandle == INVALID_HANDLE_VALUE) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    if (!*count) {
        return true;
    }

    DWORD bytesRead;
    BOOL result = ReadFile(file->m_fileHandle, buffer, *count, &bytesRead, overlapped);
    *count = bytesRead;

    if (result) {
        return true;
    }

    Blizzard::File::SetLastError(5);
    Blizzard::File::AddToLastErrorStack(5, Blizzard::String::QuickFormat<1024>("Win32 Read - %s", file->m_pathname).m_buffer, 1);

    return false;
}

// OFFSET: 0x00454910
bool System_File::write_overlapped(Blizzard::File::StreamRecord* file, void* buffer, OVERLAPPED* overlapped, int32_t* count) {
    if (file->m_fileHandle == INVALID_HANDLE_VALUE) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    if (!*count) {
        return true;
    }

    file->m_infoValid = false;
    DWORD bytesWritten;
    BOOL result = WriteFile(file->m_fileHandle, buffer, *count, &bytesWritten, overlapped);
    *count = bytesWritten;

    if (result) {
        return true;
    }

    Blizzard::File::SetLastError(6);
    Blizzard::File::AddToLastErrorStack(6, Blizzard::String::QuickFormat<1024>("Win32 Write - %s", file->m_pathname).m_buffer, 1);

    return false;
}

// OFFSET: 0x004549B0
bool System_File::ToCreateFlags(uint32_t flags, DWORD* sharemode, DWORD* desiredaccess, DWORD* creationdisposition, DWORD* flagsandattributes) {
    *sharemode = (flags >> 2) & 3;
    *desiredaccess = ((flags * 4) | (flags & 2)) << 29;
    *creationdisposition = 0;

    if ((flags & 0x100) || (flags & 0x400)) {
        *creationdisposition = (flags & 0x800) ? CREATE_NEW : CREATE_ALWAYS;
    } else if (flags & 0x800) {
        *creationdisposition = CREATE_NEW;
    } else {
        *creationdisposition = OPEN_ALWAYS - ((flags & 0x1000) != 0);
    }

    *flagsandattributes = (2 * ((flags & 0x80) | 0x40)) | ((flags & 0x40) ? (FILE_FLAG_WRITE_THROUGH | FILE_FLAG_NO_BUFFERING) : 0);

    return true;
}

// OFFSET: 0x00454A50
bool System_File::SetWorkingDirectory(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    if (!parms->m_path) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    Blizzard::StringConversion_CodepageToUTF8::ToNative name(parms->m_path);

    return SetCurrentDirectoryA(name.m_data) != 0;
}

// OFFSET: 0x00454AC0
bool System_File::Create(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    Blizzard::File::SetLastError(9);

    return false;
}

// OFFSET: 0x00454AE0
bool System_File::GetWorkingDirectory(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    if (!parms->m_cwdbuf || parms->m_cwdbuflen <= 0) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    *parms->m_cwdbuf = '\0';
    char buffer[1024];
    GetCurrentDirectoryA(sizeof(buffer), buffer);
    FromNativeName(parms->m_cwdbuf, parms->m_cwdbuflen, buffer);

    return true;
}

// OFFSET: 0x00454B50
bool System_File::ProcessDirFast(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    void* cookie = parms->m_cookie;
    const char* path = parms->m_path;
    Blizzard::File::ProcessDirCallback callback = parms->m_dircallback;

    char pattern[512];
    Blizzard::String::Format(pattern, sizeof(pattern), "%s\\*", path);

    WIN32_FIND_DATAA findData;
    HANDLE find;

    {
        Blizzard::StringConversion_CodepageToUTF8::ToNative name(pattern);
        find = FindFirstFileA(name.m_data, &findData);
    }

    if (find == INVALID_HANDLE_VALUE) {
        Blizzard::Debug::Printf(Blizzard::Debug::StderrOutput, "failed to open %s\n", path);
        Blizzard::File::SetLastError(8);
        return false;
    }

    Blizzard::File::ProcessDirParms dirParms;
    dirParms.m_pathRoot = path;
    dirParms.m_cookie = cookie;

    do {
        if (findData.cFileName[0] != '.' || (findData.cFileName[1] && (findData.cFileName[1] != '.' || findData.cFileName[2]))) {
            bool isDir = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            char filename[1024];
            FromNativeName(filename, sizeof(filename), findData.cFileName);
            dirParms.m_filename = filename;
            dirParms.m_isDir = isDir;

            if (!callback(dirParms)) {
                FindClose(find);
                return false;
            }
        }
    } while (FindNextFileA(find, &findData));

    FindClose(find);

    return true;
}

// OFFSET: 0x00454CB0
bool System_File::Flush(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    if (parms->m_stream->m_fileHandle == INVALID_HANDLE_VALUE) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    return FlushFileBuffers(parms->m_stream->m_fileHandle) != 0;
}

// OFFSET: 0x00454CF0
bool System_File::GetFileInfo(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    if (parms->m_path) {
        WIN32_FILE_ATTRIBUTE_DATA data;
        BOOL found;

        {
            Blizzard::StringConversion_CodepageToUTF8::ToNative name(parms->m_path);
            found = GetFileAttributesExA(name.m_data, GetFileExInfoStandard, &data);
        }

        if (!found) {
            Blizzard::File::SetLastError(8);
            return false;
        }

        parms->m_info->m_size = (static_cast<int64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
        parms->m_info->m_createTime = Blizzard::Time::FromFileTime((static_cast<uint64_t>(data.ftCreationTime.dwHighDateTime) << 32) | data.ftCreationTime.dwLowDateTime);
        parms->m_info->m_modTime = Blizzard::Time::FromFileTime((static_cast<uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime);
        parms->m_info->m_accessTime = Blizzard::Time::FromFileTime((static_cast<uint64_t>(data.ftLastAccessTime.dwHighDateTime) << 32) | data.ftLastAccessTime.dwLowDateTime);
        parms->m_info->m_path = nullptr;
        parms->m_info->m_attributes = FromNativeAttributes(data.dwFileAttributes);
        parms->m_info->m_regularFile = (parms->m_info->m_attributes & 0x20) != 0;

        if (data.dwFileAttributes == INVALID_FILE_ATTRIBUTES) {
            parms->m_info->m_existence = 0;
        } else {
            parms->m_info->m_existence = ((parms->m_info->m_attributes & 0x40) != 0) + 1;
        }

        return true;
    }

    if (parms->m_stream) {
        GetFileInfoByFile(parms->m_stream);
        Blizzard::File::StreamRecord* stream = parms->m_stream;
        parms->m_info = &stream->m_info;

        if (!stream->m_info.m_path) {
            stream->m_info.m_path = stream->m_pathname;
        }

        return parms->m_stream->m_infoValid;
    }

    Blizzard::File::SetLastError(8);

    return false;
}

// OFFSET: 0x00454E70
bool System_File::GetFreeSpace(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    const char* path = parms->m_path;

    if (path && *path) {
        char directory[MAX_PATH];
        Blizzard::String::Copy(directory, path, std::min<int32_t>(static_cast<int32_t>(Blizzard::String::FindFilename(path) - path + 1), MAX_PATH));

        ULARGE_INTEGER freeBytes;
        ULARGE_INTEGER totalBytes;
        freeBytes.QuadPart = 0;
        totalBytes.QuadPart = 0;

        char shortPath[MAX_PATH];
        DWORD shortLength;

        {
            Blizzard::StringConversion_CodepageToUTF8::ToNative name(directory);
            shortLength = GetShortPathNameA(name.m_data, shortPath, MAX_PATH);
        }

        if (shortLength && shortLength < MAX_PATH) {
            if (!GetDiskFreeSpaceExA(shortPath, &freeBytes, &totalBytes, nullptr)) {
                Blizzard::File::SetLastError(8);
                return false;
            }

            parms->m_pos = freeBytes.QuadPart;
            return true;
        }

        BOOL result;

        {
            Blizzard::StringConversion_CodepageToUTF8::ToNative name(directory);
            result = GetDiskFreeSpaceExA(name.m_data, &freeBytes, &totalBytes, nullptr);
        }

        if (result) {
            parms->m_pos = freeBytes.QuadPart;
            return true;
        }
    }

    Blizzard::File::SetLastError(8);

    return false;
}

// OFFSET: 0x00454FD0
bool System_File::GetPos(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    Blizzard::File::StreamRecord* stream = parms->m_stream;
    LONG high;
    DWORD low;

    if (stream->m_fileHandle == INVALID_HANDLE_VALUE || (high = 0, low = SetFilePointer(stream->m_fileHandle, 0, &high, FILE_CURRENT), low == INVALID_SET_FILE_POINTER && GetLastError())) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    parms->m_pos = (static_cast<int64_t>(high) << 32) | low;

    return true;
}

// OFFSET: 0x00455050
bool System_File::IsAbsolutePath(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    const char* path = parms->m_path;

    if (((path[0] == '\\' || path[0] == '/') && (path[1] == '\\' || path[1] == '/') && path[2]) || (isalpha(path[0]) && path[1] == ':' && (path[2] == '\\' || path[2] == '/') && path[3])) {
        return true;
    }

    Blizzard::File::SetLastError(8);

    return false;
}

// OFFSET: 0x004550C0
bool System_File::IsReadOnly(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    DWORD attributes;

    {
        Blizzard::StringConversion_CodepageToUTF8::ToNative name(parms->m_path);
        attributes = GetFileAttributesA(name.m_data);
    }

    if (attributes != INVALID_FILE_ATTRIBUTES) {
        return attributes & FILE_ATTRIBUTE_READONLY;
    }

    Blizzard::File::SetLastError(8);

    return false;
}

// OFFSET: 0x00455130
bool System_File::CreateDirectory(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    bool recursive = parms->m_recursive;
    const char* path = parms->m_path;
    bool grantEveryone = parms->m_grantEveryone;

    if (!path) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    Blizzard::Memory::TempBuffer<char, MAX_PATH> buffer(static_cast<uint32_t>(strlen(path)) + 1);
    Blizzard::String::MakeBackslashPath(path, buffer.m_data, buffer.m_count);

    SECURITY_ATTRIBUTES attributes;
    memset(&attributes, 0, sizeof(attributes));
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = FALSE;

    if (grantEveryone) {
        ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;OICI;GA;;;BU)", SDDL_REVISION_1, &attributes.lpSecurityDescriptor, nullptr);
    }

    if (recursive) {
        char* start = buffer.m_data;

        for (char* end = start; *end && end < start + strlen(start); end++) {
            while (*end && *end != '\\') {
                end++;
            }

            char partial[MAX_PATH];

            if (Blizzard::String::Copy(partial, start, std::min<uint32_t>(static_cast<uint32_t>(end - start + 2), MAX_PATH)) >= MAX_PATH) {
                Blizzard::File::SetLastError(8);
                return false;
            }

            Blizzard::StringConversion_CodepageToUTF8::ToNative name(partial);

            if (CreateDirectoryA(name.m_data, grantEveryone ? &attributes : nullptr)) {
                if (!*end) {
                    return true;
                }
            } else {
                DWORD error = GetLastError();
                DWORD existing;

                if ((error != ERROR_ALREADY_EXISTS && error != ERROR_ACCESS_DENIED) || (existing = GetFileAttributesA(name.m_data)) == INVALID_FILE_ATTRIBUTES) {
                    Blizzard::File::SetLastError(8);
                    return false;
                }

                if (!(existing & FILE_ATTRIBUTE_DIRECTORY)) {
                    Blizzard::File::SetLastError(8);
                    return false;
                }
            }

            start = buffer.m_data;
        }

        return true;
    }

    BOOL created;

    {
        Blizzard::StringConversion_CodepageToUTF8::ToNative name(buffer.m_data);
        created = CreateDirectoryA(name.m_data, grantEveryone ? &attributes : nullptr);
    }

    if (created) {
        return true;
    }

    DWORD error = GetLastError();

    return error == ERROR_ALREADY_EXISTS || error == ERROR_ACCESS_DENIED;
}

// OFFSET: 0x004554C0
bool System_File::Copy(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    bool overwrite = parms->m_overwrite;
    char src[1024];
    char dst[1024];
    Blizzard::String::MakeBackslashPath(parms->m_path, src, sizeof(src));
    Blizzard::String::MakeBackslashPath(parms->m_newpath, dst, sizeof(dst));

    Blizzard::File::StreamRecord* input;

    if (!Blizzard::File::Open(src, 0x1005, input)) {
        Blizzard::File::SetLastError(2);
        return false;
    }

    Blizzard::File::StreamRecord* output;

    if (!Blizzard::File::Open(dst, (overwrite ? 0x400 : 0xC00) | 0x2, output)) {
        Blizzard::File::Close(input);
        Blizzard::File::SetLastError(4);
        return false;
    }

    int64_t size = Blizzard::File::GetFileInfo(input)->m_size;
    int64_t offset = 0;
    bool result = true;
    void* buffer = Blizzard::Memory::Allocate(size >= 0xA00000 ? 0xA00000 : static_cast<uint32_t>(size));

    if (size > 0) {
        for (;;) {
            int32_t count = size - offset >= 0xA00000 ? 0xA00000 : static_cast<int32_t>(size - offset);

            if (!Blizzard::File::Read(input, buffer, offset, &count)) {
                break;
            }

            if (!Blizzard::File::Write(output, buffer, offset, &count)) {
                break;
            }

            offset += count;

            if (offset >= size) {
                goto done;
            }
        }

        result = false;
    }

done:
    Blizzard::Memory::Free(buffer);
    Blizzard::File::Close(input);
    Blizzard::File::Close(output);

    if (!result) {
        Blizzard::File::Delete(dst);
    }

    return result;
}

// OFFSET: 0x00455730
bool System_File::Open(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    int32_t mode = parms->m_mode;
    char path[1024];
    Blizzard::String::MakeBackslashPath(parms->m_path, path, sizeof(path));

    DWORD sharemode;
    DWORD desiredaccess;
    DWORD creationdisposition;
    DWORD flagsandattributes;

    if (!ToCreateFlags(mode, &sharemode, &desiredaccess, &creationdisposition, &flagsandattributes)) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    HANDLE handle;

    {
        Blizzard::StringConversion_CodepageToUTF8::ToNative name(path);
        handle = CreateFileA(name.m_data, desiredaccess, sharemode, nullptr, creationdisposition, flagsandattributes, nullptr);
    }

    if (handle == INVALID_HANDLE_VALUE) {
        Blizzard::File::SetLastError(4);
        Blizzard::File::AddToLastErrorStack(4, Blizzard::String::QuickFormat<1024>("Win32 Open - %s", path).m_buffer, 1);
        return false;
    }

    Blizzard::File::StreamRecord* stream = parms->m_stream;

    if (!stream) {
        uint32_t length = static_cast<uint32_t>(strlen(path));
        stream = static_cast<Blizzard::File::StreamRecord*>(SMemAlloc(sizeof(Blizzard::File::StreamRecord) + length + 1, __FILE__, __LINE__, SMEM_FLAG_ZEROMEMORY));
        if (!stream) {
            CloseHandle(handle);
            return false;
        }
        stream->m_pathname = reinterpret_cast<char*>(stream + 1);
        Blizzard::String::Copy(stream->m_pathname, path, length + 1);
    }

    stream->m_fileHandle = handle;
    stream->m_mode = mode;
    stream->m_infoValid = false;
    stream->m_info.m_path = stream->m_pathname;
    GetFileInfoByFile(stream);

    if (mode & 0x200) {
        Blizzard::File::SetPos(stream, stream->m_info.m_size, 0);
    }

    parms->m_stream = stream;

    return true;
}

// OFFSET: 0x004558F0
bool System_File::Read(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    return read_overlapped(parms->m_stream, parms->m_buffer, nullptr, &parms->m_amount);
}

// OFFSET: 0x00455910
bool System_File::RemoveDirectory(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    const char* path = parms->m_path;

    if (parms->m_recursive) {
        return Blizzard::File::RemoveDirectoryAndContents(path, false);
    }

    if (!path) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    Blizzard::Memory::TempBuffer<char, MAX_PATH> buffer(static_cast<uint32_t>(strlen(path)) + 1);
    Blizzard::String::MakeBackslashPath(path, buffer.m_data, buffer.m_count);

    Blizzard::StringConversion_CodepageToUTF8::ToNative name(buffer.m_data);

    return RemoveDirectoryA(name.m_data) != 0;
}

// OFFSET: 0x00455A30
bool System_File::SetCacheMode(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    Blizzard::File::StreamRecord* stream = parms->m_stream;
    stream->m_mode = parms->m_mode | (stream->m_mode & ~0x40);

    DWORD sharemode;
    DWORD desiredaccess;
    DWORD creationdisposition;
    DWORD flagsandattributes;

    if (!ToCreateFlags(stream->m_mode, &sharemode, &desiredaccess, &creationdisposition, &flagsandattributes)) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    CloseHandle(stream->m_fileHandle);

    {
        Blizzard::StringConversion_CodepageToUTF8::ToNative name(stream->m_info.m_path);
        stream->m_fileHandle = CreateFileA(name.m_data, desiredaccess, sharemode, nullptr, creationdisposition, flagsandattributes, nullptr);
    }

    if (stream->m_fileHandle == INVALID_HANDLE_VALUE) {
        Blizzard::File::SetLastError(4);
        Blizzard::File::AddToLastErrorStack(4, "Win32 Close", 1);
        return false;
    }

    return true;
}

// OFFSET: 0x00455B10
bool System_File::SetEOF(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    int64_t pos = parms->m_pos;
    Blizzard::File::StreamRecord* stream = parms->m_stream;
    int32_t whence = parms->m_posMode;
    int64_t offset;

    if (!Blizzard::File::GetPos(stream, &offset) || !Blizzard::File::SetPos(stream, pos, whence)) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    BOOL result = SetEndOfFile(stream->m_fileHandle);
    Blizzard::File::SetPos(stream, offset, 0);

    return result != 0;
}

// OFFSET: 0x00455BA0
bool System_File::SetAttributes(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    if (parms->m_has.m_all & 0x10) {
        Blizzard::Time::Timestamp time = parms->m_info->m_modTime;
        int64_t ticks = time / 100 + 125911584000000000;

        if (time < 0 && time % 100) {
            ticks = time / 100 + 125911583999999999;
        }

        FILETIME fileTime;
        fileTime.dwLowDateTime = static_cast<DWORD>(ticks);
        fileTime.dwHighDateTime = static_cast<DWORD>(ticks >> 32);

        if (!SetFileTime(parms->m_stream->m_fileHandle, nullptr, nullptr, &fileTime)) {
            Blizzard::File::SetLastError(8);
            return false;
        }

        parms->m_stream->m_info.m_modTime = time;
        parms->m_has.m_all &= ~0x10u;
    }

    if (parms->m_has.m_all & 0x4) {
        int32_t attributes = ToNativeAttributes(parms->m_info->m_attributes);
        BOOL result;

        {
            Blizzard::StringConversion_CodepageToUTF8::ToNative name(parms->m_path);
            result = SetFileAttributesA(name.m_data, attributes);
        }

        if (!result) {
            Blizzard::File::SetLastError(8);
            return false;
        }

        parms->m_has.m_all &= ~0x4u;
    }

    return parms->m_has.m_all == 0;
}

// OFFSET: 0x00455CA0
bool System_File::SetPos(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    Blizzard::File::StreamRecord* stream = parms->m_stream;
    LONG low = static_cast<LONG>(parms->m_pos);
    DWORD method = parms->m_posMode;

    if (stream->m_fileHandle == INVALID_HANDLE_VALUE) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    LONG high = static_cast<LONG>(parms->m_pos >> 32);

    if (method) {
        method = (method != 1) + 1;
    }

    return SetFilePointer(stream->m_fileHandle, low, &high, method) != INVALID_SET_FILE_POINTER || !GetLastError();
}

// OFFSET: 0x00455D20
bool System_File::Write(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    return write_overlapped(parms->m_stream, parms->m_buffer, nullptr, &parms->m_amount);
}

// OFFSET: 0x00457820
bool System_File::read_locked(Blizzard::File::StreamRecord* file, void* buffer, OVERLAPPED* overlapped, int32_t* count) {
    Blizzard::Lock::ScopedMutex lock(*s_ioLock);

    if (!Blizzard::File::SetPos(file, (static_cast<int64_t>(overlapped->OffsetHigh) << 32) | overlapped->Offset, 0)) {
        *count = 0;
        Blizzard::File::SetLastError(9);
        return false;
    }

    return read_overlapped(file, buffer, nullptr, count);
}

// OFFSET: 0x00457910
bool System_File::write_locked(Blizzard::File::StreamRecord* file, void* buffer, OVERLAPPED* overlapped, int32_t* count) {
    Blizzard::Lock::ScopedMutex lock(*s_ioLock);

    if (!Blizzard::File::SetPos(file, (static_cast<int64_t>(overlapped->OffsetHigh) << 32) | overlapped->Offset, 0)) {
        *count = 0;
        Blizzard::File::SetLastError(9);
        return false;
    }

    return write_overlapped(file, buffer, nullptr, count);
}

// OFFSET: 0x00459910
void System_File::detect_io_mode() {
    OSVERSIONINFOA info;
    info.dwOSVersionInfoSize = sizeof(info);
    memset(&info.dwMajorVersion, 0, sizeof(info) - sizeof(info.dwOSVersionInfoSize));

    if (GetVersionExA(&info) && info.dwMajorVersion >= 5) {
        read_func = read_overlapped;
        write_func = write_overlapped;
    } else {
        Blizzard::Lock::MutexCreate(s_ioMutex);
        s_ioLock = &s_ioMutex;
        read_func = read_locked;
        write_func = write_locked;
    }
}

// OFFSET: 0x0045B440
bool System_File::read_first(Blizzard::File::StreamRecord* file, void* buffer, OVERLAPPED* overlapped, int32_t* count) {
    if (!s_read_init) {
        if (s_readInitCount.Increment() == 1) {
            detect_io_mode();
            s_read_init = true;
        } else {
            do {
                Sleep(1);
            } while (!s_read_init);
        }
    }

    return read_func(file, buffer, overlapped, count);
}

// OFFSET: 0x0045B490
bool System_File::write_first(Blizzard::File::StreamRecord* file, void* buffer, OVERLAPPED* overlapped, int32_t* count) {
    if (!s_write_init) {
        if (s_writeInitCount.Increment() == 1) {
            detect_io_mode();
            s_write_init = true;
        } else {
            do {
                Sleep(1);
            } while (!s_write_init);
        }
    }

    return write_func(file, buffer, overlapped, count);
}

// OFFSET: 0x00433590
void System_File::FromNativeName(char* buffer, uint32_t buffersize, const char* str) {
    uint32_t length = str ? static_cast<uint32_t>(strlen(str)) : 0;
    Blizzard::Memory::TempBuffer<uint16_t, 300> wide(length + 1);
    wide.m_data[MultiByteToWideChar(CP_ACP, 0, str, length, reinterpret_cast<wchar_t*>(wide.m_data), wide.m_count)] = 0;
    Blizzard::Unicode::ConvertUTF16to8(buffer, buffersize, wide.m_data, -1, nullptr, nullptr);
}

#endif
