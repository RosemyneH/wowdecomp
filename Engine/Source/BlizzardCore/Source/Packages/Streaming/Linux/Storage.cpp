#if defined(__linux__) || defined(__APPLE__)

#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>
#include "Engine/Source/BlizzardCore/Source/System/Lock/Lock.h"
#include "Engine/Source/BlizzardCore/Source/System/Process/Process.h"
#include "Engine/Source/BlizzardCore/Source/Packages/Streaming/Storage.h"
#include "Engine/Source/BlizzardCore/Source/Packages/Streaming/Streaming.h"
#include "Engine/Source/BlizzardCore/Source/System/File/File.h"
#include "Engine/Source/BlizzardCore/Source/System/File/Filestack.h"
#include "Engine/Source/BlizzardCore/Source/System/Log/Log.h"
#include "Engine/Source/BlizzardCore/Source/System/Memory/MemoryStorm.h"
#include "Engine/Source/BlizzardCore/Source/System/String/String.h"
#include "Engine/Source/BlizzardCore/Source/System/Thread/Thread.h"

bool Blizzard::Storage::s_usePartFiles = true;

int32_t Blizzard::Storage::s_blockSize = 0x4000;

std::string Blizzard::Storage::s_storagePath;

Blizzard::Lock::Mutex Blizzard::Storage::s_storageLock;

void* Blizzard::Storage::s_storageEvent;

std::map<std::string, Blizzard::Streaming::StorageEntry>* Blizzard::Storage::s_storage;

Blizzard::File::StreamRecord* Blizzard::Storage::s_storageFile;

bool Blizzard::Storage::s_storageReady;

bool Blizzard::Storage::s_storageDirty;

volatile bool Blizzard::Storage::s_storageStop;

volatile bool Blizzard::Storage::s_storageStopped;

std::deque<Blizzard::Streaming::WriteRequest> Blizzard::Storage::s_writeQueue;

int32_t Blizzard::Storage::GetBlockLength(Blizzard::Streaming::StorageEntry* entry, int32_t block) {
    int32_t blockSize = entry->m_blockSize;

    if (entry->m_size <= (block + 1LL) * blockSize) {
        return static_cast<int32_t>(entry->m_size - static_cast<int64_t>(block) * blockSize);
    }

    return entry->m_blockSize;
}

int32_t Blizzard::Storage::CountRanges(Blizzard::Streaming::StorageEntry* entry, int64_t offset, int64_t size) {
    return static_cast<int32_t>((size + offset) / entry->m_blockSize - offset / entry->m_blockSize + 4);
}

bool Blizzard::Storage::ReadAt(Blizzard::File::Filesystem* fs, Blizzard::File::StreamRecord* file, void* buffer, int64_t offset, int32_t size) {
    return Blizzard::File::Read(fs, file, buffer, offset, &size);
}

bool Blizzard::Storage::WriteAt(Blizzard::File::StreamRecord* file, const void* buffer, int64_t offset, int32_t size) {
    return Blizzard::File::Write(file, buffer, offset, &size);
}

bool Blizzard::Storage::IsComplete(Blizzard::Streaming::StorageEntry* entry) {
    for (int32_t i = 0; i < static_cast<int32_t>(entry->m_blocks.size()); i++) {
        if (entry->m_blocks[i].m_state != 3) {
            return false;
        }
    }

    return true;
}

int32_t Blizzard::Storage::BuildRanges(Blizzard::Streaming::StorageEntry* entry, int64_t offset, int32_t size, Blizzard::Streaming::LoadRange* ranges) {
    int64_t position = offset;
    uint32_t block = static_cast<uint32_t>(offset / entry->m_blockSize);
    int32_t count = 0;

    for (int32_t phase = 1; size && phase <= 3; phase++) {
        if (phase == 2) {
            int32_t full = entry->m_blockSize * (size / entry->m_blockSize);

            while (full >= entry->m_blockSize) {
                int32_t state = entry->m_blocks[block].m_state;
                Blizzard::Streaming::LoadRange& range = ranges[count];

                switch (state) {
                    case 0:
                    case 1:
                        count++;
                        range.m_offset = position;
                        range.m_blockOffset = position;
                        range.m_size = entry->m_blockSize;
                        range.m_blockSize = entry->m_blockSize;
                        range.m_state = 3 - (state != 1);
                        break;

                    case 2:
                    case 3:
                    case 4:
                        count++;
                        range.m_size = entry->m_blockSize;
                        range.m_offset = static_cast<int64_t>(block) * entry->m_blockSize;
                        range.m_blockOffset = 0;
                        range.m_blockSize = 0;
                        range.m_state = state == 4;
                        break;
                }

                block++;
                size -= entry->m_blockSize;
                position += entry->m_blockSize;
                full -= entry->m_blockSize;
            }

            continue;
        }

        int64_t blockStart = static_cast<int64_t>(block) * entry->m_blockSize;

        if (phase == 1 && position == blockStart) {
            continue;
        }

        int32_t inner = static_cast<int32_t>(position - blockStart);
        int32_t length = GetBlockLength(entry, block);
        int32_t state = entry->m_blocks[block].m_state;
        int32_t available = length - inner;
        int32_t take = size >= available ? available : size;

        switch (state) {
            case 0:
            case 1: {
                Blizzard::Streaming::LoadRange& range = ranges[count++];
                size -= take;
                range.m_offset = position;
                range.m_blockOffset = blockStart;
                range.m_size = take;
                range.m_blockSize = length;
                range.m_state = 3 - (state != 1);
                break;
            }

            case 2:
            case 3:
            case 4: {
                Blizzard::Streaming::LoadRange& range = ranges[count++];
                range.m_offset = inner + entry->m_blockSize * static_cast<int64_t>(block);
                size -= take;
                range.m_size = take;
                range.m_blockOffset = 0;
                range.m_blockSize = 0;
                range.m_state = state == 4;
                break;
            }
        }

        block++;
        position += entry->m_blockSize - inner;
    }

    return count;
}

int32_t Blizzard::Storage::BuildRangesFromStore(Blizzard::Streaming::StorageEntry* entry, int64_t offset, int32_t size, Blizzard::Streaming::LoadRange* ranges) {
    int64_t start = entry->m_storeSize >= offset ? offset : entry->m_storeSize;
    int32_t count = 0;

    if (start / entry->m_blockSize < offset / entry->m_blockSize) {
        count = BuildRanges(entry, entry->m_storeSize, static_cast<int32_t>(offset / entry->m_blockSize * entry->m_blockSize - entry->m_storeSize), ranges);

        for (int32_t i = 0; i < count; i++) {
            ranges[i].m_state = 4;
        }
    }

    return count + BuildRanges(entry, offset, size, ranges + count);
}

int32_t Blizzard::Storage::BuildRangesWithHeader(Blizzard::Streaming::StorageEntry* entry, int64_t offset, int32_t size, Blizzard::Streaming::LoadRange* ranges) {
    if (entry->m_blocks[0].m_state == 3) {
        return BuildRanges(entry, offset, size, ranges);
    }

    if (entry->m_blocks[0].m_state == 2) {
        return BuildRanges(entry, offset, size, ranges);
    }

    int32_t total = static_cast<int32_t>(entry->m_size / entry->m_blockSize + 1);
    int32_t first = static_cast<int32_t>(offset / entry->m_blockSize);
    int32_t head = 0;

    if (first > 0) {
        head = first;

        for (int32_t i = 0; i < first; i++) {
            Blizzard::Streaming::LoadRange& range = ranges[i];
            int32_t length = GetBlockLength(entry, i);
            range.m_offset = 0;
            range.m_size = 0;
            range.m_blockOffset = static_cast<int64_t>(i) * entry->m_blockSize;
            range.m_blockSize = length;
            range.m_state = 4;
        }
    }

    int32_t count = BuildRanges(entry, offset, size, ranges + head) + head;
    int32_t next = static_cast<int32_t>((offset + size - 1) / entry->m_blockSize + 1);

    if (next < total) {
        Blizzard::Streaming::LoadRange* range = &ranges[count];
        count += total - next;

        for (; next < total; next++, range++) {
            int64_t end = (next + 1LL) * entry->m_blockSize;
            int32_t length = entry->m_size <= end ? static_cast<int32_t>(entry->m_size - static_cast<int64_t>(next) * entry->m_blockSize) : entry->m_blockSize;

            range->m_offset = 0;
            range->m_size = 0;
            range->m_blockOffset = static_cast<int64_t>(next) * entry->m_blockSize;
            range->m_blockSize = length;
            range->m_state = 4;
        }
    }

    return count;
}

bool Blizzard::Storage::RequestRange(Blizzard::Streaming::StorageEntry* entry, int64_t offset, int32_t size, int32_t state) {
    Blizzard::Lock::MutexEnter(s_storageLock);

    if (entry->m_closed) {
        Blizzard::Lock::MutexLeave(s_storageLock);
        return false;
    }

    int64_t blockSize = entry->m_blockSize;
    int32_t last = static_cast<int32_t>((offset + size - 1) / blockSize);

    for (uint32_t block = static_cast<uint32_t>(offset / blockSize); static_cast<int32_t>(block) <= last; block++) {
        Blizzard::Streaming::StorageBlock& storageBlock = entry->m_blocks[block];

        if (state != 1 || (storageBlock.m_state != 2 && storageBlock.m_state != 3)) {
            storageBlock.m_state = state;
        }
    }

    Blizzard::Lock::MutexLeave(s_storageLock);
    return true;
}

void Blizzard::Storage::SetRangeAs(Blizzard::Streaming::StorageEntry* entry, int64_t offset, int32_t size, int32_t state) {
    int32_t first = static_cast<int32_t>(offset / entry->m_blockSize);
    int32_t last = size <= 0 ? first : static_cast<int32_t>((offset + size - 1) / entry->m_blockSize);

    Blizzard::Lock::MutexEnter(s_storageLock);

    for (uint32_t block = first; static_cast<int32_t>(block) <= last; block++) {
        entry->m_blocks[block].m_state = state;
    }

    Blizzard::Lock::MutexLeave(s_storageLock);
}

Blizzard::Storage::StorageEntryRef::StorageEntryRef(Blizzard::Streaming::StorageEntry* entry) {
    Blizzard::Lock::MutexEnter(s_storageLock);

    if (!entry || entry->m_released) {
        Blizzard::Lock::MutexLeave(s_storageLock);
        this->m_entry = nullptr;
        return;
    }

    entry->m_refCount++;
    Blizzard::Lock::MutexLeave(s_storageLock);
    this->m_entry = entry;
}

bool Blizzard::Storage::WriteIndexEntry(Blizzard::Streaming::StorageEntry* entry, int32_t block, bool header) {
    if (!s_storageReady) {
        return false;
    }

    if (entry->m_flags & 4) {
        return true;
    }

    if (!header) {
        int32_t amount = 8;
        Blizzard::File::Write(s_storageFile, &entry->m_storeSize, entry->m_headerOffset + 305, &amount);

        if (block >= 0 && block < static_cast<int32_t>(entry->m_blocks.size())) {
            Blizzard::Streaming::StorageBlockRecord record;
            record.m_state = entry->m_blocks[block].m_state;
            record.m_offset = entry->m_blocks[block].m_offset;
            record.m_storeOffset = entry->m_blocks[block].m_storeOffset;
            record.m_size = entry->m_blocks[block].m_size;

            WriteAt(s_storageFile, &record, entry->m_headerOffset + 24LL * block + 321, 24);
        }

        return true;
    }

    int32_t version = 2;
    Blizzard::File::SetPos(s_storageFile, 0, 0);
    int32_t count = 4;
    Blizzard::File::Write(s_storageFile, &version, &count);

    int32_t entries = static_cast<int32_t>(s_storage->size());
    count = 4;
    Blizzard::File::Write(s_storageFile, &entries, &count);

    Blizzard::Streaming::StorageHeader storageHeader;
    storageHeader.m_flags = entry->m_flags;
    storageHeader.m_storeSize = entry->m_storeSize;
    Blizzard::String::Copy(storageHeader.m_path, entry->m_path.c_str(), sizeof(storageHeader.m_path));
    Blizzard::String::Copy(storageHeader.m_version, entry->m_version.c_str(), sizeof(storageHeader.m_version));
    storageHeader.m_closed = entry->m_closed;
    storageHeader.m_size = entry->m_size;
    storageHeader.m_blockSize = entry->m_blockSize;
    storageHeader.m_blockCount = static_cast<int32_t>(entry->m_blocks.size());

    int32_t amount = sizeof(storageHeader);
    Blizzard::File::Write(s_storageFile, &storageHeader, entry->m_headerOffset, &amount);

    amount = 24 * storageHeader.m_blockCount;
    Blizzard::File::Write(s_storageFile, &entry->m_blocks[0], entry->m_headerOffset + 321, &amount);

    return true;
}

Blizzard::Streaming::LoadRange* Blizzard::Storage::GetRange(Blizzard::Streaming::StorageEntry* entry, int64_t offset, int32_t size, int32_t* count, bool lock) {
    int64_t zero = 0;
    int64_t start = offset >= 0 ? offset : zero;
    start = entry->m_size >= start ? start : entry->m_size;

    int64_t end = start + size;
    zero = 0;
    end = end >= 0 ? end : zero;
    end = entry->m_size >= end ? end : entry->m_size;

    int32_t length = static_cast<int32_t>(end - start);

    if (end == start) {
        *count = 0;
        return nullptr;
    }

    if (lock) {
        Blizzard::Lock::MutexEnter(s_storageLock);
    }

    Blizzard::Streaming::LoadRange* ranges;

    if (entry->m_flags & 2) {
        ranges = static_cast<Blizzard::Streaming::LoadRange*>(Blizzard::Memory::Allocate(sizeof(Blizzard::Streaming::LoadRange) * CountRanges(entry, 0, entry->m_size)));
        *count = BuildRangesWithHeader(entry, start, length, ranges);
    } else if (entry->m_flags & 1) {
        zero = 0;
        int64_t from = entry->m_storeSize >= 0 ? entry->m_storeSize : zero;
        from = start >= from ? from : start;
        ranges = static_cast<Blizzard::Streaming::LoadRange*>(Blizzard::Memory::Allocate(sizeof(Blizzard::Streaming::LoadRange) * CountRanges(entry, from, start + length - from)));
        *count = BuildRangesFromStore(entry, start, length, ranges);
    } else {
        ranges = static_cast<Blizzard::Streaming::LoadRange*>(Blizzard::Memory::Allocate(sizeof(Blizzard::Streaming::LoadRange) * CountRanges(entry, start, length)));
        *count = BuildRanges(entry, start, length, ranges);
    }

    if (lock) {
        Blizzard::Lock::MutexLeave(s_storageLock);
    }

    return ranges;
}

int64_t Blizzard::Storage::GetNextHeaderOffset() {
    int64_t offset = 8;

    for (std::map<std::string, Blizzard::Streaming::StorageEntry>::iterator it = s_storage->begin(); it != s_storage->end(); ++it) {
        if (it->second.m_flags & 4) {
            continue;
        }

        if (offset <= it->second.m_headerOffset) {
            offset = it->second.m_headerOffset + static_cast<int64_t>(24 * static_cast<int32_t>(it->second.m_blocks.size())) + 321;
        }
    }

    return offset;
}

bool Blizzard::Storage::ReadLocal(Blizzard::File::Filesystem* fs, Blizzard::File::StreamRecord* file, Blizzard::Streaming::StorageEntry* entry, uint8_t* buffer, int64_t offset, int32_t size) {
    Blizzard::Lock::MutexEnter(s_storageLock);

    bool result = false;
    int32_t block = static_cast<int32_t>(offset / entry->m_blockSize);
    Blizzard::Streaming::StorageBlock& storageBlock = entry->m_blocks[block];
    int64_t position = offset + storageBlock.m_storeOffset - static_cast<int64_t>(entry->m_blockSize) * block;

    if (entry->m_flags & 4) {
        Blizzard::Lock::MutexLeave(s_storageLock);
        return false;
    }

    if (storageBlock.m_state != 2) {
        if (storageBlock.m_state == 3) {
            int32_t amount = size;
            result = Blizzard::File::Read(fs, file, buffer, position, &amount);
            Blizzard::Lock::MutexLeave(s_storageLock);
            return result;
        }

        if (storageBlock.m_state != 4) {
            Blizzard::Lock::MutexLeave(s_storageLock);
            return false;
        }

        Blizzard::Lock::MutexLeave(s_storageLock);

        while (storageBlock.m_state == 4) {
            Blizzard::Process::Sleep(1);
        }

        Blizzard::Lock::MutexEnter(s_storageLock);

        if (storageBlock.m_state == 3) {
            result = ReadAt(fs, file, buffer, offset + storageBlock.m_storeOffset - static_cast<int64_t>(entry->m_blockSize) * block, size);
            Blizzard::Lock::MutexLeave(s_storageLock);
            return result;
        }

        result = false;
    }

    int32_t count = static_cast<int32_t>(s_writeQueue.size());

    for (int32_t i = 0; i < count; i++) {
        Blizzard::Streaming::WriteRequest& request = s_writeQueue[i];

        if (entry == request.m_entry && entry->m_blockSize * block == static_cast<int32_t>(request.m_offset)) {
            Blizzard::String::MemCopy(buffer, request.m_data + offset - static_cast<int64_t>(block) * entry->m_blockSize, size);
            result = true;
            break;
        }
    }

    Blizzard::Lock::MutexLeave(s_storageLock);
    return result;
}

bool Blizzard::Storage::QueueWrite(Blizzard::File::Filesystem* fs, Blizzard::File::StreamRecord* file, uint8_t* data, int64_t offset, int32_t size, bool owned) {
    Blizzard::Lock::MutexEnter(s_storageLock);

    Blizzard::Streaming::StorageEntry* entry = file->m_streaming->m_entry;
    Blizzard::Streaming::AddStreamed(size);

    uint32_t block = static_cast<uint32_t>(offset / entry->m_blockSize);
    Blizzard::Streaming::StorageBlock& storageBlock = entry->m_blocks[block];

    if (!(entry->m_flags & 4)) {
        if (!storageBlock.m_state || storageBlock.m_state == 1 || storageBlock.m_state == 4) {
            Blizzard::Streaming::WriteRequest request;
            request.m_fs = fs;
            request.m_pending = 1;
            request.m_file = file;
            request.m_entry = entry;
            request.m_offset = offset;
            request.m_size = size;

            if (owned) {
                request.m_data = data;
            } else {
                request.m_data = static_cast<uint8_t*>(Blizzard::Memory::Allocate(size));
                memmove(request.m_data, data, size);
            }

            storageBlock.m_state = 2;
            s_storageDirty = true;
            s_writeQueue.push_back(request);
            Blizzard::Lock::EventSet(s_storageEvent);
        } else if (owned) {
            Blizzard::Memory::Free(data);
        }
    }

    Blizzard::Lock::MutexLeave(s_storageLock);
    return true;
}

bool Blizzard::Storage::WriteIndex() {
    if (!s_storageReady) {
        return false;
    }

    if (!s_storageDirty) {
        return true;
    }

    int32_t version = 2;
    Blizzard::File::SetPos(s_storageFile, 0, 0);
    int32_t count = 4;
    Blizzard::File::Write(s_storageFile, &version, &count);

    int32_t entries = static_cast<int32_t>(s_storage->size());
    count = 4;
    Blizzard::File::Write(s_storageFile, &entries, &count);

    std::vector<Blizzard::Streaming::StorageBlockRecord> records;

    for (std::map<std::string, Blizzard::Streaming::StorageEntry>::iterator it = s_storage->begin(); it != s_storage->end(); ++it) {
        Blizzard::Streaming::StorageEntry& entry = it->second;

        if (entry.m_flags & 4) {
            continue;
        }

        Blizzard::Streaming::StorageHeader header;
        header.m_flags = entry.m_flags;
        header.m_storeSize = entry.m_storeSize;

        const char* path = entry.m_path.c_str();

        if (path) {
            char* dst = header.m_path;

            do {
                char c = *path++;

                if (!c) {
                    break;
                }

                *dst++ = c;
            } while (dst < &header.m_path[sizeof(header.m_path) - 1]);

            *dst = '\0';
        } else {
            header.m_path[0] = '\0';
        }

        const char* version = entry.m_version.c_str();

        if (version) {
            char* dst = header.m_version;

            do {
                char c = *version++;

                if (!c) {
                    break;
                }

                *dst++ = c;
            } while (dst < &header.m_version[sizeof(header.m_version) - 1]);

            *dst = '\0';
        } else {
            header.m_version[0] = '\0';
        }

        header.m_size = entry.m_size;
        header.m_closed = entry.m_closed;
        header.m_blockSize = entry.m_blockSize;
        header.m_blockCount = static_cast<int32_t>(entry.m_blocks.size());

        int32_t amount = sizeof(header);
        Blizzard::File::Write(s_storageFile, &header, &amount);

        records.resize(header.m_blockCount);

        for (int32_t i = 0; i < header.m_blockCount; i++) {
            records[i].m_state = entry.m_blocks[i].m_state;
            records[i].m_offset = entry.m_blocks[i].m_offset;
            records[i].m_storeOffset = entry.m_blocks[i].m_storeOffset;
            records[i].m_size = entry.m_blocks[i].m_size;
        }

        amount = 24 * header.m_blockCount;
        Blizzard::File::Write(s_storageFile, &records[0], -1LL, &amount);
    }

    Blizzard::File::SetEOF(s_storageFile, 0, 1);
    s_storageDirty = false;

    return true;
}

bool Blizzard::Storage::Remove(const std::string& path) {
    std::map<std::string, Blizzard::Streaming::StorageEntry>::iterator it = s_storage->find(Blizzard::Streaming::ResolveFilePath(path));

    if (it == s_storage->end()) {
        return false;
    }

    s_storage->erase(it);
    return true;
}

bool Blizzard::Storage::ReadStorageFile(const uint8_t* data, int32_t size) {
    Blizzard::Streaming::StorageEntry entry;

    if (size < 4 || size - 4 < 4) {
        return false;
    }

    const uint8_t* cursor = data + 8;
    int32_t left = size - 8;
    int32_t count = *reinterpret_cast<const int32_t*>(data + 4);

    for (int32_t i = 0; i < count && left > 0; i++) {
        if (left < 321) {
            s_storage->clear();
            return false;
        }

        const Blizzard::Streaming::StorageHeader* header = reinterpret_cast<const Blizzard::Streaming::StorageHeader*>(cursor);
        const uint8_t* blocks = cursor + 321;
        left -= 321;

        entry.m_path.assign(header->m_path, strlen(header->m_path));
        entry.m_version.assign(header->m_version, strlen(header->m_version));
        entry.m_storeSize = header->m_storeSize;
        entry.m_size = header->m_size;
        entry.m_flags = header->m_flags;
        entry.m_blockSize = header->m_blockSize;
        entry.m_closed = header->m_closed;
        entry.m_headerWritten = false;
        entry.m_headerOffset = size - left - 321;

        if (!entry.m_blockSize || (entry.m_size + entry.m_blockSize - 1) / entry.m_blockSize != header->m_blockCount) {
            s_storage->clear();
            return false;
        }

        (*s_storage)[Blizzard::Streaming::ResolveFilePath(entry.m_path)] = entry;
        Blizzard::Streaming::StorageEntry& stored = (*s_storage)[Blizzard::Streaming::ResolveFilePath(entry.m_path)];

        int32_t bytes = 24 * header->m_blockCount;

        if (left < bytes) {
            s_storage->clear();
            return false;
        }

        std::vector<Blizzard::Streaming::StorageBlockRecord> records;
        records.resize(header->m_blockCount);
        Blizzard::String::MemCopy(&records[0], blocks, bytes);

        stored.m_blocks.resize(header->m_blockCount);

        for (int32_t j = 0; j < header->m_blockCount; j++) {
            Blizzard::Streaming::StorageBlock& storageBlock = stored.m_blocks[j];
            storageBlock.m_offset = records[j].m_offset;
            storageBlock.m_size = records[j].m_size;
            storageBlock.m_storeOffset = records[j].m_storeOffset;
            storageBlock.m_partOffset = records[j].m_storeOffset;
            storageBlock.m_state = records[j].m_state;
        }

        cursor = blocks + bytes;
        left -= bytes;
    }

    return true;
}

Blizzard::Streaming::StorageEntry* Blizzard::Storage::PartialFile::CreateShared(const std::string& key, const char* path, int64_t size, const char* version, int32_t flags) {
    Blizzard::Streaming::StorageEntry entry;
    entry.m_path.assign(path, strlen(path));
    entry.m_version.assign(version, strlen(version));
    entry.m_flags = flags;
    entry.m_closed = false;
    entry.m_released = false;
    entry.m_refCount = 0;
    entry.m_size = size;
    entry.m_storeSize = 0;
    entry.m_blockSize = s_blockSize;
    entry.m_headerWritten = true;
    entry.m_file = nullptr;
    entry.m_openCount = 0;
    Blizzard::Lock::MutexCreate(entry.m_lock);

    int32_t count = static_cast<int32_t>((size + entry.m_blockSize - 1) / entry.m_blockSize);

    if (s_usePartFiles) {
        entry.m_blocks.resize(count, Blizzard::Streaming::StorageBlock());
        memset(&entry.m_blocks[0], 0, sizeof(Blizzard::Streaming::StorageBlock) * count);
        (*s_storage)[key] = entry;
    } else {
        entry.m_blocks.resize(count, Blizzard::Streaming::StorageBlock());

        int32_t left = static_cast<int32_t>(size);

        for (uint32_t i = 0; i < entry.m_blocks.size(); i++) {
            Blizzard::Streaming::StorageBlock& block = entry.m_blocks[i];
            block.m_state = 0;
            block.m_offset = static_cast<int64_t>(entry.m_blockSize) * i;
            block.m_storeOffset = 0;
            block.m_partOffset = 0;
            block.m_size = i < entry.m_blocks.size() - 1 ? entry.m_blockSize : left;
            left -= entry.m_blockSize;
        }

        entry.m_headerOffset = GetNextHeaderOffset();
        (*s_storage)[key] = entry;
        WriteIndexEntry(&entry, -1, true);
    }

    return &(*s_storage)[key];
}

Blizzard::Streaming::StorageEntry* Blizzard::Storage::Acquire(Blizzard::File::Filesystem* fs, const std::string& key, const char* path, int64_t size, const char* version, int32_t flags) {
    if (!s_storage) {
        return nullptr;
    }

    if (!path) {
        std::map<std::string, Blizzard::Streaming::StorageEntry>::iterator it = s_storage->find(key);

        if (it == s_storage->end()) {
            return nullptr;
        }

        return &it->second;
    }

    Blizzard::Lock::MutexEnter(s_storageLock);

    std::map<std::string, Blizzard::Streaming::StorageEntry>::iterator it = s_storage->find(key);

    char partName[260];
    BuildVariantFileName(partName, sizeof(partName), path, "part");

    if (it != s_storage->end()) {
        Blizzard::File::FileInfo info;

        if (it->second.m_headerWritten || (Blizzard::File::GetFileInfo(fs, partName, &info) && info.m_size == it->second.m_storeSize && size == it->second.m_size && Blizzard::String::Equal(version, it->second.m_version.c_str()))) {
            Blizzard::Streaming::StorageEntry* entry = &it->second;
            Blizzard::Lock::MutexLeave(s_storageLock);
            return entry;
        }

        Remove(it->second.m_path);
    }

    if (!s_usePartFiles) {
        Blizzard::File::Delete(partName);
    }

    Blizzard::Streaming::StorageEntry* entry = PartialFile::CreateShared(key, path, size, version, flags);
    Blizzard::Lock::MutexLeave(s_storageLock);
    return entry;
}

void Blizzard::Storage::Shutdown() {
    if (s_storageReady && s_storage) {
        s_storageStop = true;
        Blizzard::Lock::EventSet(s_storageEvent);

        while (!s_storageStopped) {
            Blizzard::Process::Sleep(10);
        }

        Blizzard::Lock::EventDestroy(s_storageEvent);
        Blizzard::Lock::MutexDestroy(s_storageLock);

        if (s_storageFile) {
            Blizzard::File::Close(s_storageFile);
        }
    }

    delete s_storage;
    s_storage = nullptr;
}

bool Blizzard::Storage::PartialFile::RebuildHeader(Blizzard::File::Filesystem* fs, Blizzard::File::StreamRecord* file) {
    Blizzard::Log::Printf(3, "Logs/Profiling_Storage_Initialization.log", "Reinitialized Part file '%s' started.\n", file->m_pathname);

    Blizzard::File::StreamingInfo* streaming = file->m_streaming;

    if (!streaming || !streaming->m_entry) {
        return false;
    }

    Blizzard::Streaming::StorageEntry* entry = streaming->m_entry;

    Blizzard::Streaming::PartFileHeader header;
    header.m_magic = 2;
    Blizzard::String::Copy(header.m_version, entry->m_version.c_str(), sizeof(header.m_version));
    header.m_flags = entry->m_flags;
    header.m_size = entry->m_size;
    header.m_blockSize = entry->m_blockSize;

    int32_t count = static_cast<int32_t>((header.m_size + header.m_blockSize - 1) / header.m_blockSize);
    entry->m_blocks.resize(count);
    memset(&entry->m_blocks[0], 0, sizeof(Blizzard::Streaming::StorageBlock) * count);

    int32_t amount = sizeof(header);

    if (!Blizzard::File::Write(file, &header, 0, &amount)) {
        Blizzard::File::AddToLastErrorStack(0, "FileStack_Streaming::StreamingOpen - Failed to write .part header section", 20);
        return false;
    }

    amount = 20 * count;

    if (!Blizzard::File::Write(file, &entry->m_blocks[0], 52, &amount)) {
        Blizzard::File::AddToLastErrorStack(0, "FileStack_Streaming::StreamingOpen - Failed to write .part storage section", 20);
        return false;
    }

    int64_t end = 20LL * count + 52;
    entry->m_storeSize = end;
    entry->m_headerWritten = true;
    entry->m_closed = false;
    entry->m_parsed = true;
    Blizzard::File::SetEOF(file, end, 0);

    Blizzard::Log::Printf(3, "Logs/Profiling_Storage_Initialization.log", "Reinitialized Part file '%s' succeeded.\n", file->m_pathname);
    return true;
}

bool Blizzard::Storage::PartialFile::ParseFile(Blizzard::File::Filesystem* fs, Blizzard::File::StreamRecord* file) {
    Blizzard::File::StreamingInfo* streaming = file->m_streaming;

    if (!streaming || !streaming->m_entry) {
        return false;
    }

    Blizzard::Streaming::StorageEntry* entry = streaming->m_entry;

    if (entry->m_parsed) {
        return true;
    }

    if (streaming->m_info.m_size < 52) {
        return RebuildHeader(fs, file);
    }

    Blizzard::Streaming::PartFileHeader header;
    int32_t amount = sizeof(header);
    Blizzard::File::Read(fs, file, &header, 0, &amount);

    if (!header.m_blockSize || entry->m_version.compare(header.m_version) || header.m_size != file->m_info.m_size || header.m_magic != 2) {
        return RebuildHeader(fs, file);
    }

    entry->m_flags = header.m_flags;
    entry->m_storeSize = file->m_streaming->m_info.m_size;

    int32_t count = static_cast<int32_t>((entry->m_blockSize + entry->m_size - 1) / entry->m_blockSize);
    int32_t bytes = 20 * count;
    uint8_t* raw = new uint8_t[bytes];

    std::vector<Blizzard::Streaming::PartBlockRecord> records;
    records.resize(count);

    amount = bytes;
    Blizzard::File::Read(fs, file, raw, 52, &amount);

    if (amount < bytes) {
        delete[] raw;
        return RebuildHeader(fs, file);
    }

    Blizzard::String::MemCopy(&records[0], raw, amount);
    delete[] raw;

    entry->m_blocks.resize(count);

    int32_t local = 0;

    for (int32_t i = 0; i < count; i++) {
        Blizzard::Streaming::StorageBlock& block = entry->m_blocks[i];
        block.m_offset = 0;
        block.m_size = entry->m_blockSize;
        block.m_storeOffset = records[i].m_storeOffset;
        block.m_partOffset = records[i].m_partOffset;
        block.m_state = records[i].m_state != 0 ? 3 : 0;

        if (block.m_state == 3) {
            if (block.m_storeOffset > entry->m_storeSize) {
                return RebuildHeader(fs, file);
            }

            local++;
        }
    }

    int64_t end = bytes + static_cast<int64_t>(local) * entry->m_blockSize + 52;

    if (entry->m_storeSize > end) {
        Blizzard::File::SetEOF(file, end, 0);
        entry->m_storeSize = end;
    } else if (entry->m_storeSize < end) {
        return RebuildHeader(fs, file);
    }

    entry->m_parsed = true;
    return true;
}

bool Blizzard::Storage::WritePartBlockInfo(Blizzard::Streaming::StorageEntry* entry, Blizzard::File::StreamRecord* file, int32_t block) {
    if (!s_storageReady) {
        return false;
    }

    if (entry->m_flags & 4) {
        return true;
    }

    bool result = true;

    if (block >= 0 && block < static_cast<int32_t>(entry->m_blocks.size())) {
        Blizzard::Streaming::PartBlockRecord record;
        record.m_storeOffset = entry->m_blocks[block].m_storeOffset;
        record.m_state = entry->m_blocks[block].m_state;
        record.m_partOffset = entry->m_blocks[block].m_partOffset;

        result = WriteAt(file, &record, 20LL * block + 52, sizeof(record));

        if (!result) {
            Blizzard::File::AddToLastErrorStack(0, "Failed to write storage information chunk", 20);
        }
    }

    return result;
}

bool Blizzard::Storage::WriteBlock(Blizzard::Streaming::WriteRequest* request) {
    Blizzard::Streaming::StorageEntry* entry = request->m_entry;
    uint32_t block = static_cast<uint32_t>(request->m_offset / entry->m_blockSize);
    Blizzard::Streaming::StorageBlock& storageBlock = entry->m_blocks[block];
    int32_t state = storageBlock.m_state;

    if (state == 4 || (state != 3 && state != 2)) {
        return state != 0;
    }

    uint8_t* data = request->m_data;
    bool allocated = false;

    if (block == entry->m_blocks.size() - 1 && request->m_size != entry->m_blockSize) {
        allocated = true;
        data = static_cast<uint8_t*>(Blizzard::Memory::Allocate(entry->m_blockSize));
        Blizzard::String::MemCopy(data, request->m_data, request->m_size);
    }

    int64_t position = entry->m_storeSize;
    bool existing = false;

    if (storageBlock.m_storeOffset) {
        position = storageBlock.m_storeOffset;
        existing = true;
    }

    int32_t amount = entry->m_blockSize;

    if (!Blizzard::File::Write(request->m_file, data, position, &amount)) {
        storageBlock.m_state = 0;
        return false;
    }

    if (allocated) {
        Blizzard::Memory::Free(data);
    }

    if (!existing) {
        entry->m_blocks[block].m_storeOffset = position;
        entry->m_storeSize += entry->m_blockSize;
    }

    storageBlock.m_state = 3;

    bool result = s_usePartFiles ? WritePartBlockInfo(entry, request->m_file, block) : WriteIndexEntry(entry, block, false);

    if (!result) {
        if (!existing) {
            entry->m_blocks[block].m_storeOffset = 0;
            entry->m_storeSize -= entry->m_blockSize;
        }

        storageBlock.m_state = 0;
    }

    return result;
}

bool Blizzard::Storage::FlushFile(Blizzard::File::Filesystem* fs, Blizzard::File::StreamRecord* file) {
    Blizzard::Lock::MutexEnter(s_storageLock);

    int32_t count = static_cast<int32_t>(s_writeQueue.size());

    for (int32_t i = 0; i < count; i++) {
        Blizzard::Streaming::WriteRequest& request = s_writeQueue[i];

        if (file == request.m_file) {
            if (request.m_pending == 1) {
                WriteBlock(&request);
                request.m_pending = 0;
            }

            if (request.m_data) {
                Blizzard::Memory::Free(request.m_data);
                request.m_data = nullptr;
            }
        }
    }

    if (!s_usePartFiles) {
        WriteIndex();
    }

    file->m_streaming->m_entry->m_released = true;
    Blizzard::Lock::MutexLeave(s_storageLock);

    while (file->m_streaming->m_entry->m_refCount > 0) {
        Blizzard::Process::Sleep(1);
    }

    return true;
}

uint32_t Blizzard::Storage::StorageThread(void* param) {
    if (s_storageStop) {
        s_storageStopped = true;
        return 1;
    }

    do {
        Blizzard::Lock::EventWaitForever(s_storageEvent);

        if (s_storageStop) {
            break;
        }

        for (;;) {
            Blizzard::Lock::MutexEnter(s_storageLock);

            if (!s_writeQueue.size()) {
                break;
            }

            Blizzard::Streaming::WriteRequest& request = s_writeQueue.front();

            if (request.m_pending == 1) {
                WriteBlock(&request);

                if (request.m_data) {
                    Blizzard::Memory::Free(request.m_data);
                    request.m_data = nullptr;
                }
            }

            s_writeQueue.pop_front();
            Blizzard::Lock::MutexLeave(s_storageLock);

            if (s_storageStop) {
                s_storageStopped = true;
                return 1;
            }
        }

        Blizzard::Lock::MutexLeave(s_storageLock);
    } while (!s_storageStop);

    s_storageStopped = true;
    return 1;
}

bool Blizzard::Storage::Close(Blizzard::File::Filesystem* fs, Blizzard::File::StreamRecord* file, Blizzard::Streaming::StorageEntry* entry) {
    if (!entry || (entry->m_flags & 4)) {
        return true;
    }

    Blizzard::Lock::MutexEnter(s_storageLock);

    if ((entry->m_flags & 1) && IsComplete(entry)) {
        entry->m_closed = true;
        Blizzard::Lock::MutexLeave(s_storageLock);
        return true;
    }

    bool result = FlushFile(fs, file);

    for (std::vector<Blizzard::Streaming::StorageBlock>::iterator it = entry->m_blocks.begin(); it != entry->m_blocks.end(); ++it) {
        if (it->m_state != 3) {
            it->m_state = 0;
        }
    }

    Blizzard::Lock::MutexLeave(s_storageLock);
    return result;
}

bool Blizzard::Storage::Initialize() {
    s_usePartFiles = true;

    if (!s_storageReady) {
        s_storage = new std::map<std::string, Blizzard::Streaming::StorageEntry>();
        Blizzard::Lock::MutexCreate(s_storageLock);
        Blizzard::Lock::EventCreate(s_storageEvent);
        Blizzard::Thread::CreateDetached(StorageThread, nullptr, "Streaming Storage Thread");
        s_storageReady = true;
    }

    return true;
}

bool Blizzard::Storage::Initialize(const char* path, bool required) {
    s_usePartFiles = false;

    if (s_storageReady) {
        Blizzard::File::AddToLastErrorStack(0, "Storage::Initialize - s_storageReady said we've been here", 20);
        return false;
    }

    s_storage = new std::map<std::string, Blizzard::Streaming::StorageEntry>();
    Blizzard::Lock::MutexCreate(s_storageLock);
    Blizzard::Lock::EventCreate(s_storageEvent);
    Blizzard::Thread::CreateDetached(StorageThread, nullptr, "Streaming Storage Thread");
    s_storageReady = true;

    void* data = nullptr;
    uint32_t size = 0;
    char absolute[260];

    if (!path || !Blizzard::File::MakeAbsolutePath(path, absolute, sizeof(absolute), false)) {
        Blizzard::File::AddToLastErrorStack(0, "Storage::Initialize - MakeAbsolutePath failed", 20);
        return false;
    }

    s_storagePath.assign(absolute, strlen(absolute));

    bool found = false;

    for (int32_t i = 0; i < 25; i++) {
        if (found) {
            break;
        }

        char name[260];
        BuildVariantFileName(name, sizeof(name), path, "");

        if (!Blizzard::File::Exists(name)) {
            break;
        }

        if (Blizzard::File::Open(name, 4099, s_storageFile)) {
            if (!System_File::LoadFile(s_storageFile, &data, &size)) {
                break;
            }

            found = true;
        } else {
            s_variantIndex++;
        }
    }

    if (!found) {
        if (required) {
            Blizzard::File::AddToLastErrorStack(0, "Storage::Initialize - Bad existing storage file", 20);
            return false;
        }

        if (s_storageFile) {
            if (data) {
                Blizzard::Memory::Free(data);
            }

            Blizzard::File::Close(s_storageFile);
            s_storageFile = nullptr;
            Blizzard::File::Delete(path);
        }

        s_usePartFiles = true;
        return true;
    }

    bool result = ReadStorageFile(static_cast<uint8_t*>(data), size);
    Blizzard::Memory::Free(data);

    if (result) {
        return true;
    }

    if (required) {
        Blizzard::File::AddToLastErrorStack(0, "Storage::Initialize - Bad existing storage file", 20);
        return false;
    }

    Blizzard::File::Close(s_storageFile);
    s_storageFile = nullptr;
    s_usePartFiles = true;
    Blizzard::File::Delete(path);
    return true;
}

#endif
