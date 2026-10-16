#include <cerrno>
#include <cstdint>
#include "Engine/Source/BlizzardCore/Source/System/File/File.h"
#include "Engine/Source/BlizzardCore/Source/System/String/Path.h"
#include "Engine/Source/BlizzardCore/Source/System/String/String.h"
#include "Engine/Source/Os/OsFile.h"

#define OS_FILE_SHARE_READ 0x1
#define OS_FILE_SHARE_WRITE 0x2
#define OS_FILE_ATTRIBUTE_READONLY 0x1
#define OS_FILE_ATTRIBUTE_HIDDEN 0x2
#define OS_CREATE_NEW 1
#define OS_OPEN_ALWAYS 4
#define OS_TRUNCATE_EXISTING 5

// OFFSET: 0x00461B00
bool OsCloseFile(HOSFILE file) {
    return Blizzard::File::Close(reinterpret_cast<Blizzard::File::StreamRecord*>(file));
}

// OFFSET: 0x00461B10
int32_t OsFileExists(const char* fileName) {
    return Blizzard::File::IsFile(fileName);
}

// OFFSET: 0x00461B30
int32_t OsDirectoryExists(const char* pathName) {
    return Blizzard::File::IsDirectory(pathName);
}

// OFFSET: 0x00461B50
int32_t OsReadFile(HOSFILE file, void* buffer, uint32_t bytesToRead, uint32_t* bytesRead) {
    if (buffer && bytesRead) {
        *bytesRead = bytesToRead;
        return Blizzard::File::Read(reinterpret_cast<Blizzard::File::StreamRecord*>(file), buffer, reinterpret_cast<int32_t*>(bytesRead));
    }

    errno = EINVAL;
    return 0;
}

// OFFSET: 0x00461B90
int32_t OsWriteFile(HOSFILE file, const void* buffer, uint32_t bytesToWrite, uint32_t* bytesWritten) {
    if (buffer && bytesWritten) {
        *bytesWritten = bytesToWrite;
        return Blizzard::File::Write(reinterpret_cast<Blizzard::File::StreamRecord*>(file), buffer, reinterpret_cast<int32_t*>(bytesWritten));
    }

    errno = EINVAL;
    return 0;
}

// OFFSET: 0x00461BD0
uint64_t OsGetFileSize(HOSFILE file) {
    Blizzard::File::FileInfo* info = Blizzard::File::GetFileInfo(reinterpret_cast<Blizzard::File::StreamRecord*>(file));
    return info ? info->m_size : 0xFFFFFFFF;
}

// OFFSET: 0x00461C00
uint32_t OsGetFileAttributes(const char* fileName) {
    if (!fileName) {
        errno = EINVAL;
        return 0;
    }

    Blizzard::File::FileInfo info;
    if (!Blizzard::File::GetFileInfo(fileName, &info)) {
        return 0xFFFFFFFF;
    }

    return info.m_attributes;
}

// OFFSET: 0x00461C40
int32_t OsSetFileAttributes(const char* fileName, uint32_t attributes) {
    if (!fileName) {
        errno = EINVAL;
        return 0;
    }

    return Blizzard::File::SetAttributes(fileName, attributes);
}

// OFFSET: 0x00461C70
int32_t OsMoveFile(const char* existingFileName, const char* newFileName) {
    if (existingFileName && newFileName) {
        return Blizzard::File::Move(existingFileName, newFileName);
    }

    errno = EINVAL;
    return 0;
}

// OFFSET: 0x00461CA0
int32_t OsCopyFile(const char* existingFileName, const char* newFileName, int32_t failIfExists) {
    if (existingFileName && newFileName) {
        return Blizzard::File::Copy(existingFileName, newFileName, failIfExists == 0);
    }

    errno = EINVAL;
    return 0;
}

// OFFSET: 0x00461CE0
int32_t OsDeleteFile(const char* fileName) {
    if (!fileName) {
        errno = EINVAL;
        return 0;
    }

    return Blizzard::File::Delete(fileName);
}

// OFFSET: 0x00461D10
int32_t OsCreateDirectory(const char* pathName, int32_t recursive) {
    if (!pathName) {
        errno = EINVAL;
        return 0;
    }

    return Blizzard::File::CreateDirectory(pathName, recursive != 0);
}

// OFFSET: 0x00461D40
int32_t OsRemoveDirectory(const char* pathName) {
    if (!pathName) {
        errno = EINVAL;
        return 0;
    }

    return Blizzard::File::RemoveDirectory(pathName);
}

// OFFSET: 0x00461D70
int32_t OsRemoveDirectoryRecurse(const char* pathName, uint32_t flags) {
    if (!pathName) {
        errno = EINVAL;
        return 0;
    }

    return Blizzard::File::RemoveDirectoryAndContents(pathName, (flags & 0x1) != 0);
}

// OFFSET: 0x00461DB0
bool OsSetCurrentDirectory(const char* path) {
    if (path) {
        return Blizzard::File::SetWorkingDirectory(path) != 0;
    }

    errno = EINVAL;

    return false;
}

// OFFSET: 0x00461DE0
int32_t OsGetCurrentDirectory(uint32_t bufferSize, char* buffer) {
    if (!buffer) {
        errno = EINVAL;
        return 0;
    }

    return Blizzard::File::GetWorkingDirectory(buffer, bufferSize);
}

// OFFSET: 0x00461E10
bool OsFileList_Internal::Callback(const Blizzard::File::ProcessDirParms& parms) {
    OS_FILE_DATA data;
    Blizzard::String::Copy(data.fileName, parms.m_filename, sizeof(data.fileName));

    char path[260];
    Blizzard::String::JoinPath(path, sizeof(path), parms.m_pathRoot, parms.m_filename);

    OsFileList_Internal* list = static_cast<OsFileList_Internal*>(parms.m_cookie);

    if (!Blizzard::File::SimpleGlob(parms.m_filename, list->m_pattern)) {
        return true;
    }

    Blizzard::File::FileInfo info;
    if (!Blizzard::File::GetFileInfo(path, &info)) {
        return true;
    }

    data.size = static_cast<uint32_t>(info.m_size);
    data.flags = 0;

    if (info.m_attributes & 0x40) {
        data.flags = OS_FILE_ATTRIBUTE_DIRECTORY;
    }

    if (info.m_attributes & 0x1) {
        data.flags |= OS_FILE_ATTRIBUTE_READONLY;
    }

    if (info.m_attributes & 0x2) {
        data.flags |= OS_FILE_ATTRIBUTE_HIDDEN;
    }

    if (!list->m_includeHidden && (info.m_attributes & 0x2)) {
        return true;
    }

    return !list->m_callback(data, list->m_param);
}

// OFFSET: 0x00461EF0
uint32_t GetFileMode(uint32_t desiredAccess, uint32_t shareMode, uint32_t createDisposition) {
    uint32_t mode = 0;

    if (desiredAccess & OS_GENERIC_READ) {
        mode = 0x1;
    }

    if (desiredAccess & OS_GENERIC_WRITE) {
        mode |= 0x2;
    }

    if (shareMode & OS_FILE_SHARE_READ) {
        mode |= 0x4;
    }

    if (shareMode & OS_FILE_SHARE_WRITE) {
        mode |= 0x8;
    }

    switch (createDisposition) {
        case OS_CREATE_NEW:
            mode |= 0xC00;
            break;

        case OS_CREATE_ALWAYS:
            mode |= 0x400;
            break;

        case OS_OPEN_EXISTING:
            mode |= 0x1000;
            break;

        case OS_OPEN_ALWAYS:
            mode |= 0x200;
            break;

        case OS_TRUNCATE_EXISTING:
            mode |= 0x100;
            break;
    }

    return mode;
}

// OFFSET: 0x00461F70
char* OsFileGetDownloadFolder() {
    static char s_downloadfolder[260];

    if (!s_downloadfolder[0]) {
        Blizzard::File::GetWorkingDirectory(s_downloadfolder, sizeof(s_downloadfolder));
    }

    return s_downloadfolder;
}

// OFFSET: 0x00461FA0
HOSFILE OsCreateFile(const char* fileName, uint32_t desiredAccess, uint32_t shareMode, uint32_t createDisposition, uint32_t flagsAndAttributes, uint32_t extendedFileType) {
    if (!fileName || !desiredAccess || createDisposition < OS_CREATE_NEW || createDisposition > OS_TRUNCATE_EXISTING) {
        return OS_INVALID_FILE;
    }

    Blizzard::File::StreamRecord* file;
    if (!Blizzard::File::Open(fileName, GetFileMode(desiredAccess, shareMode, createDisposition), file)) {
        return OS_INVALID_FILE;
    }

    if (!(flagsAndAttributes & OS_FILE_ATTRIBUTE_NORMAL)) {
        OsSetFileAttributes(fileName, flagsAndAttributes);
    }

    return reinterpret_cast<HOSFILE>(file);
}

// OFFSET: 0x00462000
int32_t OsFileList(const char* dir, const char* pattern, OsFileListCallback callback, void* param, int32_t includeHidden) {
    char relative[260];
    Blizzard::String::Copy(relative, dir, sizeof(relative));

    char* separator = Blizzard::String::FindPathSeparatorReverse(relative, 0x7FFFFFFF);
    if (separator) {
        *separator = '\0';
    }

    char path[260];
    if (!Blizzard::File::MakeAbsolutePath(relative, path, sizeof(path), false)) {
        return 0;
    }

    OsFileList_Internal list;
    list.m_callback = callback;
    list.m_param = param;
    list.m_includeHidden = includeHidden;
    list.m_pattern = pattern;

    return Blizzard::File::ProcessDirFast(path, &list, OsFileList_Internal::Callback, false);
}
