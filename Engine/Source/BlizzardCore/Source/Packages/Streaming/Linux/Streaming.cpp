#if defined(__linux__) || defined(__APPLE__)

#include <cctype>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>
#include "Engine/Source/BlizzardCore/Source/System/Lock/Lock.h"
#include "Engine/Source/BlizzardCore/Source/System/Process/Process.h"
#include "Engine/Source/BlizzardCore/Include/Blizzard/Util/MD5.h"
#include "Engine/Source/BlizzardCore/Source/Packages/Streaming/Manifests.h"
#include "Engine/Source/BlizzardCore/Source/Packages/Streaming/Storage.h"
#include "Engine/Source/BlizzardCore/Source/Packages/Streaming/Streaming.h"
#include "Engine/Source/BlizzardCore/Source/System/Debug/ErrorStack.h"
#include "Engine/Source/BlizzardCore/Source/System/File/File.h"
#include "Engine/Source/BlizzardCore/Source/System/File/Filestack.h"
#include "Engine/Source/BlizzardCore/Source/System/File/Filestack_Streaming.h"
#include "Engine/Source/BlizzardCore/Source/System/Log/Log.h"
#include "Engine/Source/BlizzardCore/Source/System/Memory/MemoryStorm.h"
#include "Engine/Source/BlizzardCore/Source/System/String/Path.h"
#include "Engine/Source/BlizzardCore/Source/System/String/String.h"
#include "Engine/Source/BlizzardCore/Source/System/Thread/Thread.h"

static const int32_t s_queueOrder[6] = { 0, 1, 0, 2, 0, 1 };

static const int32_t s_queueGroupBase[2] = { 0, 4 };

static bool s_useBackgroundQueues = true;

static bool s_throttleProgress = true;

static Blizzard::Lock::Event s_throttleEvent;

static Blizzard::Lock::Mutex s_queueLock;

static Blizzard::Lock::Event s_threadEvents[2];

static Blizzard::Lock::Event s_pauseEvent;

static Blizzard::Streaming::FILE_THREAD_CMD* s_currentCommands[2];

static std::deque<Blizzard::Streaming::FILE_THREAD_CMD>* s_queues[7];

static Blizzard::Lock::Mutex s_pauseLock;

static bool s_paused;

static Blizzard::Lock::Event s_queueEvent;

static volatile bool s_stopThreads;

static volatile int32_t s_threadMask;

static int32_t s_busyMask;

static Blizzard::Streaming::DigestStateMap* s_digestStates;

static Blizzard::Streaming::PathMap* s_paths;

static int32_t s_commandsTaken;

static int32_t s_commandsFreed;

Blizzard::Streaming::DownloadProgress Blizzard::Streaming::s_progress;

static int32_t s_queueRotation[2];

int32_t Blizzard::Streaming::GetIdleThread() {
    for (int32_t thread = 0; thread < 2; thread++) {
        if (((1 << thread) & s_threadMask) && !((1 << thread) & s_busyMask)) {
            return thread;
        }
    }

    return -1;
}

void Blizzard::Streaming::DownloadProgress::Sample(uint32_t time) {
    if (++this->m_index >= 51) {
        this->m_index = 0;
    }

    this->m_samples[this->m_index].m_time = 0;
    this->m_samples[this->m_index].m_bytes = this->m_streamed;
    this->m_samples[this->m_index].m_time = time;
}

int32_t Blizzard::Streaming::DownloadProgress::GetRate() {
    uint32_t last = this->m_index;
    uint32_t first;

    if (last == 50) {
        first = 0;
    } else {
        first = last + 1;

        if (!last) {
            last = 51;
        }
    }

    uint32_t end = last - 1;

    while (!this->m_samples[first].m_time) {
        if (first == end) {
            break;
        }

        if (++first > 50) {
            first = 0;
        }
    }

    for (; !this->m_samples[end].m_time; end--) {
        if (first == end) {
            break;
        }

        if (!end) {
            end = 51;
        }
    }

    int64_t bytes = this->m_samples[end].m_bytes - this->m_samples[first].m_bytes;
    int32_t time = this->m_samples[end].m_time - this->m_samples[first].m_time;

    if (first == end) {
        uint32_t next = end + 1;

        if (next == 51) {
            next = 0;
        }

        if (this->m_samples[next].m_time) {
            bytes = this->m_samples[next].m_bytes;
            time = this->m_samples[next].m_time - this->m_lastTime;
        }
    }

    if (bytes > 0) {
        return static_cast<int32_t>(1000 * (bytes / time));
    }

    return 0;
}

void Blizzard::Streaming::HashFileName(Blizzard::Util::Digest<16>* digest, const char* name) {
    char lower[260];
    memset(lower, 0, sizeof(lower));

    if (name) {
        char* dst = lower;

        do {
            char c = *name++;

            if (!c) {
                break;
            }

            *dst++ = c;
        } while (dst < &lower[259]);

        *dst = '\0';
    } else {
        lower[0] = '\0';
    }

    for (char* cursor = lower; *cursor; cursor++) {
        *cursor = tolower(*cursor);
    }

    Blizzard::Util::HashString(digest->m_data, lower);
}

void Blizzard::Streaming::WaitForLoads() {
    for (;;) {
        Blizzard::Lock::MutexEnter(s_queueLock);

        bool busy = false;

        for (int32_t i = 0; i < 2; i++) {
            if (s_currentCommands[i] && s_currentCommands[i]->m_type == 1) {
                busy = true;
                break;
            }
        }

        Blizzard::Lock::MutexLeave(s_queueLock);

        if (!busy) {
            return;
        }

        Blizzard::Process::Sleep(1);
    }
}

bool Blizzard::Streaming::CancelQueue(int32_t priority) {
    Blizzard::Lock::MutexEnter(s_queueLock);

    std::deque<FILE_THREAD_CMD>& queue = *s_queues[priority];

    for (std::deque<FILE_THREAD_CMD>::iterator it = queue.begin(); it != queue.end(); ++it) {
        if (it->m_completion && it->m_type == 2) {
            it->m_completion->m_result = static_cast<int32_t>(s_queues[priority]->size());
            it->m_completion->m_done = true;
            Blizzard::Lock::EventSet(*it->m_completion->m_event);
            it->m_completion = nullptr;
        }

        it->m_type = 0;
    }

    for (DigestStateMap::iterator it = s_digestStates->begin(); it != s_digestStates->end(); ++it) {
        if (it->second.m_state && it->second.m_priority == priority) {
            it->second.m_state = 0;
        }
    }

    Blizzard::Lock::MutexLeave(s_queueLock);
    return true;
}

bool Blizzard::Streaming::PopCommand(FILE_THREAD_CMD* command, int32_t queue) {
    if (!s_queues[queue]->size()) {
        return false;
    }

    *command = s_queues[queue]->front();
    s_queues[queue]->pop_front();
    s_commandsTaken++;

    return true;
}

bool Blizzard::Streaming::SelectQueue(FILE_THREAD_CMD* command, int32_t group, int32_t* queue) {
    if (group >= 1 && !s_useBackgroundQueues) {
        return false;
    }

    int32_t base = s_queueGroupBase[group];

    for (int32_t i = 0; i < 6; i++) {
        *queue = base + s_queueOrder[s_queueRotation[group]];
        s_queueRotation[group] = (s_queueRotation[group] + 1) % 6;

        if (PopCommand(command, *queue)) {
            return true;
        }
    }

    return false;
}

int32_t Blizzard::Streaming::GetNextCommand(FILE_THREAD_CMD** command, int32_t thread) {
    FILE_THREAD_CMD* next = new FILE_THREAD_CMD();
    int32_t queue = 7;

    if (SelectQueue(next, 0, &queue) || (queue = 3, PopCommand(next, 3)) || SelectQueue(next, 1, &queue)) {
        s_currentCommands[thread] = next;
        *command = next;
        return queue;
    }

    delete next;
    s_commandsFreed++;
    return 7;
}

void Blizzard::Streaming::DownloadProgress::FireCallback() {
    if (!this->m_enabled) {
        return;
    }

    if (s_throttleProgress && Blizzard::Time::Milliseconds() - this->m_lastTime < 5000) {
        return;
    }

    PROGRESSCALLBACK callback = this->m_callback;

    if (callback) {
        callback(static_cast<uint32_t>(this->m_total), static_cast<uint32_t>(this->m_total >> 32), static_cast<uint32_t>(this->m_downloaded), static_cast<uint32_t>(this->m_downloaded >> 32), static_cast<uint32_t>(this->m_streamed), static_cast<uint32_t>(this->m_streamed >> 32), this->GetRate());
    }
}

void Blizzard::Streaming::AddDownloaded(int32_t size) {
    s_progress.m_downloaded += size;
    s_progress.FireCallback();
}

void Blizzard::Streaming::AddStreamed(int32_t size) {
    s_progress.m_streamed += size;

    uint32_t now = static_cast<uint32_t>(Blizzard::Time::Milliseconds());

    if (now - s_progress.m_samples[s_progress.m_index].m_time > 100) {
        s_progress.Sample(now);
        s_progress.FireCallback();
    }
}

bool Blizzard::Streaming::RemoveCompletion(Completion* completion, int32_t priority) {
    if (!s_queues[priority]->size()) {
        return false;
    }

    Blizzard::Lock::MutexEnter(s_queueLock);

    std::deque<FILE_THREAD_CMD>& queue = *s_queues[priority];

    for (std::deque<FILE_THREAD_CMD>::iterator it = queue.end(); it != queue.begin();) {
        --it;

        if (it->m_completion == completion) {
            it->m_completion = nullptr;
            Blizzard::Lock::MutexLeave(s_queueLock);
            return true;
        }
    }

    Blizzard::Lock::MutexLeave(s_queueLock);
    return false;
}

bool Blizzard::Streaming::QueueLoad(Blizzard::File::StreamRecord* file, int32_t priority, LoadRange* range, Completion* completion) {
    Blizzard::File::Filesystem* fs = System_File::Stacked::s_manager;

    while (fs && fs->m_original != &System_File::Stacked::s_streamstack) {
        fs = fs->m_next;
    }

    FILE_THREAD_CMD command;
    command.m_fs = fs;
    command.m_type = 1;
    command.m_file = file;
    command.m_range = static_cast<LoadRange*>(Blizzard::Memory::Allocate(sizeof(LoadRange)));

    if (range) {
        memmove(command.m_range, range, sizeof(LoadRange));
    }

    command.m_completion = completion;

    Blizzard::Lock::MutexEnter(s_queueLock);
    s_queues[priority]->push_back(command);
    Blizzard::Lock::MutexLeave(s_queueLock);

    return Blizzard::Storage::RequestRange(file->m_streaming->m_entry, range->m_blockOffset, range->m_blockSize, 1);
}

bool Blizzard::Streaming::RequestRanges(Blizzard::File::StreamRecord* file, LoadRange* ranges, int32_t count) {
    int32_t state = 0;
    int32_t requests = 0;
    int32_t blocks = 0;
    std::vector<Completion*> completions;

    LoadRange current;
    current.m_offset = 0;
    current.m_size = 0;
    current.m_state = 0;

    for (int32_t i = 0; i < count; i++) {
        LoadRange& range = ranges[i];

        if (range.m_state > 4) {
            continue;
        }

        if (!state) {
            if (range.m_state < 2) {
                continue;
            }

            current.m_blockOffset = range.m_blockOffset;
            current.m_blockSize = range.m_blockSize;
            current.m_state = 2;
            blocks++;
            state = 2;
            continue;
        }

        if (current.m_blockOffset + current.m_blockSize != range.m_blockOffset) {
            Completion* completion = new Completion();
            Blizzard::Lock::Event* event = new Blizzard::Lock::Event();
            completion->m_event = event;
            Blizzard::Lock::EventCreate(*event);
            completions.push_back(completion);

            QueueLoad(file, 0, &current, completion);
            Blizzard::Lock::EventSet(s_throttleEvent);
            Blizzard::Lock::EventSet(s_queueEvent);
            requests++;

            if (range.m_state < 2) {
                state = 0;
            } else {
                current.m_blockOffset = range.m_blockOffset;
                current.m_blockSize = range.m_blockSize;
                state = range.m_state;
            }

            current.m_state = state;
        } else {
            current.m_blockSize += range.m_blockSize;
            blocks++;
        }
    }

    if (state) {
        if (requests || blocks < 4) {
            Completion* completion = new Completion();
            Blizzard::Lock::Event* event = new Blizzard::Lock::Event();
            completion->m_event = event;
            Blizzard::Lock::EventCreate(*event);
            completions.push_back(completion);
            QueueLoad(file, 0, &current, completion);
        } else {
            int32_t half = Blizzard::Storage::s_blockSize * (blocks - blocks / 2);

            LoadRange second;
            second.m_offset = 0;
            second.m_size = 0;
            second.m_state = 2;
            second.m_blockOffset = current.m_blockOffset + half;
            second.m_blockSize = current.m_blockSize - half;
            current.m_blockSize = half;

            Completion* completion = new Completion();
            Blizzard::Lock::Event* event = new Blizzard::Lock::Event();
            completion->m_event = event;
            Blizzard::Lock::EventCreate(*event);
            completions.push_back(completion);
            QueueLoad(file, 0, &current, completion);

            completion = new Completion();
            event = new Blizzard::Lock::Event();
            completion->m_event = event;
            Blizzard::Lock::EventCreate(*event);
            completions.push_back(completion);
            QueueLoad(file, 0, &second, completion);
        }
    }

    Blizzard::Lock::EventSet(s_throttleEvent);
    Blizzard::Lock::EventSet(s_queueEvent);

    bool success = true;

    for (std::vector<Completion*>::iterator it = completions.begin(); it != completions.end(); ++it) {
        Blizzard::Lock::EventWaitForever(*(*it)->m_event);
        Blizzard::Lock::EventDestroy(*(*it)->m_event);

        if (!(*it)->m_success) {
            success = false;
        }

        delete (*it)->m_event;
        (*it)->m_event = nullptr;
        delete *it;
        *it = nullptr;
    }

    completions.erase(completions.begin(), completions.end());

    return success;
}

bool Blizzard::Streaming::QueueCompletion(Completion* completion, int32_t priority) {
    if (!s_queues[priority]->size()) {
        completion->m_result = 0;
        completion->m_done = false;
        completion->m_success = false;
        return false;
    }

    FILE_THREAD_CMD command;
    command.m_fs = nullptr;
    command.m_type = 2;
    command.m_file = nullptr;
    command.m_range = nullptr;
    command.m_completion = completion;

    Blizzard::Lock::MutexEnter(s_queueLock);
    s_queues[priority]->push_back(command);
    Blizzard::Lock::MutexLeave(s_queueLock);
    Blizzard::Lock::EventSet(s_queueEvent);

    return true;
}

bool Blizzard::Streaming::MakeLocalAsync(Blizzard::File::StreamRecord* file, int64_t offset, int32_t size, int32_t priority, const char* name) {
    if (!file->m_streaming || !file->m_streaming->m_entry) {
        return false;
    }

    Blizzard::Util::Digest<16> digest;
    HashFileName(&digest, name);

    DigestStateMap::iterator it = s_digestStates->find(digest);

    if (it != s_digestStates->end()) {
        if (it->second.m_state == 1) {
            return true;
        }

        if (it->second.m_state == 2 && it->second.m_priority <= priority) {
            return true;
        }
    }

    StorageEntry* entry = file->m_streaming->m_entry;

    if (!entry) {
        return false;
    }

    int32_t count;
    LoadRange* ranges = Blizzard::Storage::GetRange(entry, offset, size, &count, true);

    if (!ranges || !count) {
        return false;
    }

    bool queued = false;

    for (int32_t i = 0; i < count; i++) {
        if (static_cast<uint32_t>(ranges[i].m_state - 2) <= 2) {
            QueueLoad(file, priority, &ranges[i], nullptr);
            queued = true;
        }
    }

    if (queued) {
        (*s_digestStates)[digest].m_state = 2;
        (*s_digestStates)[digest].m_priority = priority;

        if (s_useBackgroundQueues) {
            Blizzard::Lock::EventSet(s_queueEvent);
        }
    } else {
        (*s_digestStates)[digest].m_state = 1;
        AddDownloaded(size);
    }

    Blizzard::Memory::Free(ranges);
    return true;
}

int32_t Blizzard::Streaming::CheckFileRange(const char* path, int64_t offset, int32_t size, const char* name, int32_t flags) {
    Blizzard::Util::Digest<16> digest;
    HashFileName(&digest, name);

    DigestStateMap::iterator it = s_digestStates->find(digest);
    int32_t previous = 0;

    if (it != s_digestStates->end()) {
        previous = it->second.m_state;

        if (previous == 1) {
            return 1;
        }
    }

    if (!path) {
        return -1;
    }

    std::string key = ResolveFilePath(std::string(path));

    if (!Blizzard::Storage::s_storage) {
        return -1;
    }

    std::map<std::string, StorageEntry>::iterator entry = Blizzard::Storage::s_storage->find(key);

    if (entry == Blizzard::Storage::s_storage->end()) {
        return -1;
    }

    int32_t count;
    LoadRange* ranges = Blizzard::Storage::GetRange(&entry->second, offset, size, &count, false);

    if (!ranges || !count) {
        return -1;
    }

    int32_t result = 1;

    for (LoadRange* range = ranges; count > 0 && result; range++, count--) {
        switch (range->m_state) {
            case 1:
            case 3:
                if (result == 1) {
                    result = 2;
                }

                break;

            case 2:
            case 4:
                result = 0;
                break;
        }
    }

    Blizzard::Memory::Free(ranges);

    if (previous != result) {
        (*s_digestStates)[digest].m_state = result;
    }

    return result;
}

int32_t Blizzard::Streaming::GetIsLocalAmount(const char* path, int64_t offset, int32_t size, const char* name) {
    Blizzard::Util::Digest<16> digest;
    HashFileName(&digest, name);

    DigestStateMap::iterator it = s_digestStates->find(digest);

    if (it != s_digestStates->end() && it->second.m_state == 1) {
        return size;
    }

    std::string key = ResolveFilePath(std::string(path));

    if (!Blizzard::Storage::s_storage) {
        return size;
    }

    std::map<std::string, StorageEntry>::iterator entry = Blizzard::Storage::s_storage->find(key);

    if (entry == Blizzard::Storage::s_storage->end()) {
        return size;
    }

    int32_t local = size;
    int32_t count;
    LoadRange* ranges = Blizzard::Storage::GetRange(&entry->second, offset, size, &count, false);

    if (ranges && count) {
        local = 0;

        for (int32_t i = 0; i < count; i++) {
            if (!ranges[i].m_state) {
                local += ranges[i].m_size;
            }
        }

        Blizzard::Memory::Free(ranges);
    }

    return local;
}

bool Blizzard::Streaming::MakeLocalBlocking(Blizzard::File::StreamRecord* file, int64_t offset, int32_t size, int32_t priority, const char* name) {
    Blizzard::Util::Digest<16> digest;
    HashFileName(&digest, name);

    DigestStateMap::iterator it = s_digestStates->find(digest);

    if (it != s_digestStates->end() && it->second.m_state == 1) {
        return true;
    }

    std::string key = ResolveFilePath(std::string(file->m_pathname));

    if (!Blizzard::Storage::s_storage) {
        return false;
    }

    std::map<std::string, StorageEntry>::iterator entry = Blizzard::Storage::s_storage->find(key);

    if (entry == Blizzard::Storage::s_storage->end()) {
        return false;
    }

    int32_t count;
    LoadRange* ranges = Blizzard::Storage::GetRange(&entry->second, offset, size, &count, true);

    if (!ranges || !count) {
        return false;
    }

    std::vector<Completion*> completions;
    bool queued = false;

    for (int32_t i = 0; i < count; i++) {
        LoadRange& range = ranges[i];

        if (static_cast<uint32_t>(range.m_state - 2) > 2) {
            continue;
        }

        if (range.m_blockSize <= 0) {
            Blizzard::Memory::Free(ranges);
            return false;
        }

        LoadRange request;
        request.m_state = 2;
        request.m_blockOffset = range.m_blockOffset;
        request.m_blockSize = range.m_blockSize;
        queued = true;

        Completion* completion = new Completion();
        Blizzard::Lock::Event* event = new Blizzard::Lock::Event();
        completion->m_event = event;
        Blizzard::Lock::EventCreate(*event);
        completions.push_back(completion);

        QueueLoad(file, priority, &request, completion);
        Blizzard::Lock::EventSet(s_throttleEvent);
        Blizzard::Lock::EventSet(s_queueEvent);
    }

    if (!queued) {
        Blizzard::Memory::Free(ranges);
        return true;
    }

    Blizzard::Lock::EventSet(s_throttleEvent);
    Blizzard::Lock::EventSet(s_queueEvent);

    bool success = true;

    for (std::vector<Completion*>::iterator it = completions.begin(); it != completions.end(); ++it) {
        Blizzard::Lock::EventWaitForever(*(*it)->m_event);
        Blizzard::Lock::EventDestroy(*(*it)->m_event);

        if (!(*it)->m_success) {
            success = false;
        }

        delete (*it)->m_event;
        (*it)->m_event = nullptr;
        delete *it;
        *it = nullptr;
    }

    completions.erase(completions.begin(), completions.end());
    Blizzard::Memory::Free(ranges);

    return success;
}

bool System_File::Stacked::stream_flush(Blizzard::File::Filesystem* fs, FileParms* parms) {
    Blizzard::File::StreamRecord* file = parms->m_stream;

    if (file->m_streaming && file->m_streaming->m_entry) {
        Blizzard::Lock::MutexEnter(s_queueLock);

        for (int32_t i = 0; i < 7; i++) {
            std::deque<Blizzard::Streaming::FILE_THREAD_CMD>* queue = s_queues[i];

            if (queue->size()) {
                for (std::deque<Blizzard::Streaming::FILE_THREAD_CMD>::iterator it = queue->begin(); it != queue->end(); ++it) {
                    if (file == it->m_file && it->m_type == 1) {
                        it->m_type = 0;
                    }
                }
            }
        }

        for (int32_t i = 0; i < 2; i++) {
            if (s_currentCommands[i] && file == s_currentCommands[i]->m_file && s_currentCommands[i]->m_type == 1) {
                s_currentCommands[i]->m_type = 0;
            }
        }

        Blizzard::Lock::MutexLeave(s_queueLock);

        if (!Blizzard::Storage::FlushFile(fs->m_next, file)) {
            return false;
        }
    }

    if (!fs->m_next) {
        return false;
    }

    return (*reinterpret_cast<FileFunc*>(reinterpret_cast<uintptr_t>(fs->m_next) + parms->m_verb))(fs->m_next, parms);
}

bool Blizzard::Streaming::LoadFile(Blizzard::File::Filesystem* fs, Blizzard::File::StreamRecord* file, LoadRange* range) {
    Blizzard::Storage::StorageEntryRef ref(file->m_streaming->m_entry);
    StorageEntry* entry = ref.m_entry;

    if (!entry) {
        Blizzard::Lock::MutexEnter(Blizzard::Storage::s_storageLock);
        Blizzard::Lock::MutexLeave(Blizzard::Storage::s_storageLock);
        return false;
    }

    int32_t count = 0;
    LoadRange* ranges = file->m_streaming ? Blizzard::Storage::GetRange(entry, range->m_blockOffset, range->m_blockSize, &count, true) : nullptr;

    if (!ranges || !count) {
        Blizzard::Lock::MutexEnter(Blizzard::Storage::s_storageLock);
        entry->m_refCount--;
        Blizzard::Lock::MutexLeave(Blizzard::Storage::s_storageLock);
        return false;
    }

    bool requested = false;
    int32_t failState = 3;

    Blizzard::Lock::MutexEnter(Blizzard::Storage::s_storageLock);

    for (int32_t i = 0; i < count; i++) {
        LoadRange& loadRange = ranges[i];

        if (loadRange.m_state == 3) {
            failState = 1;
            Blizzard::Storage::RequestRange(entry, loadRange.m_blockOffset, loadRange.m_blockSize, 4);
            requested = true;
        } else if (loadRange.m_state == 2 || loadRange.m_state == 4) {
            failState = 0;
            Blizzard::Storage::RequestRange(entry, loadRange.m_blockOffset, loadRange.m_blockSize, 4);
            requested = true;
        }
    }

    Blizzard::Lock::MutexLeave(Blizzard::Storage::s_storageLock);
    Blizzard::Memory::Free(ranges);

    if (!requested) {
        Blizzard::Log::Printf(3, "Logs/Profiling_File_Defer_Cancelled.log", "%10lld, %10d - %s\n", range->m_blockOffset, range->m_blockSize, file->m_pathname);
        Blizzard::Lock::MutexEnter(Blizzard::Storage::s_storageLock);
        entry->m_refCount--;
        Blizzard::Lock::MutexLeave(Blizzard::Storage::s_storageLock);
        return true;
    }

    Blizzard::Log::Printf(3, "Logs/Profiling_File_Defer.log", "%10lld, %10d - %s\n", range->m_blockOffset, range->m_blockSize, file->m_pathname);
    s_statBytesDownloaded += range->m_blockSize;

    uint8_t* data = static_cast<uint8_t*>(Blizzard::Memory::Allocate(range->m_blockSize));

    std::string url;
    url = ManifestInfo::GetRemotePath(file->m_streaming->m_path);

    Manifests::TransportItem* transport = ManifestInfo::GetTransportItem(file->m_streaming->m_path);
    int32_t maxRetry = ManifestInfo::GetServerItemMaxRetry(std::string(url), file->m_streaming->m_path);

    bool result = Transport::Read(transport, std::string(url), maxRetry, data, file->m_info.m_size, range->m_blockOffset, range->m_blockSize);

    if (!result) {
        ManifestInfo::DeactivateServer(std::string(url), file->m_streaming->m_path);
        Blizzard::Memory::Free(data);
    } else if (range->m_blockSize > Blizzard::Storage::s_blockSize) {
        int32_t written = 0;
        int32_t left = range->m_blockSize;

        while (left > 0) {
            int32_t chunk = Blizzard::Storage::s_blockSize < left ? Blizzard::Storage::s_blockSize : left;
            result &= Blizzard::Storage::QueueWrite(fs, file, &data[written], range->m_blockOffset + written, chunk, false);
            written += chunk;
            left -= chunk;
        }

        Blizzard::Memory::Free(data);
    } else {
        result = Blizzard::Storage::QueueWrite(fs, file, data, range->m_blockOffset, range->m_blockSize, true);
    }

    if (!result) {
        Blizzard::Storage::SetRangeAs(entry, range->m_blockOffset, range->m_blockSize, failState);
    }

    Blizzard::Lock::MutexEnter(Blizzard::Storage::s_storageLock);
    entry->m_refCount--;
    Blizzard::Lock::MutexLeave(Blizzard::Storage::s_storageLock);

    return result;
}

uint32_t Blizzard::Streaming::DownloadThread(void* param) {
    int32_t thread = *static_cast<int32_t*>(param);
    delete static_cast<int32_t*>(param);

    Blizzard::Lock::MutexEnter(s_queueLock);
    int32_t bit = 1 << thread;
    s_threadMask |= bit;
    Blizzard::Lock::MutexLeave(s_queueLock);

    if (!s_stopThreads) {
        for (;;) {
            if (Blizzard::Lock::EventWait(100, s_queueEvent) || Blizzard::Lock::EventWait(5, s_threadEvents[thread])) {
                while (!s_stopThreads) {
                    if (s_paused) {
                        Blizzard::Lock::EventWaitForever(s_pauseEvent);
                        Blizzard::Lock::EventSet(s_pauseEvent);
                    }

                    Blizzard::Lock::MutexEnter(s_queueLock);
                    s_busyMask |= bit;

                    FILE_THREAD_CMD* command = nullptr;
                    int32_t queue = GetNextCommand(&command, thread);

                    if (queue == 7) {
                        Blizzard::Lock::MutexLeave(s_queueLock);
                        break;
                    }

                    int32_t pending = 0;

                    for (int32_t i = 0; i < 7; i++) {
                        pending += static_cast<int32_t>(s_queues[i]->size());
                    }

                    if (pending > 0) {
                        int32_t idle = GetIdleThread();

                        if (idle >= 0) {
                            Blizzard::Lock::EventSet(s_threadEvents[idle]);
                        }
                    }

                    Blizzard::Lock::MutexLeave(s_queueLock);

                    bool success = true;
                    bool requeue = false;

                    if (!command->m_type) {
                        success = false;
                    } else if (command->m_type == 1) {
                        if (LoadFile(command->m_fs, command->m_file, command->m_range)) {
                            command->m_type = 0;
                        } else if (command->m_type && !command->m_completion) {
                            requeue = true;
                            success = false;
                        }
                    } else if (command->m_type == 2) {
                        command->m_type = 0;

                        if (!pending) {
                            WaitForLoads();
                        }
                    }

                    if (command->m_completion) {
                        command->m_completion->m_result = static_cast<int32_t>(s_queues[queue]->size());
                        command->m_completion->m_done = false;
                        command->m_completion->m_success = success;
                        Blizzard::Lock::EventSet(*command->m_completion->m_event);
                        command->m_completion = nullptr;
                    }

                    Blizzard::Lock::MutexEnter(s_queueLock);

                    if (requeue) {
                        s_queues[queue]->push_front(*command);
                        AddStreamed(0);
                    }

                    FILE_THREAD_CMD* current = s_currentCommands[thread];

                    if (current) {
                        s_currentCommands[thread] = nullptr;
                        Blizzard::Memory::Free(current->m_range);
                        delete current;
                        s_commandsFreed++;
                    }

                    Blizzard::Lock::MutexLeave(s_queueLock);

                    System_File::FileError& error = System_File::s_error;

                    if (error.m_errorStack) {
                        Blizzard::Debug::FreeErrorStack(error.m_errorStack);
                    }

                    error.m_errorStack = nullptr;

                    if (queue > 2) {
                        Blizzard::Lock::EventWait(FileManifest::GetBGPreloadSleep(), s_throttleEvent);
                    }
                }
            }

            Blizzard::Lock::MutexEnter(s_queueLock);
            s_busyMask &= ~bit;
            Blizzard::Lock::MutexLeave(s_queueLock);

            if (s_stopThreads) {
                break;
            }
        }
    }

    Blizzard::Lock::MutexEnter(s_queueLock);
    s_threadMask &= ~bit;
    Blizzard::Lock::MutexLeave(s_queueLock);

    return 1;
}

bool Blizzard::Streaming::InitializeStreaming(const char* manifest, const char* storagePath, const char* variant, const char* dataPath, bool required) {
    if (s_streamingReady) {
        return true;
    }

    char cwd[260];
    cwd[0] = '\0';

    if (!Blizzard::File::GetWorkingDirectory(cwd, sizeof(cwd))) {
        Blizzard::File::AddToLastErrorStack(0, "InitializeStreaming - Failed to get working directory", 20);
        return false;
    }

    char root[260];

    if (!manifest || Blizzard::String::EqualI(manifest, s_httpPrefix, s_httpPrefix ? strlen(s_httpPrefix) : 0)) {
        Blizzard::String::Copy(root, cwd, sizeof(root));
    } else {
        char absolute[260];

        if (!Blizzard::File::MakeAbsolutePath(manifest, absolute, sizeof(absolute), false)) {
            Blizzard::File::AddToLastErrorStack(0, "InitializeStreaming - Failed to make absolute manifest path", 20);
            return false;
        }

        int32_t length = static_cast<int32_t>(Blizzard::String::FindFilename(absolute) - absolute + 1);
        Blizzard::String::Copy(root, absolute, length <= 260 ? length : 260);
    }

    if (dataPath && (!Blizzard::File::MakeAbsolutePath(dataPath, root, sizeof(root), false) || !Blizzard::File::IsDirectory(dataPath))) {
        Blizzard::File::AddToLastErrorStack(0, "InitializeStreaming - Invalid data path specified", 20);
    }

    if (variant && *variant) {
        Blizzard::String::Copy(Blizzard::Storage::s_variantName, variant, sizeof(Blizzard::Storage::s_variantName));
    }

    if (!Blizzard::File::SetWorkingDirectory(root)) {
        Blizzard::File::AddToLastErrorStack(0, "InitializeStreaming - Failed to change working directory", 20);
        return false;
    }

    s_paths = new PathMap();

    bool manifestLoaded = ManifestInfo::Initialize(manifest);
    s_isStreamingTrial = FileManifest::GetIsStreamingTrial();

    if (!manifestLoaded) {
        Blizzard::File::SetWorkingDirectory(cwd);
        Blizzard::File::AddToLastErrorStack(0, "InitializeStreaming - Could not load manifest", 20);
        return false;
    }

    bool storage = true;

    if (storagePath && *storagePath) {
        if (!Blizzard::Storage::Initialize(storagePath, required)) {
            if (required) {
                storage = false;
            } else {
                storage = Blizzard::Storage::Initialize();
                Blizzard::File::Delete(storagePath);
            }
        }
    } else {
        storage = Blizzard::Storage::Initialize();
    }

    if (!storage) {
        Blizzard::File::SetWorkingDirectory(cwd);
        Blizzard::File::AddToLastErrorStack(0, "InitializeStreaming - Could not initialize storage", 20);
        return false;
    }

    s_digestStates = new DigestStateMap();

    for (int32_t i = 0; i < 7; i++) {
        s_queues[i] = new std::deque<FILE_THREAD_CMD>();
    }

    Blizzard::Lock::MutexCreate(s_queueLock);
    Blizzard::Lock::EventCreate(s_queueEvent);
    Blizzard::Lock::EventCreate(s_throttleEvent);

    for (int32_t i = 0; i < 2; i++) {
        s_currentCommands[i] = nullptr;
        Blizzard::Lock::EventCreate(s_threadEvents[i]);

        int32_t* thread = new int32_t;
        *thread = i;
        Blizzard::Thread::CreateDetached(DownloadThread, thread, Blizzard::String::QuickFormat<1024>("Stream BG DL %d", i).m_buffer);
    }

    s_paused = false;
    Blizzard::Lock::MutexCreate(s_pauseLock);
    Blizzard::Lock::EventCreate(s_pauseEvent);

    System_File::Stacked::PushStreaming();
    s_streamingReady = true;

    if (Blizzard::File::SetWorkingDirectory(cwd)) {
        return true;
    }

    Blizzard::File::AddToLastErrorStack(0, "InitializeStreaming - Failed to restore working directory", 20);
    return false;
}

char* GetComponentVersionData(uint32_t* size) {
    *size = 0;

    return nullptr;
}

std::string Blizzard::Streaming::ResolveFilePath(const std::string& path) {
    if (!s_paths) {
        return std::string();
    }

    if ((*s_paths)[path].size()) {
        return (*s_paths)[path];
    }

    char absolute[260];

    if (!Blizzard::File::MakeAbsolutePath(path.c_str(), absolute, 260, false)) {
        return std::string();
    }

    char normalized[260];
    char* dst = normalized;
    const char* src = absolute;
    uint32_t left = 260;

    while (true) {
        char c = *src++;
        left--;

        if (!c) {
            *dst = '\0';
            break;
        }

        *dst++ = c == '/' ? '\\' : c;

        if (!left) {
            dst[-1] = '\0';
            break;
        }
    }

    StringToLower(normalized);
    (*s_paths)[path].assign(normalized, strlen(normalized));
    return std::string((*s_paths)[path]);
}

bool Blizzard::Streaming::IsStreamedFile(const char* path) {
    if (!Blizzard::Streaming::s_streamingReady || !path) {
        return false;
    }

    std::string resolved = Blizzard::Streaming::ResolveFilePath(std::string(path));

    if (!Blizzard::Storage::s_storage) {
        return false;
    }

    return Blizzard::Storage::s_storage->find(resolved) != Blizzard::Storage::s_storage->end();
}

void StopStreaming() {
    if (Blizzard::Streaming::s_streamingReady) {
        Blizzard::Streaming::s_progress.FireCallback();
        s_stopThreads = true;
        Blizzard::Lock::EventSet(s_queueEvent);
        Blizzard::Lock::EventSet(s_throttleEvent);
        s_paused = false;
        Blizzard::Lock::EventSet(s_pauseEvent);

        while (s_threadMask > 0) {
            Blizzard::Process::Sleep(10);
        }

        Blizzard::Lock::EventDestroy(s_queueEvent);
        Blizzard::Lock::EventDestroy(s_throttleEvent);

        for (uint32_t i = 0; i < sizeof(s_threadEvents) / sizeof(s_threadEvents[0]); i++) {
            Blizzard::Lock::EventDestroy(s_threadEvents[i]);
            delete s_currentCommands[i];
        }

        Blizzard::Lock::MutexDestroy(s_queueLock);
        Blizzard::Lock::EventDestroy(s_pauseEvent);
        Blizzard::Lock::MutexDestroy(s_pauseLock);

        delete s_digestStates;

        for (uint32_t i = 0; i < sizeof(s_queues) / sizeof(s_queues[0]); i++) {
            delete s_queues[i];
        }

        delete s_paths;
        s_paths = nullptr;
        Blizzard::Streaming::s_streamingReady = false;
    }

    Blizzard::Storage::Shutdown();

    delete Blizzard::Streaming::ManifestInfo::s_instance;
    Blizzard::Streaming::ManifestInfo::s_instance = nullptr;
}

#endif
