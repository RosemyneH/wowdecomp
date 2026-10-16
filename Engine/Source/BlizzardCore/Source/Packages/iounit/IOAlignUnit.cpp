#include <algorithm>
#include <cstdint>
#include <cstring>
#include <new>
#include "Engine/Source/BlizzardCore/Source/Packages/iounit/IOAlignUnit.h"
#include "Engine/Source/BlizzardCore/Source/Packages/iounit/IOUnitContainer.h"
#include "Engine/Source/BlizzardCore/Source/System/File/File.h"
#include "Engine/Source/Storm/H/storm.h"

// INLINED
bool IOAlignUnit::CacheBlock::operator<(const CacheBlock& other) const {
    return this->m_offset < other.m_offset;
}

// VERIFY: ba556dcf manual
// OFFSET: 0x00430650
IOAlignUnit::IOAlignUnit(int64_t base, uint32_t blockSize, uint32_t blockCount, bool largeTransfers, bool exactSize, uint32_t bufferAlign, bool skipCleanOnWrite, int64_t size, bool ownsMemory, IOUnit* next) : IOUnit(next, ownsMemory) {
    this->m_largeTransfers = largeTransfers;
    this->m_bufferAlign = bufferAlign;
    this->m_blockSize = blockSize;
    this->m_exactSize = exactSize;
    this->m_skipCleanOnWrite = skipCleanOnWrite;
    this->m_blockCount = blockCount;
    this->m_blocks = blockCount > 1 ? new CacheBlock[blockCount] : &this->m_block;
    this->m_alignedBuffer = nullptr;
    this->m_buffer = nullptr;
    this->m_writtenSize = size;
    this->m_size = size;

    if (!exactSize) {
        this->m_size = (size + blockSize - 1) / blockSize * blockSize;
    }

    this->m_base = base >= blockSize ? base % blockSize : base;

    for (uint32_t i = 0; i < blockCount; i++) {
        this->m_blocks[i].m_offset = -1;
        this->m_blocks[i].m_dirty = false;
        this->m_blocks[i].m_data = nullptr;
    }
}

// VERIFY: ac983fc0 auto
// OFFSET: 0x00434CA0
IOAlignUnit::~IOAlignUnit() {
    if (this->m_blocks != &this->m_block) {
        delete[] this->m_blocks;
    }
}

// VERIFY: c125ece0 auto
// OFFSET: 0x00429610
uint32_t IOAlignUnit::GetType() {
    return 'alig';
}

// VERIFY: 8f878c27 manual
// OFFSET: 0x0042B3C0
void IOAlignUnit::AllocateBuffer() {
    if (this->m_buffer) {
        return;
    }

    uint32_t count = this->m_blockCount;
    this->m_buffer = SMemAlloc(count * this->m_blockSize + this->m_bufferAlign - 1, __FILE__, __LINE__, SMEM_FLAG_ZEROMEMORY);

    if (!this->m_buffer) {
        return;
    }

    this->m_alignedBuffer = reinterpret_cast<uint8_t*>(~static_cast<uintptr_t>(this->m_bufferAlign - 1) & (reinterpret_cast<uintptr_t>(this->m_buffer) + this->m_bufferAlign - 1));

    for (uint32_t i = 0; i < count; i++) {
        this->m_blocks[i].m_offset = -1;
        this->m_blocks[i].m_dirty = false;
        this->m_blocks[i].m_data = this->m_alignedBuffer + i * this->m_blockSize;
    }
}

// VERIFY: 1821d547 manual
// OFFSET: 0x0042B450
void IOAlignUnit::MoveToBack(uint32_t index) {
    uint32_t count = this->m_blockCount;

    if (index == count - 1) {
        return;
    }

    CacheBlock block = this->m_blocks[index];
    memmove(&this->m_blocks[index], &this->m_blocks[index + 1], (count - index - 1) * sizeof(CacheBlock));
    this->m_blocks[count - 1] = block;
}

// VERIFY: 50927b95 manual
// OFFSET: 0x0042B4D0
bool IOAlignUnit::CleanCacheEntry(uint32_t index) {
    CacheBlock* block = &this->m_blocks[index];

    if (!block->m_dirty) {
        return true;
    }

    int64_t end = block->m_offset + this->m_blockSize;
    uint32_t count = static_cast<uint32_t>(std::min(end, this->m_size) - block->m_offset);
    count = std::min(this->m_blockSize, count);

    if (!this->m_exactSize) {
        memset(block->m_data + count, 0, this->m_blockSize - count);
        count = this->m_blockSize;
    }

    bool result = this->m_next->Write(this->m_blocks[index].m_data, this->m_blocks[index].m_offset, count);

    block = &this->m_blocks[index];
    this->m_writtenSize = std::max(this->m_writtenSize, block->m_offset + count);
    block->m_dirty = !result;

    return result;
}

// VERIFY: a1f0cb23 manual
// OFFSET: 0x004308C0
uint8_t* IOAlignUnit::GetCachedBlock(int64_t offset, bool dirty) {
    this->AllocateBuffer();

    if (!this->m_buffer) {
        return nullptr;
    }

    uint32_t count = this->m_blockCount;
    uint32_t index = 0;

    while (index < count && this->m_blocks[index].m_offset != offset) {
        index++;
    }

    if (index >= count) {
        index = count - 1;

        if (!this->CleanCacheEntry(index)) {
            return nullptr;
        }

        CacheBlock* block = &this->m_blocks[index];
        block->m_offset = -1;

        if (offset + this->m_blockSize > this->m_writtenSize) {
            if (offset >= this->m_writtenSize) {
                memset(block->m_data, 0, this->m_blockSize);
            } else {
                uint32_t valid = static_cast<uint32_t>(this->m_writtenSize - offset);

                if (!this->m_next->Read(block->m_data, offset, valid, nullptr)) {
                    return nullptr;
                }

                memset(block->m_data + valid, 0, this->m_blockSize - valid);
            }
        } else if (!this->m_next->Read(block->m_data, offset, this->m_blockSize, nullptr)) {
            return nullptr;
        }

        block->m_offset = offset;
    }

    if (index) {
        CacheBlock block = this->m_blocks[index];
        memmove(&this->m_blocks[1], &this->m_blocks[0], index * sizeof(CacheBlock));
        this->m_blocks[0] = block;
    }

    this->m_blocks[0].m_dirty |= dirty;
    return this->m_blocks[0].m_data;
}

// VERIFY: 54c96425 manual
// OFFSET: 0x004408B0
bool IOAlignUnit::CleanCache() {
    if (!this->m_buffer) {
        return true;
    }

    bool result = true;
    std::sort(this->m_blocks, this->m_blocks + this->m_blockCount);

    uint32_t count = this->m_blockCount;

    for (uint32_t i = 0; i < count; i++) {
        if (!this->CleanCacheEntry(i)) {
            result = false;
        }
    }

    return result;
}

// VERIFY: 6efd4aeb manual
// OFFSET: 0x00457A00
bool IOAlignUnit::Read(void* buffer, int64_t offset, uint32_t count, uint32_t* bytesRead) {
    uint32_t dummy;

    if (!bytesRead) {
        bytesRead = &dummy;
    }

    if (offset < this->m_base) {
        Blizzard::File::SetLastError(10);
        Blizzard::File::AddToLastErrorStack(10, "IOAlignUnit::Read - Range Error", 1);
        return false;
    }

    if (!bytesRead && offset + count > this->m_size) {
        return false;
    }

    if (!count) {
        *bytesRead = 0;
        return true;
    }

    if (offset >= this->m_size) {
        if (offset == this->m_size) {
            *bytesRead = 0;
            return true;
        }

        Blizzard::File::SetLastError(10);
        Blizzard::File::AddToLastErrorStack(10, "IOAlignUnit::Read - Range Error", 1);
        return false;
    }

    count = std::min(count, static_cast<uint32_t>(this->m_size - offset));

    uint8_t* dst = static_cast<uint8_t*>(buffer);
    int64_t position = this->m_base + (offset - this->m_base) / this->m_blockSize * this->m_blockSize;
    uint32_t remaining = count;

    if (position != offset) {
        uint32_t head = static_cast<uint32_t>(offset - position);
        uint32_t amount = std::min(remaining, this->m_blockSize - head);
        uint8_t* block = this->GetCachedBlock(position, false);

        if (!block) {
            return false;
        }

        memcpy(dst, block + head, amount);
        dst += amount;
        remaining = count - amount;
        position += this->m_blockSize;
    }

    if (!(this->m_exactSize && this->m_largeTransfers && position + remaining == this->m_size) && remaining >= this->m_blockSize) {
        uint32_t whole = remaining / this->m_blockSize * this->m_blockSize;
        uint32_t chunk = this->m_largeTransfers ? whole : this->m_blockSize;
        int32_t blockCount = this->m_blockCount;

        for (int32_t i = 0; i < blockCount; i++) {
            int64_t blockOffset = this->m_blocks[i].m_offset;

            if (blockOffset >= position && blockOffset < position + whole && !this->CleanCacheEntry(i)) {
                return false;
            }
        }

        do {
            if (!this->m_next->Read(dst, position, chunk, nullptr)) {
                return false;
            }

            dst += chunk;
            remaining -= chunk;
            position += chunk;
        } while (remaining >= chunk);
    }

    if (this->m_exactSize && position + remaining == this->m_size) {
        if (!this->m_next->Read(dst, position, remaining, nullptr)) {
            return false;
        }
    } else if (remaining) {
        uint8_t* block = this->GetCachedBlock(position, false);

        if (!block) {
            return false;
        }

        memcpy(dst, block, remaining);
    }

    *bytesRead = count;
    return true;
}

// VERIFY: 99286d46 manual
// OFFSET: 0x004437D0
bool IOAlignUnit::Write(const void* buffer, int64_t offset, uint32_t count) {
    if (offset < this->m_base) {
        return false;
    }

    const uint8_t* src = static_cast<const uint8_t*>(buffer);
    int64_t position = this->m_base + (offset - this->m_base) / this->m_blockSize * this->m_blockSize;
    uint32_t remaining = count;

    if (position != offset) {
        uint32_t head = static_cast<uint32_t>(offset - position);
        uint32_t amount = std::min(remaining, this->m_blockSize - head);
        uint8_t* block = this->GetCachedBlock(position, true);

        if (!block) {
            return false;
        }

        memcpy(block + head, src, amount);
        src += amount;
        remaining = count - amount;
        position += this->m_blockSize;
        offset += amount;
        this->m_size = std::max(this->m_size, offset);
    }

    if (remaining >= this->m_blockSize) {
        uint32_t whole = remaining / this->m_blockSize * this->m_blockSize;

        for (int32_t i = this->m_blockCount - 1; i >= 0; i--) {
            CacheBlock* block = &this->m_blocks[i];

            if (block->m_offset >= position && block->m_offset < position + whole) {
                block->m_offset = -1;
                this->m_blocks[i].m_dirty = false;
                this->MoveToBack(i);
            }
        }

        if (!this->m_skipCleanOnWrite && !this->CleanCache()) {
            return false;
        }

        uint32_t chunk = this->m_largeTransfers ? whole : this->m_blockSize;

        do {
            if (!this->m_next->Write(src, position, chunk)) {
                return false;
            }

            src += chunk;
            remaining -= chunk;
            position += chunk;
        } while (remaining >= chunk);

        this->m_size = std::max(this->m_size, position);
        this->m_writtenSize = std::max(this->m_writtenSize, position);
    }

    if (remaining) {
        uint8_t* block = this->GetCachedBlock(position, true);

        if (!block) {
            return false;
        }

        memcpy(block, src, remaining);
        this->m_size = std::max(this->m_size, position + remaining);
    }

    return true;
}

// VERIFY: 0b965076 manual
// OFFSET: 0x00443A90
bool IOAlignUnit::Flush() {
    bool result = this->CleanCache();

    if (!this->m_next->Flush()) {
        return false;
    }

    return result;
}

// VERIFY: b5bcb124 auto
// OFFSET: 0x00426320
bool IOAlignUnit::GetFileSize(int64_t* size) {
    *size = this->m_size;
    return true;
}

// VERIFY: 71918a65 manual
// OFFSET: 0x004307A0
bool IOAlignUnit::SetFileSize(int64_t size) {
    if (this->m_buffer && size < this->m_size) {
        int64_t blockStart = this->m_base + (size - this->m_base - 1) / this->m_blockSize * this->m_blockSize;
        uint32_t tail = static_cast<uint32_t>(size - blockStart);

        for (int32_t i = this->m_blockCount - 1; i >= 0; i--) {
            CacheBlock* block = &this->m_blocks[i];

            if (block->m_offset >= blockStart) {
                if (block->m_offset <= blockStart) {
                    memset(block->m_data + tail, 0, this->m_blockSize - tail);
                } else {
                    block->m_offset = -1;
                    this->m_blocks[i].m_dirty = false;
                    this->MoveToBack(i);
                }
            }
        }
    }

    this->m_size = size;
    return this->m_next->SetFileSize(size);
}

// VERIFY: 3d18d170 manual
// OFFSET: 0x004437A0
bool IOAlignUnit::Close() {
    bool result = this->CleanCache();
    SMemFree(this->m_buffer, __FILE__, __LINE__, 0);
    return result;
}

// VERIFY: c897ffe8 manual
// OFFSET: 0x00434B90
IOAlignUnit* IOAlignUnit::Create(IOUnitContainer* container, int64_t base, uint32_t blockSize, uint32_t blockCount, bool largeTransfers, bool exactSize, uint32_t bufferAlign, bool skipCleanOnWrite, IOUnit* next) {
    if (!next) {
        return nullptr;
    }

    int64_t size;

    if (!next->GetFileSize(&size)) {
        next->Release();
        return nullptr;
    }

    if (!blockSize) {
        blockSize = 1;
    }

    bool ownsMemory;
    void* memory = IOUnitContainer::Alloc(container, sizeof(IOAlignUnit), &ownsMemory);

    if (!memory) {
        return nullptr;
    }

    return new (memory) IOAlignUnit(base, blockSize, blockCount, largeTransfers, exactSize, bufferAlign, skipCleanOnWrite, size, ownsMemory, next);
}

// VERIFY: 5e8cfd60 auto
// OFFSET: 0x00434C60
IOAlignUnit* IOAlignUnit::Create(IOUnitContainer* container, int64_t base, uint32_t blockSize, uint32_t blockCount, bool largeTransfers, bool exactSize, uint32_t bufferAlign, IOUnit* next) {
    return IOAlignUnit::Create(container, base, blockSize, blockCount, largeTransfers, exactSize, bufferAlign, true, next);
}
