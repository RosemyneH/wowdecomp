#if defined(__linux__) || defined(__APPLE__)

#include <cstdint>
#include <fcntl.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include "Engine/Source/BlizzardCore/Source/Packages/iounit/IOFileUnit.h"
#include "Engine/Source/BlizzardCore/Source/System/File/File.h"
#include "Engine/Source/BlizzardCore/Source/System/Memory/MemoryStorm.h"
#include "Engine/Source/BlizzardCore/Source/System/String/String.h"
#include "Engine/Source/Storm/H/storm.h"

bool IOFileUnit::ReadNative(uint32_t offset, const char* path, void* buffer, uint32_t count, uint32_t* bytesRead) {
    int file = open(path, O_RDONLY | O_CLOEXEC);

    if (file < 0) {
        return false;
    }

    uint32_t head = offset & 0xFFF;
    uint32_t size = (head + count + 0xFFF) & ~0xFFFu;
    bool result = false;
    uint8_t* data = static_cast<uint8_t*>(SMemAlloc(size, __FILE__, __LINE__, 0));

    if (!data && size) {
        close(file);
        return false;
    }

    ssize_t read = pread(file, data, size, offset - head);

    if (read >= 0) {
        uint32_t available = static_cast<uint32_t>(read) > head ? static_cast<uint32_t>(read) - head : 0;
        uint32_t amount = available >= count ? count : available;
        Blizzard::String::MemCopy(buffer, data + head, amount);

        if (bytesRead) {
            *bytesRead = amount;
        }

        result = true;
    }

    SMemFree(data, __FILE__, __LINE__, 0);
    close(file);

    return result;
}

IOUnbufferedFileUnit::IOUnbufferedFileUnit(Blizzard::File::StreamRecord* stream, bool ownsMemory) : IOFileUnit(stream, ownsMemory) {
    Blizzard::File::FileInfo* info = Blizzard::File::GetFileInfo(stream);
    this->m_size = info->m_size;

    struct statvfs fs;

    if (statvfs(info->m_path, &fs) || !fs.f_bsize || ((fs.f_bsize - 1) & fs.f_bsize)) {
        this->m_alignMask = 0xFFFF;
    } else {
        this->m_alignMask = static_cast<uint32_t>(fs.f_bsize - 1);
    }

    this->m_buffer = Blizzard::Memory::Allocate(this->m_alignMask + 0x100000);
    this->m_alignedBuffer = reinterpret_cast<void*>(~static_cast<uintptr_t>(this->m_alignMask) & (reinterpret_cast<uintptr_t>(this->m_buffer) + this->m_alignMask));
}

#endif
