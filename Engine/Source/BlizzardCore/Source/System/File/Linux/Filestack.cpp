#if defined(__linux__) || defined(__APPLE__)

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include "Engine/Source/BlizzardCore/Source/System/Debug/Debug.h"
#include "Engine/Source/BlizzardCore/Source/System/File/File.h"
#include "Engine/Source/BlizzardCore/Source/System/Lock/Lock.h"
#include "Engine/Source/BlizzardCore/Source/System/Memory/MemoryStorm.h"
#include "Engine/Source/BlizzardCore/Source/System/Memory/TempBuffer.h"
#include "Engine/Source/BlizzardCore/Source/System/String/Path.h"
#include "Engine/Source/BlizzardCore/Source/System/String/String.h"
#include "Engine/Source/BlizzardCore/Source/System/Time/Time.h"
#include "Engine/Source/Storm/H/storm.h"

#define NATIVE_GENERIC_READ 0x80000000
#define NATIVE_GENERIC_WRITE 0x40000000
#define NATIVE_CREATE_NEW 1
#define NATIVE_CREATE_ALWAYS 2
#define NATIVE_OPEN_EXISTING 3
#define NATIVE_OPEN_ALWAYS 4
#define NATIVE_FLAG_WRITE_THROUGH 0x80000000
#define NATIVE_FLAG_NO_BUFFERING 0x20000000

System_File::IOFunc System_File::read_func = System_File::read_first;

System_File::IOFunc System_File::write_func = System_File::write_first;

bool System_File::s_read_init;

Blizzard::Lock::Atomic System_File::s_readInitCount;

bool System_File::s_write_init;

Blizzard::Lock::Atomic System_File::s_writeInitCount;

Blizzard::Lock::Mutex System_File::s_ioMutex;

Blizzard::Lock::Mutex* System_File::s_ioLock;

static int HandleToFd(HANDLE handle) {
    return static_cast<int>(reinterpret_cast<intptr_t>(handle)) - 1;
}

static HANDLE FdToHandle(int fd) {
    return fd < 0 ? INVALID_HANDLE_VALUE : reinterpret_cast<HANDLE>(static_cast<intptr_t>(fd) + 1);
}

static Blizzard::Time::Timestamp FromTimespec(const timespec& time) {
    return (static_cast<int64_t>(time.tv_sec) - 946684800LL) * 1000000000LL + time.tv_nsec;
}

static int OpenNative(const char* path, uint32_t mode) {
    DWORD sharemode;
    DWORD desiredaccess;
    DWORD creationdisposition;
    DWORD flagsandattributes;
    System_File::ToCreateFlags(mode, &sharemode, &desiredaccess, &creationdisposition, &flagsandattributes);

    int flags;

    if ((desiredaccess & NATIVE_GENERIC_READ) && (desiredaccess & NATIVE_GENERIC_WRITE)) {
        flags = O_RDWR;
    } else if (desiredaccess & NATIVE_GENERIC_WRITE) {
        flags = O_WRONLY;
    } else {
        flags = O_RDONLY;
    }

    switch (creationdisposition) {
        case NATIVE_CREATE_NEW:
            flags |= O_CREAT | O_EXCL;
            break;
        case NATIVE_CREATE_ALWAYS:
            flags |= O_CREAT | O_TRUNC;
            break;
        case NATIVE_OPEN_ALWAYS:
            flags |= O_CREAT;
            break;
        default:
            break;
    }

    return open(path, flags | O_CLOEXEC, 0644);
}

static void FillFileInfo(Blizzard::File::FileInfo* info, const struct stat& st, const char* path) {
#if defined(__APPLE__)
    info->m_createTime = FromTimespec(st.st_ctimespec);
    info->m_modTime = FromTimespec(st.st_mtimespec);
    info->m_accessTime = FromTimespec(st.st_atimespec);
#else
    info->m_createTime = FromTimespec(st.st_ctim);
    info->m_modTime = FromTimespec(st.st_mtim);
    info->m_accessTime = FromTimespec(st.st_atim);
#endif
    info->m_attributes = System_File::FromNativeAttributes(st.st_mode);

    const char* name = Blizzard::String::FindFilename(path);

    if (name && name[0] == '.' && name[1] && strcmp(name, "..")) {
        info->m_attributes |= 0x2;
    }

    info->m_regularFile = (info->m_attributes & 0x20) != 0;
    info->m_existence = ((info->m_attributes & 0x40) != 0) + 1;
}

int32_t System_File::ToNativeAttributes(int32_t attributes) {
    return (attributes & 0x1) ? 0444 : 0644;
}

int32_t System_File::FromNativeAttributes(int32_t attributes) {
    int32_t result = (attributes & S_IWUSR) ? 0 : 0x1;

    if (S_ISDIR(attributes)) {
        result |= 0x40;
    } else {
        result |= 0x20;
    }

    return result;
}

bool System_File::ReadP(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    OVERLAPPED overlapped;
    overlapped.Offset = static_cast<DWORD>(parms->m_pos);
    overlapped.OffsetHigh = static_cast<DWORD>(parms->m_pos >> 32);
    overlapped.Internal = 0;
    overlapped.InternalHigh = 0;
    overlapped.hEvent = nullptr;

    return read_func(parms->m_stream, parms->m_buffer, &overlapped, &parms->m_amount);
}

bool System_File::WriteP(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    OVERLAPPED overlapped;
    overlapped.Offset = static_cast<DWORD>(parms->m_pos);
    overlapped.OffsetHigh = static_cast<DWORD>(parms->m_pos >> 32);
    overlapped.Internal = 0;
    overlapped.InternalHigh = 0;
    overlapped.hEvent = nullptr;

    return write_func(parms->m_stream, parms->m_buffer, &overlapped, &parms->m_amount);
}

bool System_File::Close(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    Blizzard::File::StreamRecord* stream = parms->m_stream;

    if (stream->m_fileHandle != INVALID_HANDLE_VALUE && stream->m_fileHandle) {
        close(HandleToFd(stream->m_fileHandle));
    }

    SMemFree(stream, __FILE__, __LINE__, 0);

    return true;
}

bool System_File::GetRootChars(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    const char* path = parms->m_path;
    parms->m_pos = path && (path[0] == '/' || path[0] == '\\') ? 1 : 0;

    return true;
}

void System_File::GetFileInfoByFile(Blizzard::File::StreamRecord* file) {
    if (file->m_infoValid) {
        return;
    }

    file->m_infoValid = false;

    struct stat st;

    if (fstat(HandleToFd(file->m_fileHandle), &st)) {
        return;
    }

    if (!file->m_streaming || !file->m_streaming->m_entry) {
        file->m_info.m_size = st.st_size;
    }

    FillFileInfo(&file->m_info, st, file->m_pathname);
    file->m_infoValid = true;
}

bool System_File::Exists(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    char path[1024];

    if (parms->m_path && strlen(parms->m_path) >= sizeof(path)) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    Blizzard::String::MakeUnivPath(parms->m_path, path, sizeof(path));

    struct stat st;

    if (stat(path, &st)) {
        parms->m_info->m_existence = 0;
    } else if (S_ISDIR(st.st_mode)) {
        parms->m_info->m_existence = 2;
    } else {
        parms->m_info->m_existence = 1;
    }

    return true;
}

bool System_File::MakeAbsolutePath(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    char buffer[1024];
    const char* path = parms->m_path;

    if (path[0] == '/') {
        if (strlen(path) >= sizeof(buffer)) {
            Blizzard::File::SetLastError(8);
            return false;
        }

        Blizzard::String::Copy(buffer, path, sizeof(buffer));
    } else {
        if (!getcwd(buffer, sizeof(buffer))) {
            if (errno == ERANGE) {
                Blizzard::File::SetLastError(8);
                return false;
            }

            buffer[0] = '\0';
        }

        if (strlen(buffer) + 1 + strlen(path) >= sizeof(buffer)) {
            Blizzard::File::SetLastError(8);
            return false;
        }

        Blizzard::String::Append(buffer, "/", sizeof(buffer));
        Blizzard::String::Append(buffer, path, sizeof(buffer));
    }

    FromNativeName(parms->m_cwdbuf, parms->m_cwdbuflen, buffer);
    Blizzard::String::MakeUnivPath(parms->m_cwdbuf, parms->m_cwdbuf, parms->m_cwdbuflen);

    if (path[(path ? strlen(path) : 0) - 1] == '/') {
        Blizzard::String::ForceTrailingSeparator(parms->m_cwdbuf, parms->m_cwdbuflen, '/');
    }

    return true;
}

bool System_File::Move(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    char src[1024];
    char dst[1024];

    if ((parms->m_path && strlen(parms->m_path) >= sizeof(src)) || (parms->m_newpath && strlen(parms->m_newpath) >= sizeof(dst))) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    Blizzard::String::MakeUnivPath(parms->m_path, src, sizeof(src));
    Blizzard::String::MakeUnivPath(parms->m_newpath, dst, sizeof(dst));

    struct stat st;

    if (!stat(dst, &st)) {
        return false;
    }

    return rename(src, dst) == 0;
}

bool System_File::Delete(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    char path[1024];

    if (parms->m_path && strlen(parms->m_path) >= sizeof(path)) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    Blizzard::String::MakeUnivPath(parms->m_path, path, sizeof(path));

    return unlink(path) == 0;
}

bool System_File::read_overlapped(Blizzard::File::StreamRecord* file, void* buffer, OVERLAPPED* overlapped, int32_t* count) {
    if (file->m_fileHandle == INVALID_HANDLE_VALUE) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    if (!*count) {
        return true;
    }

    int fd = HandleToFd(file->m_fileHandle);
    ssize_t bytesRead;

    if (overlapped) {
        bytesRead = pread(fd, buffer, *count, (static_cast<int64_t>(overlapped->OffsetHigh) << 32) | overlapped->Offset);
    } else {
        bytesRead = read(fd, buffer, *count);
    }

    if (bytesRead >= 0) {
        *count = static_cast<int32_t>(bytesRead);
        return true;
    }

    *count = 0;
    Blizzard::File::SetLastError(5);
    Blizzard::File::AddToLastErrorStack(5, Blizzard::String::QuickFormat<1024>("Linux Read - %s", file->m_pathname).m_buffer, 1);

    return false;
}

bool System_File::write_overlapped(Blizzard::File::StreamRecord* file, void* buffer, OVERLAPPED* overlapped, int32_t* count) {
    if (file->m_fileHandle == INVALID_HANDLE_VALUE) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    if (!*count) {
        return true;
    }

    file->m_infoValid = false;

    int fd = HandleToFd(file->m_fileHandle);
    ssize_t bytesWritten;

    if (overlapped) {
        bytesWritten = pwrite(fd, buffer, *count, (static_cast<int64_t>(overlapped->OffsetHigh) << 32) | overlapped->Offset);
    } else {
        bytesWritten = write(fd, buffer, *count);
    }

    if (bytesWritten >= 0) {
        *count = static_cast<int32_t>(bytesWritten);
        return true;
    }

    *count = 0;
    Blizzard::File::SetLastError(6);
    Blizzard::File::AddToLastErrorStack(6, Blizzard::String::QuickFormat<1024>("Linux Write - %s", file->m_pathname).m_buffer, 1);

    return false;
}

bool System_File::ToCreateFlags(uint32_t flags, DWORD* sharemode, DWORD* desiredaccess, DWORD* creationdisposition, DWORD* flagsandattributes) {
    *sharemode = (flags >> 2) & 3;
    *desiredaccess = ((flags * 4) | (flags & 2)) << 29;
    *creationdisposition = 0;

    if ((flags & 0x100) || (flags & 0x400)) {
        *creationdisposition = (flags & 0x800) ? NATIVE_CREATE_NEW : NATIVE_CREATE_ALWAYS;
    } else if (flags & 0x800) {
        *creationdisposition = NATIVE_CREATE_NEW;
    } else {
        *creationdisposition = NATIVE_OPEN_ALWAYS - ((flags & 0x1000) != 0);
    }

    *flagsandattributes = (2 * ((flags & 0x80) | 0x40)) | ((flags & 0x40) ? (NATIVE_FLAG_WRITE_THROUGH | NATIVE_FLAG_NO_BUFFERING) : 0);

    return true;
}

bool System_File::SetWorkingDirectory(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    if (!parms->m_path) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    return chdir(parms->m_path) == 0;
}

bool System_File::Create(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    Blizzard::File::SetLastError(9);

    return false;
}

bool System_File::GetWorkingDirectory(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    if (!parms->m_cwdbuf || parms->m_cwdbuflen <= 0) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    *parms->m_cwdbuf = '\0';
    char buffer[1024];

    if (!getcwd(buffer, sizeof(buffer))) {
        if (errno == ERANGE) {
            Blizzard::File::SetLastError(8);
            return false;
        }

        buffer[0] = '\0';
    }

    if (strlen(buffer) >= static_cast<size_t>(parms->m_cwdbuflen)) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    FromNativeName(parms->m_cwdbuf, parms->m_cwdbuflen, buffer);

    return true;
}

bool System_File::ProcessDirFast(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    void* cookie = parms->m_cookie;
    const char* path = parms->m_path;
    Blizzard::File::ProcessDirCallback callback = parms->m_dircallback;

    char nativePath[1024];

    if (path && strlen(path) >= sizeof(nativePath)) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    Blizzard::String::MakeUnivPath(path, nativePath, sizeof(nativePath));
    DIR* dir = opendir(nativePath);

    if (!dir) {
        Blizzard::Debug::Printf(Blizzard::Debug::StderrOutput, "failed to open %s\n", path);
        Blizzard::File::SetLastError(8);
        return false;
    }

    Blizzard::File::ProcessDirParms dirParms;
    dirParms.m_pathRoot = path;
    dirParms.m_cookie = cookie;

    while (dirent* entry = readdir(dir)) {
        const char* name = entry->d_name;

        if (name[0] == '.' && (!name[1] || (name[1] == '.' && !name[2]))) {
            continue;
        }

        bool isDir = entry->d_type == DT_DIR;

        if (entry->d_type == DT_UNKNOWN || entry->d_type == DT_LNK) {
            struct stat st;
            isDir = !fstatat(dirfd(dir), name, &st, 0) && S_ISDIR(st.st_mode);
        }

        char filename[1024];
        FromNativeName(filename, sizeof(filename), name);
        dirParms.m_filename = filename;
        dirParms.m_isDir = isDir;

        if (!callback(dirParms)) {
            closedir(dir);
            return false;
        }
    }

    closedir(dir);

    return true;
}

bool System_File::Flush(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    if (parms->m_stream->m_fileHandle == INVALID_HANDLE_VALUE) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    return fsync(HandleToFd(parms->m_stream->m_fileHandle)) == 0;
}

bool System_File::GetFileInfo(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    if (parms->m_path) {
        if (strlen(parms->m_path) >= 1024) {
            Blizzard::File::SetLastError(8);
            return false;
        }

        struct stat st;

        if (stat(parms->m_path, &st)) {
            Blizzard::File::SetLastError(8);
            return false;
        }

        parms->m_info->m_size = st.st_size;
        parms->m_info->m_path = nullptr;
        FillFileInfo(parms->m_info, st, parms->m_path);

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

bool System_File::GetFreeSpace(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    const char* path = parms->m_path;

    if (path && *path) {
        size_t prefix = static_cast<size_t>(Blizzard::String::FindFilename(path) - path);

        if (prefix >= MAX_PATH) {
            Blizzard::File::SetLastError(8);
            return false;
        }

        char directory[MAX_PATH];
        Blizzard::String::Copy(directory, path, static_cast<uint32_t>(prefix + 1));

        struct statvfs info;

        if (!statvfs(directory[0] ? directory : ".", &info)) {
            parms->m_pos = static_cast<int64_t>(info.f_bavail) * info.f_frsize;
            return true;
        }
    }

    Blizzard::File::SetLastError(8);

    return false;
}

bool System_File::GetPos(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    Blizzard::File::StreamRecord* stream = parms->m_stream;
    off_t pos;

    if (stream->m_fileHandle == INVALID_HANDLE_VALUE || (pos = lseek(HandleToFd(stream->m_fileHandle), 0, SEEK_CUR)) < 0) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    parms->m_pos = pos;

    return true;
}

bool System_File::IsAbsolutePath(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    if (parms->m_path[0] == '/') {
        return true;
    }

    Blizzard::File::SetLastError(8);

    return false;
}

bool System_File::IsReadOnly(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    struct stat st;

    if (!stat(parms->m_path, &st)) {
        return access(parms->m_path, W_OK) != 0;
    }

    Blizzard::File::SetLastError(8);

    return false;
}

static bool MakeDirectory(const char* path, mode_t mode) {
    if (!mkdir(path, mode)) {
        return true;
    }

    struct stat st;

    return errno == EEXIST && !stat(path, &st) && S_ISDIR(st.st_mode);
}

bool System_File::CreateDirectory(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    const char* path = parms->m_path;

    if (!path) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    mode_t mode = parms->m_grantEveryone ? 0777 : 0755;
    Blizzard::Memory::TempBuffer<char, MAX_PATH> buffer(static_cast<uint32_t>(strlen(path)) + 1);
    Blizzard::String::MakeUnivPath(path, buffer.m_data, buffer.m_count);

    if (parms->m_recursive) {
        for (char* separator = strchr(buffer.m_data + 1, '/'); separator; separator = strchr(separator + 1, '/')) {
            *separator = '\0';
            bool created = MakeDirectory(buffer.m_data, mode);
            *separator = '/';

            if (!created) {
                Blizzard::File::SetLastError(8);
                return false;
            }
        }
    }

    size_t length = strlen(buffer.m_data);

    if (length > 1 && buffer.m_data[length - 1] == '/') {
        buffer.m_data[length - 1] = '\0';
    }

    if (MakeDirectory(buffer.m_data, mode)) {
        return true;
    }

    Blizzard::File::SetLastError(8);

    return false;
}

bool System_File::Copy(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    bool overwrite = parms->m_overwrite;
    char src[1024];
    char dst[1024];

    if ((parms->m_path && strlen(parms->m_path) >= sizeof(src)) || (parms->m_newpath && strlen(parms->m_newpath) >= sizeof(dst))) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    Blizzard::String::MakeUnivPath(parms->m_path, src, sizeof(src));
    Blizzard::String::MakeUnivPath(parms->m_newpath, dst, sizeof(dst));

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

    while (offset < size) {
        int32_t count = size - offset >= 0xA00000 ? 0xA00000 : static_cast<int32_t>(size - offset);

        if (!Blizzard::File::Read(input, buffer, offset, &count) || !Blizzard::File::Write(output, buffer, offset, &count)) {
            result = false;
            break;
        }

        offset += count;
    }

    Blizzard::Memory::Free(buffer);
    Blizzard::File::Close(input);
    Blizzard::File::Close(output);

    if (!result) {
        Blizzard::File::Delete(dst);
    }

    return result;
}

bool System_File::Open(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    int32_t mode = parms->m_mode;
    char path[1024];

    if (parms->m_path && strlen(parms->m_path) >= sizeof(path)) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    Blizzard::String::MakeUnivPath(parms->m_path, path, sizeof(path));

    int fd = OpenNative(path, mode);

    if (fd < 0) {
        Blizzard::File::SetLastError(4);
        Blizzard::File::AddToLastErrorStack(4, Blizzard::String::QuickFormat<1024>("Linux Open - %s", path).m_buffer, 1);
        return false;
    }

    Blizzard::File::StreamRecord* stream = parms->m_stream;

    if (!stream) {
        uint32_t length = static_cast<uint32_t>(strlen(path));
        stream = static_cast<Blizzard::File::StreamRecord*>(SMemAlloc(sizeof(Blizzard::File::StreamRecord) + length + 1, __FILE__, __LINE__, SMEM_FLAG_ZEROMEMORY));
        if (!stream) {
            close(fd);
            return false;
        }
        stream->m_pathname = reinterpret_cast<char*>(stream + 1);
        Blizzard::String::Copy(stream->m_pathname, path, length + 1);
    }

    stream->m_fileHandle = FdToHandle(fd);
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

bool System_File::Read(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    return read_overlapped(parms->m_stream, parms->m_buffer, nullptr, &parms->m_amount);
}

bool System_File::RemoveDirectory(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    const char* path = parms->m_path;

    if (path && strlen(path) >= 1024) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    if (parms->m_recursive) {
        return Blizzard::File::RemoveDirectoryAndContents(path, false);
    }

    if (!path) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    Blizzard::Memory::TempBuffer<char, MAX_PATH> buffer(static_cast<uint32_t>(strlen(path)) + 1);
    Blizzard::String::MakeUnivPath(path, buffer.m_data, buffer.m_count);

    return rmdir(buffer.m_data) == 0;
}

bool System_File::SetCacheMode(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    Blizzard::File::StreamRecord* stream = parms->m_stream;
    stream->m_mode = parms->m_mode | (stream->m_mode & ~0x40);

    close(HandleToFd(stream->m_fileHandle));
    stream->m_fileHandle = FdToHandle(OpenNative(stream->m_info.m_path, stream->m_mode));

    if (stream->m_fileHandle == INVALID_HANDLE_VALUE) {
        Blizzard::File::SetLastError(4);
        Blizzard::File::AddToLastErrorStack(4, "Linux Close", 1);
        return false;
    }

    return true;
}

bool System_File::SetEOF(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    Blizzard::File::StreamRecord* stream = parms->m_stream;
    int64_t offset;

    if (!Blizzard::File::GetPos(stream, &offset) || !Blizzard::File::SetPos(stream, parms->m_pos, parms->m_posMode)) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    int fd = HandleToFd(stream->m_fileHandle);
    bool result = ftruncate(fd, lseek(fd, 0, SEEK_CUR)) == 0;
    Blizzard::File::SetPos(stream, offset, 0);

    return result;
}

bool System_File::SetAttributes(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    if (parms->m_has.m_all & 0x10) {
        Blizzard::Time::Timestamp time = parms->m_info->m_modTime;
        int64_t seconds = time / 1000000000LL;
        int64_t nanoseconds = time % 1000000000LL;

        if (nanoseconds < 0) {
            nanoseconds += 1000000000LL;
            seconds--;
        }

        timespec times[2];
        times[0].tv_sec = 0;
        times[0].tv_nsec = UTIME_OMIT;
        times[1].tv_sec = static_cast<time_t>(seconds + 946684800LL);
        times[1].tv_nsec = static_cast<long>(nanoseconds);

        if (futimens(HandleToFd(parms->m_stream->m_fileHandle), times)) {
            Blizzard::File::SetLastError(8);
            return false;
        }

        parms->m_stream->m_info.m_modTime = time;
        parms->m_has.m_all &= ~0x10u;
    }

    if (parms->m_has.m_all & 0x4) {
        struct stat st;

        if (stat(parms->m_path, &st) || chmod(parms->m_path, (st.st_mode & ~0777) | ToNativeAttributes(parms->m_info->m_attributes))) {
            Blizzard::File::SetLastError(8);
            return false;
        }

        parms->m_has.m_all &= ~0x4u;
    }

    return parms->m_has.m_all == 0;
}

bool System_File::SetPos(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    Blizzard::File::StreamRecord* stream = parms->m_stream;
    int32_t method = parms->m_posMode;

    if (stream->m_fileHandle == INVALID_HANDLE_VALUE) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    int whence = method == 0 ? SEEK_SET : (method == 1 ? SEEK_CUR : SEEK_END);

    return lseek(HandleToFd(stream->m_fileHandle), parms->m_pos, whence) >= 0;
}

bool System_File::Write(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    return write_overlapped(parms->m_stream, parms->m_buffer, nullptr, &parms->m_amount);
}

bool System_File::read_locked(Blizzard::File::StreamRecord* file, void* buffer, OVERLAPPED* overlapped, int32_t* count) {
    Blizzard::Lock::ScopedMutex lock(*s_ioLock);

    if (!Blizzard::File::SetPos(file, (static_cast<int64_t>(overlapped->OffsetHigh) << 32) | overlapped->Offset, 0)) {
        *count = 0;
        Blizzard::File::SetLastError(9);
        return false;
    }

    return read_overlapped(file, buffer, nullptr, count);
}

bool System_File::write_locked(Blizzard::File::StreamRecord* file, void* buffer, OVERLAPPED* overlapped, int32_t* count) {
    Blizzard::Lock::ScopedMutex lock(*s_ioLock);

    if (!Blizzard::File::SetPos(file, (static_cast<int64_t>(overlapped->OffsetHigh) << 32) | overlapped->Offset, 0)) {
        *count = 0;
        Blizzard::File::SetLastError(9);
        return false;
    }

    return write_overlapped(file, buffer, nullptr, count);
}

void System_File::detect_io_mode() {
    read_func = read_overlapped;
    write_func = write_overlapped;
}

bool System_File::read_first(Blizzard::File::StreamRecord* file, void* buffer, OVERLAPPED* overlapped, int32_t* count) {
    if (!s_read_init) {
        if (s_readInitCount.Increment() == 1) {
            detect_io_mode();
            s_read_init = true;
        } else {
            do {
                usleep(1000);
            } while (!s_read_init);
        }
    }

    return read_func(file, buffer, overlapped, count);
}

bool System_File::write_first(Blizzard::File::StreamRecord* file, void* buffer, OVERLAPPED* overlapped, int32_t* count) {
    if (!s_write_init) {
        if (s_writeInitCount.Increment() == 1) {
            detect_io_mode();
            s_write_init = true;
        } else {
            do {
                usleep(1000);
            } while (!s_write_init);
        }
    }

    return write_func(file, buffer, overlapped, count);
}

void System_File::FromNativeName(char* buffer, uint32_t buffersize, const char* str) {
    Blizzard::String::Copy(buffer, str ? str : "", buffersize);
}

#endif
