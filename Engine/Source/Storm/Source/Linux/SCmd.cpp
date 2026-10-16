#if defined(__linux__) || defined(__APPLE__)

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(__APPLE__)
#include <crt_externs.h>
#endif
#include "Engine/Source/Storm/Source/SCmd.h"
#include "Engine/Source/Storm/H/stpl.h"
#include "Engine/Source/Storm/H/storm.h"

#define SCMD_ERROR_OPEN_FAILED 110

static const char* const s_errorstr[3] = {
    "Invalid argument: %s",
    "The syntax of the command is incorrect.",
    "Unable to open response file: %s",
};

void GenerateError(void (STORMAPI* errorcallback)(_CMDERROR*), uint32_t errorcode, const char* itemstring) {
    uint32_t index;

    switch (errorcode) {
        case STORM_ERROR_BAD_ARGUMENT:
            index = 0;
            break;
        case STORM_ERROR_NOT_ENOUGH_ARGUMENTS:
            index = 1;
            break;
        case SCMD_ERROR_OPEN_FAILED:
            index = 2;
            break;
        default:
            return;
    }

    char errorstr[256];

    if (strstr(s_errorstr[index], "%s")) {
        snprintf(errorstr, sizeof(errorstr), s_errorstr[index], itemstring);
    } else {
        SStrCopy(errorstr, s_errorstr[index], sizeof(errorstr));
    }

    if (errorstr[0]) {
        SStrPack(errorstr, "\n", sizeof(errorstr));
    }

    SErrSetLastError(errorcode);

    _CMDERROR data;
    data.errorcode = errorcode;
    data.itemstr = itemstring;
    data.errorstr = errorstr;
    errorcallback(&data);
}

int32_t ProcessFile(const char* filename, _PROCESSING* processing, CMDDEF** nextarg, int32_t (STORMAPI* extracallback)(const char*), void (STORMAPI* errorcallback)(_CMDERROR*)) {
    FILE* file = fopen(filename, "rb");

    if (!file) {
        if (errorcallback) {
            GenerateError(errorcallback, SCMD_ERROR_OPEN_FAILED, filename);
        }

        return 0;
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (size < 0) {
        fclose(file);
        return 0;
    }

    char* data = static_cast<char*>(SMemAlloc(static_cast<size_t>(size) + 1, __FILE__, __LINE__, 0));

    if (!data) {
        fclose(file);
        return 0;
    }

    size_t bytesread = fread(data, 1, static_cast<size_t>(size), file);
    fclose(file);
    data[bytesread] = 0;

    const char* curr = data;
    int32_t result = ProcessString(&curr, processing, nextarg, extracallback, errorcallback);
    SMemFree(data, __FILE__, __LINE__, 0);

    return result;
}

static const char* GetCommandLine() {
    static char cmdline[4096];

    if (cmdline[0]) {
        return cmdline;
    }

#if defined(__APPLE__)
    int argc = *_NSGetArgc();
    char** argv = *_NSGetArgv();

    for (int i = 0; i < argc; i++) {
        if (!argv[i]) {
            continue;
        }

        bool quote = strpbrk(argv[i], " \t") != nullptr;

        if (cmdline[0]) {
            SStrPack(cmdline, " ", sizeof(cmdline));
        }

        if (quote) {
            SStrPack(cmdline, "\"", sizeof(cmdline));
        }

        SStrPack(cmdline, argv[i], sizeof(cmdline));

        if (quote) {
            SStrPack(cmdline, "\"", sizeof(cmdline));
        }
    }

    return cmdline;
#else
    FILE* file = fopen("/proc/self/cmdline", "rb");

    if (!file) {
        return cmdline;
    }

    char raw[4096];
    size_t length = fread(raw, 1, sizeof(raw) - 1, file);
    fclose(file);
    raw[length] = 0;

    for (size_t pos = 0; pos < length; pos += strlen(&raw[pos]) + 1) {
        const char* arg = &raw[pos];
        bool quote = strpbrk(arg, " \t") != nullptr;

        if (pos) {
            SStrPack(cmdline, " ", sizeof(cmdline));
        }

        if (quote) {
            SStrPack(cmdline, "\"", sizeof(cmdline));
        }

        SStrPack(cmdline, arg, sizeof(cmdline));

        if (quote) {
            SStrPack(cmdline, "\"", sizeof(cmdline));
        }
    }

    return cmdline;
#endif
}

int32_t STORMAPI SCmdProcessCommandLine(int32_t (STORMAPI* extracallback)(const char*), void (STORMAPI* errorcallback)(_CMDERROR*)) {
    return SCmdProcess(GetCommandLine(), 1, extracallback, errorcallback);
}

#endif
