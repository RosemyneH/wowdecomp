#if defined(__linux__) || defined(__APPLE__)

#include "Engine/Source/Base/OsPosix.h"
#include <cerrno>
#include <csignal>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <initializer_list>
#include <new>
#if defined(__APPLE__)
#include <climits>
#include <mach-o/dyld.h>
#endif
#include <pthread.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <unistd.h>
#include "Engine/Source/Storm/Source/SOption.h"
#include "Engine/Source/Storm/H/stpl.h"
#include "Engine/Source/Storm/H/storm.h"
#include "Engine/Source/Storm/Source/MachineState.h"

#define SERR_STR_ERROR 0
#define SERR_STR_CRITICAL 1
#define SERR_STR_PROGRAM 2
#define SERR_STR_FILE_LINE 3
#define SERR_STR_FUNCTION 4
#define SERR_STR_OBJECT 5
#define SERR_STR_HANDLE 6
#define SERR_STR_EXPRESSION 7
#define SERR_STR_DESCRIPTION 8
#define SERR_STR_TERMINATE 9
#define SERR_STR_KEEP_RUNNING 10
#define SERR_STR_FILE 11
#define SERR_STR_BREAK 12
#define SERR_STR_BREAK_OR_RUN 13
#define SERR_STR_EXCEPTION 14
#define SERR_STR_ACCESS_VIOLATION 15
#define SERR_STR_READ 16
#define SERR_STR_WRITTEN 17

template <class T, class TGetLink = TSGetLink<T> >
class TSListWinHeap : public TSList<T, TGetLink> {
    public:
    T* NewNode(uint32_t location, size_t extrabytes, uint32_t flags);
};

struct APPFATINFO {
    const char* filename;
    int32_t linenumber;
    uint32_t threadId;
};

struct HANDLER : TSLinkedNode<HANDLER> {
    int32_t (STORMAPI* handler)(uint32_t, const char*, const char*, int32_t, const char*);
};

static const char* const s_displaystr[18] = {
    "ERROR #%u (0x%08x) %s",
    "This application has encountered a critical error:\n\n%s\n",
    "Program:\t%s\n",
    "File:\t%s\nLine:\t%d\n",
    "Function:\t%s\n",
    "Object:\t%s\n",
    "Handle:\t%s\n",
    "Expr:\t%s\n\n",
    "\n%s\n\n",
    "Press OK to terminate the application.",
    "Do you wish to keep running anyway?\n\tOK:\tKeep running\n\tCancel:\tTerminate application",
    "File:\t%s\n",
    "Do you wish to break to the debugger?\n\tYes:\tBreak to debugger\n\tNo:\tTerminate application",
    "Do you wish to break to the debugger?\n\tYes:\tBreak to debugger\n\tNo:\tTerminate application\n\tCancel:\tKeep running",
    "Exception:\t%s\n",
    "The instruction at \"0x%p\" referenced memory at \"0x%p\".\nThe memory could not be \"%s\".",
    "read",
    "written"
};

static TSListWinHeap<HANDLER> s_handlerlist;

static EXCEPTION_POINTERS* s_exceptionPointers;

static pthread_mutex_t s_exceptioncritsect;

static int32_t s_displaying;

static pthread_mutex_t s_critsect;

static pthread_once_t s_critsectonce = PTHREAD_ONCE_INIT;

static APPFATINFO s_appFatInfo;

static uint32_t s_lasterror;

static char s_logtitle[128];

static int32_t (STORMAPI* s_logCallback)(char*, uint32_t);

static uint32_t s_noMiniDumps;

static char s_logLastFile[MAX_PATH];

template <class T, class TGetLink>
T* TSListWinHeap<T, TGetLink>::NewNode(uint32_t location, size_t extrabytes, uint32_t flags) {
    void* memory = calloc(1, sizeof(T) + extrabytes);

    if (!memory) {
        abort();
    }

    T* node = new (memory) T;
    memset(node, 0, sizeof(T) + extrabytes);

    if (location) {
        this->LinkNode(node, location, nullptr);
    }

    return node;
}

static uint32_t CurrentThreadId() {
    return OsPosixThreadId();
}

static void InitCriticalSections() {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&s_critsect, &attr);
    pthread_mutex_init(&s_exceptioncritsect, &attr);
    pthread_mutexattr_destroy(&attr);
}

static void InternalEnterCriticalSection(pthread_mutex_t* critsect) {
    pthread_once(&s_critsectonce, InitCriticalSections);
    pthread_mutex_lock(critsect);
}

inline void InternalLeaveCriticalSection(pthread_mutex_t* critsect) {
    pthread_mutex_unlock(critsect);
}

static const char* GetErrorString(uint32_t id) {
    return s_displaystr[id];
}

static void GetLocalSystemTime(SYSTEMTIME* time) {
    timeval now;
    gettimeofday(&now, nullptr);

    tm local;
    localtime_r(&now.tv_sec, &local);
    time->wYear = static_cast<WORD>(local.tm_year + 1900);
    time->wMonth = static_cast<WORD>(local.tm_mon + 1);
    time->wDayOfWeek = static_cast<WORD>(local.tm_wday);
    time->wDay = static_cast<WORD>(local.tm_mday);
    time->wHour = static_cast<WORD>(local.tm_hour);
    time->wMinute = static_cast<WORD>(local.tm_min);
    time->wSecond = static_cast<WORD>(local.tm_sec);
    time->wMilliseconds = static_cast<WORD>(now.tv_usec / 1000);
}

void SErrInitialize() {
    g_opt.serrleaksilentwarning = 1;
    SRegLoadValue("Internal", "No Minidumps", 0, &s_noMiniDumps);
}

static int32_t CanBreakToDebugger() {
    FILE* status = fopen("/proc/self/status", "r");

    if (!status) {
        return 0;
    }

    char line[256];
    int32_t result = 0;

    while (fgets(line, sizeof(line), status)) {
        if (!strncmp(line, "TracerPid:", 10)) {
            result = atoi(line + 10) != 0;
            break;
        }
    }

    fclose(status);

    return result;
}

static void WriteLine(void* param, const char* format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer) - 2, format ? format : "", args);
    va_end(args);
    buffer[sizeof(buffer) - 2] = 0;
    SStrPack(buffer, "\n", sizeof(buffer));

    fputs(buffer, static_cast<FILE*>(param));
}

static void WriteMessageToLog(FILE* logfile, const char* message) {
    size_t length = SStrLen(message);
    fputc('\n', logfile);
    fputs(message, logfile);

    if (!length || message[length - 1] != '\n') {
        fputc('\n', logfile);
    }
}

static int32_t MakeDirectory(const char* path) {
    if (!mkdir(path, 0755)) {
        return 1;
    }

    struct stat info;

    return errno == EEXIST && !stat(path, &info) && S_ISDIR(info.st_mode);
}

static void GetExecutablePath(char* buffer, uint32_t buffersize) {
#if defined(__APPLE__)
    char executable[PATH_MAX];
    char resolved[PATH_MAX];
    uint32_t length = sizeof(executable);

    if (_NSGetExecutablePath(executable, &length)) {
        buffer[0] = 0;
        return;
    }

    const char* path = realpath(executable, resolved);
    SStrCopy(buffer, path ? path : executable, buffersize);
#else
    ssize_t length = readlink("/proc/self/exe", buffer, buffersize - 1);
    buffer[length > 0 ? length : 0] = 0;
#endif
}

static FILE* CreateErrorLogFile(const char* suffix, const char* ext, char* logpath, uint32_t logpathchars, const SYSTEMTIME* time) {
    char logfilename[MAX_PATH];
    char exefullpath[MAX_PATH];

    if (!suffix) {
        suffix = " Log";
    }

    GetExecutablePath(exefullpath, sizeof(exefullpath));
    SStrCopy(logpath, exefullpath, logpathchars);

    char* slash = SStrChrR(logpath, '/');

    if (slash) {
        slash[1] = 0;
    } else {
        logpath[0] = 0;
        slash = logpath - 1;
    }

    SStrPack(logpath, "Errors/", logpathchars);

    if (!MakeDirectory(logpath)) {
        slash[1] = 0;
    }

    snprintf(logfilename, sizeof(logfilename), "%04d-%02d-%02d %02d.%02d.%02d %s.%s", time->wYear, time->wMonth, time->wDay, time->wHour, time->wMinute, time->wSecond, suffix, ext);
    SStrPack(logpath, logfilename, logpathchars);

    return fopen(logpath, "w");
}

static FILE* CreateErrorLog(const char* suffix, const char* message, const SYSTEMTIME* time) {
    char logpath[MAX_PATH];
    SYSTEMTIME localTime;

    if (!time) {
        GetLocalSystemTime(&localTime);
        time = &localTime;
    }

    FILE* logfile = CreateErrorLogFile(suffix, "txt", logpath, sizeof(logpath), time);

    if (!logfile) {
        return logfile;
    }

    LogComputerInfoHeader(LOG_OPTION_DEFAULT, WriteLine, logfile, s_logtitle, time);
    WriteMessageToLog(logfile, message);

    if (s_logCallback) {
        char* buffer = static_cast<char*>(alloca(5200));
        buffer[0] = 0;
        s_logCallback(buffer, 5200);

        if (strlen(buffer)) {
            WriteMessageToLog(logfile, buffer);
        }
    }

    SStrCopy(s_logLastFile, logpath, sizeof(s_logLastFile));

    return logfile;
}

static const char* SErrGetErrorText(uint32_t errorcode) {
    switch (errorcode) {
        case STORM_ERROR_ASSERTION:
            return "Assertion Failure";
        case STORM_ERROR_NOT_ARCHIVE:
            return "Not Archive";
        case STORM_ERROR_VERSION_MISMATCH:
            return "Version Mismatch";
        case STORM_ERROR_MEM_NOT_ALLOCATED:
            return "Memory Already Freed";
        case STORM_ERROR_MEM_CORRUPTED:
            return "Memory Corrupt";
        case STORM_ERROR_MEM_INVALID:
            return "Memory Invalid Block";
        case STORM_ERROR_MEM_NOT_FREED:
            return "Memory Never Released";
        case STORM_ERROR_RESOURCES_NOT_RELEASED:
            return "Handle Never Released";
        case STORM_ERROR_OUT_OF_BOUNDS:
            return "Access Out Of Bounds";
        case STORM_ERROR_FILE_CORRUPTED:
            return "File Corrupt";
        case STORM_ERROR_FATAL:
            return "Fatal Exception";
        case STORM_ERROR_FATAL_CONDITION:
            return "Fatal Condition";
        default:
            return "";
    }
}

static const char* GetSignalName(int signal) {
    switch (signal) {
        case SIGSEGV:
            return "ACCESS_VIOLATION";
        case SIGBUS:
            return "DATATYPE_MISALIGNMENT";
        case SIGILL:
            return "ILLEGAL_INSTRUCTION";
        case SIGFPE:
            return "FLOAT_OR_INT_ARITHMETIC";
        case SIGABRT:
            return "ABORT";
        case SIGTRAP:
            return "BREAKPOINT";
        default:
            return "unknown exception";
    }
}

uint32_t STORMAPI SErrGetLastError() {
    return s_lasterror;
}

int32_t STORMAPI SErrIsDisplayingError() {
    return s_displaying;
}

void STORMAPI SErrPrepareAppFatal(const char* filename, int32_t linenumber) {
    InternalEnterCriticalSection(&s_critsect);
    s_appFatInfo.filename = filename;
    s_appFatInfo.linenumber = linenumber;
    s_appFatInfo.threadId = CurrentThreadId();
    InternalLeaveCriticalSection(&s_critsect);
}

void STORMAPI SErrSetLastError(uint32_t errorcode) {
    s_lasterror = errorcode;
}

void STORMAPI SErrSetLogTitleString(const char* title) {
    InternalEnterCriticalSection(&s_critsect);
    SStrCopy(s_logtitle, title, sizeof(s_logtitle));
    InternalLeaveCriticalSection(&s_critsect);
}

void STORMAPI SErrSetLogCallback(int32_t (STORMAPI* callback)(char*, uint32_t)) {
    InternalEnterCriticalSection(&s_critsect);
    s_logCallback = callback;
    InternalLeaveCriticalSection(&s_critsect);
}

int32_t STORMAPI SErrGetLogLastPath(char* buf, uint32_t size) {
    InternalEnterCriticalSection(&s_critsect);
    int32_t result = 0;

    if (s_logLastFile[0]) {
        SStrCopy(buf, s_logLastFile, size);
        result = 1;
    }

    InternalLeaveCriticalSection(&s_critsect);

    return result;
}

int32_t STORMAPI SErrGetErrorStr(uint32_t errorcode, char* buffer, uint32_t bufferchars) {
    if (!buffer || !bufferchars) {
        SErrSetLastError(STORM_ERROR_INVALID_PARAMETER);
        return 0;
    }

    *buffer = 0;

    if (errorcode == 108) {
        SStrPrintf(buffer, bufferchars, "%s", "Invalid or corrupt archive.\n");
        return 1;
    }

    return 0;
}

void STORMAPI SErrRegisterHandler(int32_t (STORMAPI* handler)(uint32_t, const char*, const char*, int32_t, const char*)) {
    InternalEnterCriticalSection(&s_critsect);
    s_handlerlist.NewNode(1, 0, 0x8000000)->handler = handler;
    InternalLeaveCriticalSection(&s_critsect);
}

int32_t STORMAPI SErrDestroy() {
    s_handlerlist.Clear();

    return 1;
}

int32_t STORMAPI SErrDisplayError(uint32_t errorcode, const char* filename, int32_t linenumber, const char* description, int32_t recoverable, uint32_t exitcode, uint32_t unknown) {
    static int32_t lastlinenumber;
    static const char* lastfilename;

    if (g_opt.unk_0028) {
        if (filename == lastfilename && linenumber == lastlinenumber) {
            return 1;
        }

        lastfilename = filename;
        lastlinenumber = linenumber;
    }

    if (s_displaying) {
        return 0;
    }

    s_displaying = 1;

    static char appfilename[MAX_PATH];
    GetExecutablePath(appfilename, sizeof(appfilename));

    static char errorstr[256];
    errorstr[0] = 0;
    SErrGetErrorStr(errorcode, errorstr, sizeof(errorstr));

    if (!errorstr[0]) {
        SStrPrintf(errorstr, sizeof(errorstr), GetErrorString(SERR_STR_ERROR), errorcode, errorcode, SErrGetErrorText(errorcode));
    }

    const char* localnameptr = filename;

    static char outstr[2048];
    char* outptr = outstr;
    char* outend = outstr + sizeof(outstr);

    if (localnameptr) {
        outptr += SStrCopy(outptr, localnameptr, outend - outptr);
    }

    if (linenumber > 0) {
        SStrPrintf(outptr, outend - outptr, "(%u)", linenumber);
        outptr += SStrLen(outptr);
    }

    SStrPrintf(outptr, outend - outptr, " : error %u: ", errorcode);
    outptr += SStrLen(outptr);
    SStrCopy(outptr, errorstr, outend - outptr);
    SDebugPrintf("%s\n", outstr);

    outptr = outstr;
    SStrPrintf(outptr, outend - outptr, GetErrorString(SERR_STR_CRITICAL), errorstr);
    outptr += SStrLen(outptr);
    SStrPrintf(outptr, outend - outptr, GetErrorString(SERR_STR_PROGRAM), appfilename);
    outptr += SStrLen(outptr);

    if (localnameptr && *localnameptr) {
        switch (linenumber) {
            case SLOG_EXCEPTION:
                SStrPrintf(outptr, outend - outptr, GetErrorString(SERR_STR_EXCEPTION), localnameptr);
                break;
            case SLOG_FILE:
                SStrPrintf(outptr, outend - outptr, GetErrorString(SERR_STR_FILE), localnameptr);
                break;
            case SLOG_HANDLE:
                SStrPrintf(outptr, outend - outptr, GetErrorString(SERR_STR_HANDLE), localnameptr);
                break;
            case SLOG_OBJECT:
                SStrPrintf(outptr, outend - outptr, GetErrorString(SERR_STR_OBJECT), localnameptr);
                break;
            case SLOG_FUNCTION:
                SStrPrintf(outptr, outend - outptr, GetErrorString(SERR_STR_FUNCTION), localnameptr);
                break;
            default:
                SStrPrintf(outptr, outend - outptr, GetErrorString(SERR_STR_FILE_LINE), localnameptr, linenumber);
                break;
        }

        outptr += SStrLen(outptr);
    }

    if (errorcode == STORM_ERROR_ASSERTION) {
        SStrPrintf(outptr, outend - outptr, GetErrorString(SERR_STR_EXPRESSION), description ? description : "");
    } else {
        SStrPrintf(outptr, outend - outptr, GetErrorString(SERR_STR_DESCRIPTION), description ? description : "");
    }

    outptr += SStrLen(outptr);

    if (!g_opt.serrsuppresslogs) {
        LoadMachineStateSymbols();
        InternalEnterCriticalSection(&s_exceptioncritsect);
        EXCEPTION_POINTERS* exceptionPointers = s_exceptionPointers;
        s_exceptionPointers = nullptr;
        InternalLeaveCriticalSection(&s_exceptioncritsect);

        CONTEXT* context = exceptionPointers ? exceptionPointers->ContextRecord : nullptr;

        SYSTEMTIME time;
        GetLocalSystemTime(&time);

        FILE* logfile = CreateErrorLog(context ? "Crash" : "Error", outstr, &time);

        if (logfile) {
            LogMachineState(LOG_OPTION_DEFAULT | LOG_OPTION_FRAMEPARAMS, WriteLine, logfile, context ? 0 : 2, context);
            fclose(logfile);
        }

        UnloadMachineStateSymbols();
    }

    if (g_opt.unk_0028) {
        s_displaying = 0;
        return 1;
    }

    int32_t debugger = CanBreakToDebugger();
    outptr += SStrCopy(outptr, GetErrorString(debugger ? (recoverable ? SERR_STR_BREAK_OR_RUN : SERR_STR_BREAK) : (recoverable ? SERR_STR_KEEP_RUNNING : SERR_STR_TERMINATE)), outend - outptr);

    InternalEnterCriticalSection(&s_critsect);

    HANDLER* handler = s_handlerlist.Head();

    while (handler) {
        if (!handler->handler(errorcode, errorstr, localnameptr, linenumber, description ? description : "")) {
            break;
        }

        handler = s_handlerlist.Next(handler);
    }

    if (!handler) {
        fprintf(stderr, "%s\n", outstr);
    }

    InternalLeaveCriticalSection(&s_critsect);

    if (debugger) {
        raise(SIGTRAP);
        s_displaying = 0;
        return 1;
    }

    if (recoverable) {
        s_displaying = 0;
        return 1;
    }

    fflush(nullptr);
    _exit(static_cast<int>(exitcode));
}

void STORMAPI SErrReportNamedResourceLeak(const char* handlename, const char* resourcename) {
    static int32_t checked;
    static uint32_t debugmode;

    if (!checked) {
        checked = 1;
        SRegLoadValue("Internal", "Debug Memory", 0, &debugmode);
    }

    if (!debugmode) {
        return;
    }

    char errormessage[256];

    if (resourcename && *resourcename) {
        SStrPrintf(errormessage, sizeof(errormessage), "%s (%s)", handlename, resourcename);
    } else {
        SStrPrintf(errormessage, sizeof(errormessage), "%s", handlename);
    }

    if (g_opt.serrleaksilentwarning) {
        char szMessage[200];
        SStrPrintf(szMessage, sizeof(szMessage), "Storm Error : handle never released -- %s\n", errormessage);
        OutputDebugStringA(szMessage);
    } else {
        SErrDisplayError(STORM_ERROR_RESOURCES_NOT_RELEASED, errormessage, SLOG_HANDLE, nullptr, 1, 1, 0x11111111);
    }
}

static void ExceptionFilterLinux(int signal, siginfo_t* info, void* context) {
    EXCEPTION_POINTERS exceptionpointers;
    exceptionpointers.ExceptionRecord = info;
    exceptionpointers.ContextRecord = static_cast<CONTEXT*>(context);

    InternalEnterCriticalSection(&s_exceptioncritsect);

    if (!s_exceptionPointers) {
        s_exceptionPointers = &exceptionpointers;
    }

    InternalLeaveCriticalSection(&s_exceptioncritsect);

    char buffer[120];
    char message[256];
    message[0] = 0;

    SStrPrintf(buffer, sizeof(buffer), "0x%p (%s) at %p", reinterpret_cast<void*>(static_cast<uintptr_t>(signal)), GetSignalName(signal), info->si_addr);

    if (signal == SIGSEGV || signal == SIGBUS) {
        SStrPrintf(message, sizeof(message), GetErrorString(SERR_STR_ACCESS_VIOLATION), info->si_addr, info->si_addr, GetErrorString(SERR_STR_READ));
    }

    SErrDisplayError(STORM_ERROR_FATAL, buffer, SLOG_EXCEPTION, message, 0, 1, 0x11111111);
    raise(signal);
}

void SErrDisplayAppFatalCustomV(uint32_t errorcode, const char* format, va_list args) {
    const char* filename = nullptr;
    int32_t linenumber = 0;

    InternalEnterCriticalSection(&s_critsect);

    if (s_appFatInfo.threadId == CurrentThreadId()) {
        filename = s_appFatInfo.filename;
        linenumber = s_appFatInfo.linenumber;
        s_appFatInfo.filename = nullptr;
        s_appFatInfo.linenumber = 0;
        s_appFatInfo.threadId = 0;
    }

    InternalLeaveCriticalSection(&s_critsect);

    char buffer[2048];
    vsnprintf(buffer, sizeof(buffer) - 1, format, args);
    buffer[sizeof(buffer) - 1] = 0;
    SErrDisplayError(errorcode, filename, linenumber, buffer, 0, 1, 0x11111111);
    StormExitProcess(1);
}

void SErrDisplayAppFatalCustom(uint32_t errorcode, const char* format, ...) {
    va_list args;
    va_start(args, format);
    SErrDisplayAppFatalCustomV(errorcode, format, args);
}

void SErrDisplayAppFatal(const char* format, ...) {
    va_list args;
    va_start(args, format);
    SErrDisplayAppFatalCustomV(STORM_ERROR_FATAL_CONDITION, format, args);
}

int32_t SErrDisplayErrorFmt(uint32_t errorcode, const char* filename, int32_t linenumber, int32_t recoverable, uint32_t exitcode, const char* format, ...) {
    char buffer[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer) - 1, format, args);
    va_end(args);
    buffer[sizeof(buffer) - 1] = 0;

    return SErrDisplayError(errorcode, filename, linenumber, buffer, recoverable, exitcode, 0x11111111);
}

void STORMAPI SErrCatchUnhandledExceptions() {
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_sigaction = ExceptionFilterLinux;
    action.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigemptyset(&action.sa_mask);

    for (int signal : { SIGSEGV, SIGBUS, SIGILL, SIGFPE }) {
        sigaction(signal, &action, nullptr);
    }
}

void SErrDisplayAppFatalMessage(const char* message, const char* filename, int32_t linenumber) {
    SErrPrepareAppFatal(filename, linenumber);
    SErrDisplayAppFatal("%s", message);
}

#endif
