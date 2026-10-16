#if defined(__linux__) || defined(__APPLE__)

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "Engine/Source/BlizzardCore/Source/System/Debug/Debug.h"
#include "Engine/Source/BlizzardCore/Source/System/File/File.h"
#include "Engine/Source/BlizzardCore/Source/System/Process/Process.h"
#include "Engine/Source/BlizzardCore/Source/System/String/Path.h"
#include "Engine/Source/BlizzardCore/Source/System/String/String.h"
#include "Engine/Source/BlizzardCore/Source/System/Time/Time.h"

static bool s_continueAfterAssert;

static bool IsDebuggerAttached() {
    FILE* status = fopen("/proc/self/status", "r");

    if (!status) {
        return false;
    }

    char line[256];
    bool attached = false;

    while (fgets(line, sizeof(line), status)) {
        if (!strncmp(line, "TracerPid:", 10)) {
            attached = atoi(line + 10) != 0;
            break;
        }
    }

    fclose(status);

    return attached;
}

void Blizzard::Debug::Breakpoint() {
    if (IsDebuggerAttached()) {
        raise(SIGTRAP);
    }
}

void Blizzard::Debug::DefaultCallback(const char* expression, const char* message, const char* fileName, uint32_t lineNumber) {
    char text[2048];
    char canonical[256];
    char directory[512];
    Blizzard::Time::TimeRec rec;

    Blizzard::Time::BreakTime(Blizzard::Time::GetTimestamp(), rec);
    Blizzard::String::Format(text, sizeof(text), "%04d/%02d/%02d %02d:%02d:%02d  %s(%d) : Assertion failed: %s  \"%s\"\n", rec.year, rec.month, rec.day, rec.hour, rec.minute, rec.second, CanonicalStr(fileName, canonical, sizeof(canonical)), lineNumber, expression, message);
    fprintf(stderr, "%s", text);

    Blizzard::Process::GetProcessDirectory(directory, sizeof(directory));
    Blizzard::String::Append(directory, "Errors", sizeof(directory));
    Blizzard::File::CreateDirectory(directory, true);
    Blizzard::String::Append(directory, "/Assert.log", sizeof(directory));

    FILE* stream = fopen(directory, "a");

    if (stream) {
        fprintf(stream, "%s", text);
        fclose(stream);
    }

    Breakpoint();

    if (!s_continueAfterAssert) {
        abort();
    }
}

void System_Debug::ShutdownProc() {
}

#endif
