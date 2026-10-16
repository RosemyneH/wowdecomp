#if defined(__linux__) || defined(__APPLE__)

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <ctime>
#include <cstring>
#include <pthread.h>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include "Engine/Source/BlizzardCore/Source/System/File/File.h"
#include "Engine/Source/BlizzardCore/Source/System/Memory/TempBuffer.h"
#include "Engine/Source/BlizzardCore/Source/System/Process/Process.h"
#include "Engine/Source/BlizzardCore/Source/System/String/Path.h"
#include "Engine/Source/BlizzardCore/Source/System/String/String.h"

extern char** environ;

struct ChildProcess {
    pid_t m_process;
    void (*m_callback)(void*);
    void* m_param;
};

static void* ChildWaiter(void* param) {
    ChildProcess* child = static_cast<ChildProcess*>(param);
    int status;

    while (waitpid(child->m_process, &status, 0) < 0 && errno == EINTR) {
    }

    child->m_callback(child->m_param);
    delete child;

    return nullptr;
}

bool Blizzard::Process::GetProcessPath(char* buffer, int32_t buffersize) {
    if (!buffer || buffersize <= 0) {
        return false;
    }

    char filename[MAX_PATH];
    ssize_t length = readlink("/proc/self/exe", filename, sizeof(filename) - 1);
    filename[length > 0 ? length : 0] = '\0';
    System_File::FromNativeName(buffer, buffersize, filename);

    return true;
}

bool Blizzard::Process::GetProcessDirectory(char* buffer, int32_t buffersize) {
    Blizzard::Memory::TempBuffer<char, 1024> path(buffersize);

    if (!GetProcessPath(path.m_data, buffersize)) {
        return false;
    }

    if (path.m_data) {
        if (buffer) {
            int32_t length = static_cast<int32_t>(Blizzard::String::FindFilename(path.m_data) - path.m_data + 1);
            Blizzard::String::Copy(buffer, path.m_data, std::min(length, buffersize));
        }
    } else if (buffer && buffersize > 0) {
        *buffer = '\0';
    }

    return true;
}

bool Blizzard::Process::LaunchChildProcessWithArguments(const char* path, const char* arguments, void (*callback)(void*), void* param) {
    if (!arguments) {
        arguments = "";
    }

    std::vector<std::string> args;
    args.emplace_back(path);

    while (*arguments) {
        size_t length = strcspn(arguments, "\n");
        args.emplace_back(arguments, length);
        arguments += length;
        arguments += strspn(arguments, "\n");
    }

    std::vector<char*> argv;

    for (std::string& arg : args) {
        argv.push_back(&arg[0]);
    }

    argv.push_back(nullptr);

    pid_t pid;

    if (posix_spawn(&pid, path, nullptr, nullptr, argv.data(), environ)) {
        return false;
    }

    if (!callback) {
        return true;
    }

    ChildProcess* child = new ChildProcess;
    child->m_process = pid;
    child->m_callback = callback;
    child->m_param = param;

    pthread_t thread;

    if (pthread_create(&thread, nullptr, ChildWaiter, child)) {
        delete child;
        return false;
    }

    pthread_detach(thread);

    return true;
}

void Blizzard::Process::Sleep(uint32_t milliseconds) {
    timespec remaining;
    remaining.tv_sec = milliseconds / 1000;
    remaining.tv_nsec = static_cast<long>(milliseconds % 1000) * 1000000L;
    while (nanosleep(&remaining, &remaining) != 0 && errno == EINTR) {
    }
}

#endif
