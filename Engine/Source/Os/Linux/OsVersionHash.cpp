#include <cstdint>
#include "Engine/Source/Os/OsFile.h"
#include "Engine/Source/Os/OsVersionHash.h"
#include "Engine/Source/Storm/H/storm.h"

// OFFSET: 0x00871520
OsVersionHash::~OsVersionHash() {
    if (this->m_entries) {
        SMemFree(this->m_entries, __FILE__, __LINE__, 0);
    }
}

// OFFSET: 0x00871540
bool OsVersionHash::AddEntry(const OsVersionHashEntry& entry) {
    if (!this->m_entries) {
        return false;
    }

    if (this->m_count >= this->m_capacity) {
        this->m_capacity *= 2;
        this->m_entries = static_cast<OsVersionHashEntry*>(SMemReAlloc(this->m_entries, this->m_capacity * sizeof(OsVersionHashEntry), __FILE__, __LINE__, 0));

        if (!this->m_entries) {
            return false;
        }
    }

    this->m_entries[this->m_count] = entry;
    this->m_count++;

    return true;
}

// OFFSET: 0x00871BE0
OsVersionHash::OsVersionHash() : m_entries(nullptr) {
    this->m_entries = static_cast<OsVersionHashEntry*>(SMemAlloc(256 * sizeof(OsVersionHashEntry), __FILE__, __LINE__, 0));
    this->m_count = 0;
    this->m_capacity = 256;
}

// ʕ •ᴥ•ʔ✿ There is no mapped PE image on Linux, so loaded modules hash their file contents ✿ ʕ •ᴥ•ʔ
int32_t IOsVersionChecksum(IOsVersionHashCallback& callback, const char* path, int32_t loaded) {
    if (!path && !loaded) {
        return 0;
    }

    if (!path) {
        path = "/proc/self/exe";
    }

    int32_t result = 0;
    void* buffer = nullptr;

    HOSFILE file = OsCreateFile(path, OS_GENERIC_READ, 0x1, OS_OPEN_EXISTING, OS_FILE_ATTRIBUTE_NORMAL, OS_FILE_TYPE_ANY);

    if (file != OS_INVALID_FILE) {
        uint64_t size = OsGetFileSize(file);

        if (size == static_cast<uint32_t>(size)) {
            buffer = SMemAlloc(static_cast<uint32_t>(size), __FILE__, __LINE__, 0);

            uint32_t bytesRead;
            if (buffer && OsReadFile(file, buffer, static_cast<uint32_t>(size), &bytesRead) && bytesRead == size) {
                callback.Process(buffer, static_cast<uint32_t>(size));
                result = 1;
            }
        }
    }

    if (buffer) {
        SMemFree(buffer, __FILE__, __LINE__, 0);
    }

    if (file != OS_INVALID_FILE) {
        OsCloseFile(file);
    }

    return result;
}

OsFileVersionInfo::OsFileVersionInfo(const char* filename) : m_fileVersion(), m_productVersion() {
    m_data.Clear();
}
