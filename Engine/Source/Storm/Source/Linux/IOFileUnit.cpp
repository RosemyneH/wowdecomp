#if defined(__linux__) || defined(__APPLE__)

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include "Engine/Source/Storm/Source/IOUnit/IOFileUnit.h"
#include "Engine/Source/Storm/H/storm.h"

#define IO_GENERIC_READ 0x80000000
#define IO_GENERIC_WRITE 0x40000000
#define IO_CREATE_NEW 1
#define IO_CREATE_ALWAYS 2
#define IO_OPEN_EXISTING 3
#define IO_OPEN_ALWAYS 4
#define IO_TRUNCATE_EXISTING 5
#define IO_FLAG_NO_BUFFERING 0x20000000

namespace Storm {

static int HandleToFd(void* handle) {
    return static_cast<int>(reinterpret_cast<intptr_t>(handle) - 1);
}

static void* FdToHandle(int fd) {
    return reinterpret_cast<void*>(static_cast<intptr_t>(fd) + 1);
}

static void* OpenFileHandle(const IOFileParam* param) {
    int flags;

    if ((param->m_access & IO_GENERIC_READ) && (param->m_access & IO_GENERIC_WRITE)) {
        flags = O_RDWR;
    } else if (param->m_access & IO_GENERIC_WRITE) {
        flags = O_WRONLY;
    } else {
        flags = O_RDONLY;
    }

    switch (param->m_creation) {
        case IO_CREATE_NEW:
            flags |= O_CREAT | O_EXCL;
            break;
        case IO_CREATE_ALWAYS:
            flags |= O_CREAT | O_TRUNC;
            break;
        case IO_OPEN_ALWAYS:
            flags |= O_CREAT;
            break;
        case IO_TRUNCATE_EXISTING:
            flags |= O_TRUNC;
            break;
        default:
            break;
    }

    int fd = open(param->m_name, flags | O_CLOEXEC, 0644);

    return fd >= 0 ? FdToHandle(fd) : nullptr;
}

IOUnit::IOUnit() {
    this->m_refcount = nullptr;
    this->m_count = 1;
}

void IOUnit::Release() {
    IORefcount* refcount = this->m_refcount;

    if (--this->m_count == 0) {
        delete this;
    }

    if (refcount) {
        refcount->Release();
    }
}

IOWin32FileUnit::IOWin32FileUnit(void* handle) {
    this->m_handle = handle;
    this->m_position = -1;
    this->Seek(0);
}

bool IOWin32FileUnit::Seek(int64_t offset) {
    if (this->m_position != offset) {
        if (lseek(HandleToFd(this->m_handle), offset, SEEK_SET) < 0) {
            this->m_position = -1;
            return false;
        }

        this->m_position = offset;
    }

    return true;
}

bool IOWin32FileUnit::Read(void* buffer, int64_t offset, uint32_t count, uint32_t* bytesRead) {
    uint32_t minimum = 1;
    uint32_t localRead;

    if (!bytesRead) {
        minimum = count;
        bytesRead = &localRead;
    }

    *bytesRead = 0;

    for (int32_t tries = 0; tries < 3; tries++) {
        ssize_t result = pread(HandleToFd(this->m_handle), buffer, count, offset);

        if (result >= 0) {
            *bytesRead = static_cast<uint32_t>(result);
            this->m_position = -1;
            return *bytesRead >= minimum;
        }

        this->m_position = -1;
        usleep(10000);
    }

    return false;
}

bool IOWin32FileUnit::Write(const void* buffer, int64_t offset, uint32_t count) {
    ssize_t written = pwrite(HandleToFd(this->m_handle), buffer, count, offset);
    this->m_position = -1;

    return written == static_cast<ssize_t>(count);
}

bool IOWin32FileUnit::Flush() {
    return fsync(HandleToFd(this->m_handle)) == 0;
}

bool IOWin32FileUnit::GetFileSize(int64_t* size) {
    struct stat info;

    if (fstat(HandleToFd(this->m_handle), &info)) {
        *size = 0;
        return false;
    }

    *size = info.st_size;

    return true;
}

bool IOWin32FileUnit::SetFileSize(int64_t size) {
    return ftruncate(HandleToFd(this->m_handle), size) == 0;
}

void* IOWin32FileUnit::Win32Handle() {
    return this->m_handle;
}

bool IOUnbufferedFileUnit::Read(void* buffer, int64_t offset, uint32_t count, uint32_t* bytesRead) {
    if (!(this->m_alignMask & reinterpret_cast<uintptr_t>(buffer)) && !(this->m_alignMask & count)) {
        return this->m_unit->Read(buffer, offset, count, bytesRead);
    }

    if (!this->m_buffer) {
        return false;
    }

    uint32_t total = 0;
    bool result = true;

    while (count) {
        uint32_t chunk = count >= 0x100000 ? 0x100000 : count;
        uint32_t size = chunk;

        if (chunk & this->m_alignMask) {
            size = (this->m_alignMask + chunk) & ~this->m_alignMask;
        }

        uint32_t read;
        result = this->m_unit->Read(this->m_buffer, offset, size, &read);

        if (!result) {
            break;
        }

        read = size < chunk ? size : chunk;
        memcpy(buffer, this->m_buffer, read);
        buffer = static_cast<uint8_t*>(buffer) + read;
        offset += read;
        count -= read;
        total += read;

        if (read < chunk) {
            break;
        }
    }

    if (!bytesRead) {
        return count == 0;
    }

    *bytesRead = total;

    return result;
}

bool IOUnbufferedFileUnit::Write(const void* buffer, int64_t offset, uint32_t count) {
    if (!(this->m_alignMask & reinterpret_cast<uintptr_t>(buffer)) && !(this->m_alignMask & count)) {
        return this->m_unit->Write(buffer, offset, count);
    }

    if (!this->m_buffer) {
        return false;
    }

    bool result = true;

    while (count) {
        uint32_t chunk = count >= 0x100000 ? 0x100000 : count;
        memcpy(this->m_buffer, buffer, chunk);
        buffer = static_cast<const uint8_t*>(buffer) + chunk;

        uint32_t size = chunk;

        if (chunk & this->m_alignMask) {
            size = (this->m_alignMask + chunk) & ~this->m_alignMask;
        }

        result = this->m_unit->Write(this->m_buffer, offset, size);

        if (!result) {
            break;
        }

        offset += chunk;
        count -= chunk;
    }

    if (offset > this->m_size) {
        this->m_size = offset;
    }

    return result;
}

bool IOUnbufferedFileUnit::Flush() {
    return this->m_unit->Flush();
}

bool IOUnbufferedFileUnit::SetFileSize(int64_t size) {
    this->m_size = size;

    return this->m_unit->SetFileSize((size + this->m_alignMask) & ~this->m_alignMask);
}

bool IOUnbufferedFileUnit::GetFileSize(int64_t* size) {
    *size = this->m_size;

    return true;
}

IOUnit::~IOUnit() {
}

IOWin32FileUnit::~IOWin32FileUnit() {
    close(HandleToFd(this->m_handle));
    this->m_handle = nullptr;
}

IOUnbufferedFileUnit::IOUnbufferedFileUnit(void* handle, const IOFileParam* param) {
    this->m_unit = new IOWin32FileUnit(handle);
    this->m_size = 0;
    this->m_unit->GetFileSize(&this->m_size);
    SStrCopy(this->m_path, param->m_name, sizeof(this->m_path));
    this->m_buffer = aligned_alloc(0x10000, 0x100000);

    struct statvfs info;

    if (fstatvfs(HandleToFd(handle), &info) || !info.f_bsize || ((info.f_bsize - 1) & info.f_bsize)) {
        this->m_alignMask = 0xFFFF;
    } else {
        this->m_alignMask = static_cast<uint32_t>(info.f_bsize - 1);
    }
}

void* IOUnbufferedFileUnit::Win32Handle() {
    return this->m_unit->Win32Handle();
}

IOUnbufferedFileUnit* IOUnbufferedFileUnit::Open(const IOFileParam* param) {
    void* handle = OpenFileHandle(param);

    if (!handle) {
        return nullptr;
    }

    return new IOUnbufferedFileUnit(handle, param);
}

IOWin32FileUnit* IOWin32FileUnit::Open(const IOFileParam* param) {
    void* handle = OpenFileHandle(param);

    if (!handle) {
        return nullptr;
    }

    return new IOWin32FileUnit(handle);
}

IOUnit* IOFileUnit::Create(const IOFileParam& param, IOUnit* next) {
    if (next) {
        return nullptr;
    }

    if (param.m_flags & IO_FLAG_NO_BUFFERING) {
        return IOUnbufferedFileUnit::Open(&param);
    }

    return IOWin32FileUnit::Open(&param);
}

IOUnbufferedFileUnit::~IOUnbufferedFileUnit() {
    free(this->m_buffer);

    if (this->m_unit) {
        delete this->m_unit;
    }

    IOFileParam param;
    param.m_name = this->m_path;
    param.m_access = IO_GENERIC_WRITE;
    param.m_share = 0;
    param.m_creation = IO_OPEN_EXISTING;
    param.m_flags = 0;

    IOUnit* unit = IOWin32FileUnit::Open(&param);

    if (unit) {
        unit->SetFileSize(this->m_size);
        unit->Release();
    }
}

}

#endif
