#if defined(__linux__) || defined(__APPLE__)

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <execinfo.h>
#if defined(__APPLE__)
#include <climits>
#include <mach-o/dyld.h>
#endif
#include <pwd.h>
#include <sys/time.h>
#include <unistd.h>
#include "Engine/Source/Storm/H/storm.h"
#include "Engine/Source/Storm/Source/MachineState.h"

struct MEMDUMP {
    void (*logLineProc)(void*, const char*, ...);
    void* logLineProcParam;
};

static const char s_monthNames[12][4] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

int32_t g_memFullError;

static int32_t sgRecursionLevel;

static void sLogSeparatorLine(void (*logLineProc)(void*, const char*, ...), void* logLineProcParam, char character, int32_t longLine) {
    char line[80];
    uint32_t length = longLine ? 78 : 40;
    memset(line, character, length);
    line[length] = 0;
    logLineProc(logLineProcParam, "%s", line);
}

static void sLogHeader(void (*logLineProc)(void*, const char*, ...), void* logLineProcParam, const char* headerString) {
    logLineProc(logLineProcParam, "");
    sLogSeparatorLine(logLineProc, logLineProcParam, '-', 0);
    logLineProc(logLineProcParam, "    %s", headerString);
    sLogSeparatorLine(logLineProc, logLineProcParam, '-', 0);
    logLineProc(logLineProcParam, "");
}

static void sLogExeFile(void (*logLineProc)(void*, const char*, ...), void* logLineProcParam) {
    char exeFullPath[MAX_PATH];

#if defined(__APPLE__)
    char executable[PATH_MAX];
    char resolved[PATH_MAX];
    uint32_t length = sizeof(executable);

    if (_NSGetExecutablePath(executable, &length)) {
        exeFullPath[0] = 0;
    } else {
        const char* path = realpath(executable, resolved);
        SStrCopy(exeFullPath, path ? path : executable, sizeof(exeFullPath));
    }
#else
    ssize_t length = readlink("/proc/self/exe", exeFullPath, sizeof(exeFullPath) - 1);
    exeFullPath[length > 0 ? length : 0] = 0;
#endif

    logLineProc(logLineProcParam, "%-10s%s", "Exe:", exeFullPath);
}

static void sLogDateTimeString(void (*logLineProc)(void*, const char*, ...), void* logLineProcParam, const SYSTEMTIME* time) {
    uint16_t hour = time->wHour;
    char ampm = hour < 12 ? 'A' : 'P';

    if (hour > 12) {
        hour -= 12;
    } else if (!hour) {
        hour = 12;
    }

    logLineProc(logLineProcParam, "%-10s%3s %2d, %4d %2d:%02d:%02d.%03d %cM", "Time:", s_monthNames[time->wMonth - 1], time->wDay, time->wYear, hour, time->wMinute, time->wSecond, time->wMilliseconds, ampm);
}

static void sLogUserName(void (*logLineProc)(void*, const char*, ...), void* logLineProcParam) {
    passwd* entry = getpwuid(geteuid());
    logLineProc(logLineProcParam, "%-10s%s", "User:", entry && entry->pw_name ? entry->pw_name : "<unknown>");
}

static void sLogComputerName(void (*logLineProc)(void*, const char*, ...), void* logLineProcParam) {
    char computerName[256];

    if (gethostname(computerName, sizeof(computerName))) {
        strcpy(computerName, "<unknown>");
    }

    computerName[sizeof(computerName) - 1] = 0;
    logLineProc(logLineProcParam, "%-10s%s", "Computer:", computerName);
}

static void sLogRegisters(void (*logLineProc)(void*, const char*, ...), void* logLineProcParam, const CONTEXT* context) {
#if defined(__linux__) && defined(__x86_64__)
    const greg_t* regs = context->uc_mcontext.gregs;
    sLogHeader(logLineProc, logLineProcParam, "Registers");
    logLineProc(logLineProcParam, "RAX=%016llX  RBX=%016llX  RCX=%016llX  RDX=%016llX", regs[REG_RAX], regs[REG_RBX], regs[REG_RCX], regs[REG_RDX]);
    logLineProc(logLineProcParam, "RSI=%016llX  RDI=%016llX  RBP=%016llX  RSP=%016llX", regs[REG_RSI], regs[REG_RDI], regs[REG_RBP], regs[REG_RSP]);
    logLineProc(logLineProcParam, "R8 =%016llX  R9 =%016llX  R10=%016llX  R11=%016llX", regs[REG_R8], regs[REG_R9], regs[REG_R10], regs[REG_R11]);
    logLineProc(logLineProcParam, "R12=%016llX  R13=%016llX  R14=%016llX  R15=%016llX", regs[REG_R12], regs[REG_R13], regs[REG_R14], regs[REG_R15]);
    logLineProc(logLineProcParam, "RIP=%016llX  FLG=%08llX", regs[REG_RIP], regs[REG_EFL]);
    logLineProc(logLineProcParam, "");
#elif defined(__linux__) && defined(__aarch64__)
    const unsigned long long int* regs = context->uc_mcontext.regs;
    sLogHeader(logLineProc, logLineProcParam, "Registers");
    logLineProc(logLineProcParam, "X0 =%016llX  X1 =%016llX  X2 =%016llX  X3 =%016llX", regs[0], regs[1], regs[2], regs[3]);
    logLineProc(logLineProcParam, "X4 =%016llX  X5 =%016llX  X6 =%016llX  X7 =%016llX", regs[4], regs[5], regs[6], regs[7]);
    logLineProc(logLineProcParam, "X8 =%016llX  X9 =%016llX  X10=%016llX  X11=%016llX", regs[8], regs[9], regs[10], regs[11]);
    logLineProc(logLineProcParam, "X12=%016llX  X13=%016llX  X14=%016llX  X15=%016llX", regs[12], regs[13], regs[14], regs[15]);
    logLineProc(logLineProcParam, "X16=%016llX  X17=%016llX  X18=%016llX  X19=%016llX", regs[16], regs[17], regs[18], regs[19]);
    logLineProc(logLineProcParam, "X20=%016llX  X21=%016llX  X22=%016llX  X23=%016llX", regs[20], regs[21], regs[22], regs[23]);
    logLineProc(logLineProcParam, "X24=%016llX  X25=%016llX  X26=%016llX  X27=%016llX", regs[24], regs[25], regs[26], regs[27]);
    logLineProc(logLineProcParam, "X28=%016llX  X29=%016llX  X30=%016llX  SP =%016llX", regs[28], regs[29], regs[30], context->uc_mcontext.sp);
    logLineProc(logLineProcParam, "PC =%016llX  PSR=%016llX", context->uc_mcontext.pc, context->uc_mcontext.pstate);
    logLineProc(logLineProcParam, "");
#else
    (void)logLineProc;
    (void)logLineProcParam;
    (void)context;
#endif
}

static void sLogStackTrace(void (*logLineProc)(void*, const char*, ...), void* logLineProcParam, uint32_t stackFramesToSkip) {
    void* frames[128];
    int count = backtrace(frames, 128);
    char** symbols = backtrace_symbols(frames, count);

    sLogHeader(logLineProc, logLineProcParam, "Stack Trace");

    for (int i = static_cast<int>(stackFramesToSkip); i < count; ++i) {
        logLineProc(logLineProcParam, "%016llX %s", reinterpret_cast<unsigned long long>(frames[i]), symbols ? symbols[i] : "");
    }

    free(symbols);
}

static void sLogLoadedModules(void (*logLineProc)(void*, const char*, ...), void* logLineProcParam) {
    FILE* maps = fopen("/proc/self/maps", "r");

    sLogHeader(logLineProc, logLineProcParam, "Loaded Modules");

    if (!maps) {
        return;
    }

    char line[512];
    char lastPath[512] = "";

    while (fgets(line, sizeof(line), maps)) {
        line[strcspn(line, "\n")] = 0;

        const char* path = strchr(line, '/');

        if (!path || !strcmp(path, lastPath)) {
            continue;
        }

        SStrCopy(lastPath, path, sizeof(lastPath));
        logLineProc(logLineProcParam, "%s", line);
    }

    fclose(maps);
}

static void STORMAPI CmdMemOutput(void* hOutput, const MEMREPORTENTRY* entry) {
    MEMDUMP* dump = static_cast<MEMDUMP*>(hOutput);
    dump->logLineProc(dump->logLineProcParam, "%5d %5d %s(%d)", entry->unk_0010, entry->unk_0014, entry->unk_0024, entry->unk_0020 >> 1);
}

static void sShowOutOfMemory(void (*logLineProc)(void*, const char*, ...), void* logLineProcParam) {
    if (!g_memFullError) {
        return;
    }

    MEMDUMP dump;
    dump.logLineProc = logLineProc;
    dump.logLineProcParam = logLineProcParam;
    SMemGenerateReport(SMEM_REPORT_BY_CALLER, CmdMemOutput, &dump);
    sLogSeparatorLine(logLineProc, logLineProcParam, '-', 1);
}

void SMemGenerateReport(SMEMREPORTTYPE type, void (STORMAPI* callback)(void*, const MEMREPORTENTRY*), void* context) {
    StormCallService(STORM_SERVICE_GENERATEREPORT, type, callback, context);
}

void LoadMachineStateSymbols() {
}

void UnloadMachineStateSymbols() {
}

void LogComputerInfoHeader(uint32_t logOptions, void (*logLineProc)(void*, const char*, ...), void* logLineProcParam, const char* headerTitleLine, const SYSTEMTIME* time) {
    if (__atomic_add_fetch(&sgRecursionLevel, 1, __ATOMIC_SEQ_CST) == 1) {
        if (!logOptions || (logOptions & LOG_OPTION_DEFAULT)) {
            logOptions |= LOG_OPTION_EXEFILE | LOG_OPTION_DATETIME | LOG_OPTION_COMPUTERNAME | LOG_OPTION_USERNAME;

            if (headerTitleLine && *headerTitleLine) {
                logOptions |= LOG_OPTION_TITLE;
            }
        }

        sLogSeparatorLine(logLineProc, logLineProcParam, '=', 1);

        if ((logOptions & LOG_OPTION_TITLE) && headerTitleLine && *headerTitleLine) {
            logLineProc(logLineProcParam, "%s", headerTitleLine);
            logLineProc(logLineProcParam, "");
        }

        if (logOptions & LOG_OPTION_EXEFILE) {
            sLogExeFile(logLineProc, logLineProcParam);
        }

        if (logOptions & LOG_OPTION_DATETIME) {
            SYSTEMTIME currentTime;

            if (!time) {
                timeval now;
                gettimeofday(&now, nullptr);

                tm local;
                localtime_r(&now.tv_sec, &local);
                currentTime.wYear = static_cast<WORD>(local.tm_year + 1900);
                currentTime.wMonth = static_cast<WORD>(local.tm_mon + 1);
                currentTime.wDayOfWeek = static_cast<WORD>(local.tm_wday);
                currentTime.wDay = static_cast<WORD>(local.tm_mday);
                currentTime.wHour = static_cast<WORD>(local.tm_hour);
                currentTime.wMinute = static_cast<WORD>(local.tm_min);
                currentTime.wSecond = static_cast<WORD>(local.tm_sec);
                currentTime.wMilliseconds = static_cast<WORD>(now.tv_usec / 1000);
                time = &currentTime;
            }

            sLogDateTimeString(logLineProc, logLineProcParam, time);
        }

        if (logOptions & LOG_OPTION_USERNAME) {
            sLogUserName(logLineProc, logLineProcParam);
        }

        if (logOptions & LOG_OPTION_COMPUTERNAME) {
            sLogComputerName(logLineProc, logLineProcParam);
        }

        sLogSeparatorLine(logLineProc, logLineProcParam, '-', 1);
    }

    __atomic_sub_fetch(&sgRecursionLevel, 1, __ATOMIC_SEQ_CST);
}

int32_t LogMiniDump(void* logfile, EXCEPTION_POINTERS* exceptionpointers, uint32_t count, const char** strings) {
    return 0;
}

int32_t LogMiniDumpIsAvailable() {
    return 0;
}

void LogMachineState(uint32_t logOptions, void (*logLineProc)(void*, const char*, ...), void* logLineProcParam, uint32_t stackFramesToSkip, CONTEXT* context) {
    if (__atomic_add_fetch(&sgRecursionLevel, 1, __ATOMIC_SEQ_CST) == 1) {
        if (!logOptions || (logOptions & LOG_OPTION_DEFAULT)) {
            logOptions |= LOG_OPTION_DBGHELPSTACK | LOG_OPTION_MODULES;

            if (context) {
                logOptions |= LOG_OPTION_REGISTERS;
            }
        }

        sLogSeparatorLine(logLineProc, logLineProcParam, '-', 1);

        if ((logOptions & LOG_OPTION_REGISTERS) && context) {
            sLogRegisters(logLineProc, logLineProcParam, context);
        }

        if (logOptions & (LOG_OPTION_DBGHELPSTACK | LOG_OPTION_MANUALSTACK)) {
            sLogStackTrace(logLineProc, logLineProcParam, context ? 0 : stackFramesToSkip + 1);
        }

        if (logOptions & LOG_OPTION_MODULES) {
            sLogLoadedModules(logLineProc, logLineProcParam);
        }

        sLogSeparatorLine(logLineProc, logLineProcParam, '-', 1);
        sShowOutOfMemory(logLineProc, logLineProcParam);
    }

    __atomic_sub_fetch(&sgRecursionLevel, 1, __ATOMIC_SEQ_CST);
}

#endif
