#if defined(WOWDECOMP_ARCH_64)

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <windows.h>
#include <ddraw.h>
#include <mmsystem.h>
#include <dsound.h>
#include "Engine/Source/Storm/Source/SOption.h"
#include "Engine/Source/Storm/H/stpl.h"
#include "Engine/Source/Storm/H/storm.h"
#include "Engine/Source/Storm/Source/MachineState.h"

#define STORM_FACILITY 0x510
#define SERR_STRING_BASE 0x5100

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
    /* 0000 */ const char* filename;
    /* 0004 */ int32_t linenumber;
    /* 0008 */ uint32_t threadId;
};

struct HANDLER : TSLinkedNode<HANDLER> {
    /* 0008 */ int32_t (STORMAPI* handler)(uint32_t, const char*, const char*, int32_t, const char*);
};

struct _MSGSRC {
    /* 0000 */ uint16_t facility;
    /* 0002 */ uint16_t reserved;
    /* 0004 */ HINSTANCE module;
    /* 0008 */ _MSGSRC* next;
};

// STATIC: 0x00A39AD8
static const char* const s_displaystr[20] = {
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
    "written",
    "Couldn't locate the \"DbgHelp.dll\" debugging DLL.\n\nSome debugging information will be missing from error log files. Press OK to continue, or press Cancel to terminate the program.",
    "Missing Debugging DLL"
};

// STATIC: 0x00ADE858
static LONG s_critsectinit = -1;

// STATIC: 0x00ADE85C
static TSListWinHeap<HANDLER> s_handlerlist;

// STATIC: 0x00CAD7E8
static EXCEPTION_POINTERS* s_exceptionPointers;

// STATIC: 0x00CAD7EC
static CRITICAL_SECTION s_exceptioncritsect;

// STATIC: 0x00CAD804
static int32_t s_displaying;

// STATIC: 0x00CAD808
static CRITICAL_SECTION s_critsect;

// STATIC: 0x00CAD820
static APPFATINFO s_appFatInfo;

// STATIC: 0x00CAD82C
static _MSGSRC* s_msgsrchead;

// STATIC: 0x00CAD830
static int32_t s_msgsrcinit;

// STATIC: 0x00CAD834
static uint32_t s_lasterror;

// STATIC: 0x00CAD838
static char s_logtitle[128];

// STATIC: 0x00CAD8B8
static int32_t (STORMAPI* s_logCallback)(char*, uint32_t);

// STATIC: 0x00CAD8BC
static uint32_t s_noMiniDumps;

// STATIC: 0x00CAD8C0
static char s_logLastFile[MAX_PATH];

// INLINED
template <class T, class TGetLink>
T* TSListWinHeap<T, TGetLink>::NewNode(uint32_t location, size_t extrabytes, uint32_t flags) {
    T* node = new (HeapAlloc(GetProcessHeap(), HEAP_GENERATE_EXCEPTIONS, sizeof(T) + extrabytes)) T;
    memset(node, 0, sizeof(T) + extrabytes);

    if (location) {
        this->LinkNode(node, location, nullptr);
    }

    return node;
}

// OFFSET: 0x00427010
static void Breakpoint() {
    __try {
        __debugbreak();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// OFFSET: 0x00770F70
static const char* GetErrorString(uint32_t id) {
    // STATIC: 0x00CAD9C8
    static char buffer[256];

    if (!LoadStringA(StormGetInstance(), id + SERR_STRING_BASE, buffer, sizeof(buffer))) {
        return s_displaystr[id];
    }

    return buffer;
}

// OFFSET: 0x00770FA0
static void AddStormFacility(uint16_t facility) {
    _MSGSRC** tail = &s_msgsrchead;

    for (_MSGSRC* src = s_msgsrchead; src; src = src->next) {
        tail = &src->next;
    }

    *tail = static_cast<_MSGSRC*>(HeapAlloc(GetProcessHeap(), HEAP_GENERATE_EXCEPTIONS, sizeof(_MSGSRC)));
    (*tail)->facility = facility;
    (*tail)->module = StormGetInstance();
    (*tail)->next = nullptr;
}

// INLINED
inline void AddStormMessages() {
    AddStormFacility(STORM_FACILITY);
    AddStormFacility(_FACDD);
    AddStormFacility(_FACDS);
}

// OFFSET: 0x00770FF0
static void InternalEnterCriticalSection(CRITICAL_SECTION* critsect) {
    if (InterlockedIncrement(&s_critsectinit)) {
        InterlockedDecrement(&s_critsectinit);
    } else {
        InitializeCriticalSection(&s_critsect);
        InitializeCriticalSection(&s_exceptioncritsect);
    }

    EnterCriticalSection(critsect);
}

// INLINED
inline void InternalLeaveCriticalSection(CRITICAL_SECTION* critsect) {
    LeaveCriticalSection(critsect);
}

// OFFSET: 0x00771040
void SErrInitialize() {
    g_opt.serrleaksilentwarning = 1;
    SRegLoadValue("Internal", "No Minidumps", 0, &s_noMiniDumps);
}

// OFFSET: 0x00771070
static int32_t CanBreakToDebugger() {
    int32_t result = 0;
    HMODULE kernel = LoadLibraryA("KERNEL32.DLL");

    if (kernel) {
        BOOL (WINAPI* isDebuggerPresent)() = reinterpret_cast<BOOL (WINAPI*)()>(GetProcAddress(kernel, "IsDebuggerPresent"));

        if (isDebuggerPresent) {
            result = isDebuggerPresent();
        }

        FreeLibrary(kernel);
    }

    return result;
}

// OFFSET: 0x007710B0
static void WriteLine(void* param, const char* format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    _vsnprintf(buffer, sizeof(buffer) - 3, format ? format : "", args);
    va_end(args);
    buffer[sizeof(buffer) - 3] = 0;
    SStrPack(buffer, "\r\n", sizeof(buffer));

    DWORD byteswritten;
    WriteFile(param, buffer, static_cast<DWORD>(SStrLen(buffer)), &byteswritten, nullptr);
}

// OFFSET: 0x00771130
static void WriteMessageToLog(HANDLE logfile, const char* message) {
    uint32_t length = static_cast<uint32_t>(SStrLen(message));
    char* buffer = static_cast<char*>(alloca(2 * length + 4));
    char prevc = 0;
    char c = 0;
    uint32_t pos = 2;
    buffer[0] = '\r';
    buffer[1] = '\n';

    for (uint32_t i = 0; i < length; ++i) {
        c = message[i];

        if (c == '\n' && prevc != '\r') {
            buffer[pos++] = '\r';
        }

        buffer[pos++] = c;
        prevc = c;
    }

    if (!length || c != '\n') {
        buffer[pos++] = '\r';
        buffer[pos++] = '\n';
    }

    buffer[pos] = 0;

    DWORD byteswritten;
    WriteFile(logfile, buffer, pos, &byteswritten, nullptr);
}

// INLINED
inline int32_t MakeDirectory(const char* path) {
    if (CreateDirectoryA(path, nullptr)) {
        return 1;
    }

    DWORD error = GetLastError();

    if (error != ERROR_ALREADY_EXISTS && error != ERROR_ACCESS_DENIED) {
        return 0;
    }

    DWORD attributes = GetFileAttributesA(path);

    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return 0;
    }

    return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

// OFFSET: 0x007711C0
static HANDLE CreateErrorLogFile(const char* suffix, const char* ext, char* logpath, uint32_t logpathchars, const SYSTEMTIME* time) {
    char logfilename[MAX_PATH];
    char exefullpath[MAX_PATH];

    if (!suffix) {
        suffix = " Log";
    }

    GetModuleFileNameA(nullptr, exefullpath, sizeof(exefullpath));
    SStrCopy(logpath, exefullpath, logpathchars);

    char* slash = SStrChrR(logpath, '\\');

    if (slash) {
        slash[1] = 0;
    } else {
        slash = &logpath[SStrLen(logpath)];
    }

    SStrPack(logpath, "Errors\\", logpathchars);

    if (!MakeDirectory(logpath)) {
        slash[1] = 0;
    }

    sprintf(logfilename, "%04d-%02d-%02d %02d.%02d.%02d %s.%s", time->wYear, time->wMonth, time->wDay, time->wHour, time->wMinute, time->wSecond, suffix, ext);
    SStrPack(logpath, logfilename, logpathchars);

    return CreateFileA(logpath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

// OFFSET: 0x007712D0
static HANDLE CreateErrorLog(const char* suffix, const char* message, const SYSTEMTIME* time) {
    char logpath[MAX_PATH];
    SYSTEMTIME localTime;

    if (!time) {
        GetLocalTime(&localTime);
        time = &localTime;
    }

    HANDLE logfile = CreateErrorLogFile(suffix, "txt", logpath, sizeof(logpath), time);

    if (logfile == INVALID_HANDLE_VALUE) {
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

// OFFSET: 0x007713B0
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

// OFFSET: 0x007714E0
static void GetExceptionNameWin32(uint32_t exceptioncode, char* buffer, uint32_t buffersize) {
    switch (exceptioncode) {
        case EXCEPTION_ACCESS_VIOLATION:
            SStrCopy(buffer, "ACCESS_VIOLATION", buffersize);
            break;
        case EXCEPTION_GUARD_PAGE:
            SStrCopy(buffer, "GUARD_PAGE", buffersize);
            break;
        case EXCEPTION_DATATYPE_MISALIGNMENT:
            SStrCopy(buffer, "DATATYPE_MISALIGNMENT", buffersize);
            break;
        case EXCEPTION_BREAKPOINT:
            SStrCopy(buffer, "BREAKPOINT", buffersize);
            break;
        case EXCEPTION_SINGLE_STEP:
            SStrCopy(buffer, "SINGLE_STEP", buffersize);
            break;
        case EXCEPTION_IN_PAGE_ERROR:
            SStrCopy(buffer, "IN_PAGE_ERROR", buffersize);
            break;
        case EXCEPTION_INVALID_HANDLE:
            SStrCopy(buffer, "INVALID_HANDLE", buffersize);
            break;
        case EXCEPTION_ILLEGAL_INSTRUCTION:
            SStrCopy(buffer, "ILLEGAL_INSTRUCTION", buffersize);
            break;
        case EXCEPTION_NONCONTINUABLE_EXCEPTION:
            SStrCopy(buffer, "NONCONTINUABLE_EXCEPTION", buffersize);
            break;
        case EXCEPTION_INVALID_DISPOSITION:
            SStrCopy(buffer, "INVALID_DISPOSITION", buffersize);
            break;
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
            SStrCopy(buffer, "ARRAY_BOUNDS_EXCEEDED", buffersize);
            break;
        case EXCEPTION_FLT_DENORMAL_OPERAND:
            SStrCopy(buffer, "FLT_DENORMAL_OPERAND", buffersize);
            break;
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
            SStrCopy(buffer, "FLT_DIVIDE_BY_ZERO", buffersize);
            break;
        case EXCEPTION_FLT_INEXACT_RESULT:
            SStrCopy(buffer, "FLT_INEXACT_RESULT", buffersize);
            break;
        case EXCEPTION_FLT_INVALID_OPERATION:
            SStrCopy(buffer, "FLT_INVALID_OPERATION", buffersize);
            break;
        case EXCEPTION_FLT_OVERFLOW:
            SStrCopy(buffer, "FLT_OVERFLOW", buffersize);
            break;
        case EXCEPTION_FLT_STACK_CHECK:
            SStrCopy(buffer, "FLT_STACK_CHECK", buffersize);
            break;
        case EXCEPTION_FLT_UNDERFLOW:
            SStrCopy(buffer, "FLT_UNDERFLOW", buffersize);
            break;
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
            SStrCopy(buffer, "INT_DIVIDE_BY_ZERO", buffersize);
            break;
        case EXCEPTION_INT_OVERFLOW:
            SStrCopy(buffer, "INT_OVERFLOW", buffersize);
            break;
        case EXCEPTION_PRIV_INSTRUCTION:
            SStrCopy(buffer, "PRIV_INSTRUCTION", buffersize);
            break;
        case EXCEPTION_STACK_OVERFLOW:
            SStrCopy(buffer, "STACK_OVERFLOW", buffersize);
            break;
        default: {
            HMODULE ntdll = GetModuleHandleA("ntdll.dll");

            if (!ntdll || !FormatMessageA(FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_IGNORE_INSERTS, ntdll, exceptioncode, 0, buffer, buffersize, nullptr)) {
                SStrCopy(buffer, "unknown exception", buffersize);
            }

            break;
        }
    }
}

// OFFSET: 0x007717E0
uint32_t STORMAPI SErrGetLastError() {
    return s_lasterror;
}

// OFFSET: 0x007717F0
int32_t STORMAPI SErrIsDisplayingError() {
    return s_displaying;
}

// OFFSET: 0x00771800
void STORMAPI SErrPrepareAppFatal(const char* filename, int32_t linenumber) {
    InternalEnterCriticalSection(&s_critsect);
    s_appFatInfo.filename = filename;
    s_appFatInfo.linenumber = linenumber;
    s_appFatInfo.threadId = GetCurrentThreadId();
    InternalLeaveCriticalSection(&s_critsect);
}

// OFFSET: 0x00771870
void STORMAPI SErrSetLastError(uint32_t errorcode) {
    s_lasterror = errorcode;
    SetLastError(errorcode);
}

// OFFSET: 0x00771890
void STORMAPI SErrSetLogTitleString(const char* title) {
    InternalEnterCriticalSection(&s_critsect);
    SStrCopy(s_logtitle, title, sizeof(s_logtitle));
    InternalLeaveCriticalSection(&s_critsect);
}

// OFFSET: 0x00771900
void STORMAPI SErrSetLogCallback(int32_t (STORMAPI* callback)(char*, uint32_t)) {
    InternalEnterCriticalSection(&s_critsect);
    s_logCallback = callback;
    InternalLeaveCriticalSection(&s_critsect);
}

// OFFSET: 0x00771960
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

// OFFSET: 0x007719E0
static int32_t UndecorateObjectName(const char* source, char* dest, uint32_t destchars) {
    if (SStrLen(source) < 6) {
        return 0;
    }

    if (source[0] != '.') {
        return 0;
    }

    if (source[3] != 'U') {
        return 0;
    }

    if (!strpbrk(source + 4, "?@")) {
        return 0;
    }

    SStrCopy(dest, source + 4, destchars);

    char* end = strpbrk(dest, "?@");

    if (!end) {
        return 0;
    }

    *end = 0;

    for (char* c = end - 1; c >= dest && *c == '_'; --c) {
        *c = 0;
    }

    SStrPack(dest, " (", destchars);
    SStrPack(dest, source, destchars);
    SStrPack(dest, ")", destchars);

    return 1;
}

// OFFSET: 0x00771A80
int32_t STORMAPI SErrGetErrorStr(uint32_t errorcode, char* buffer, uint32_t bufferchars) {
    if (!buffer || !bufferchars) {
        SErrSetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    InternalEnterCriticalSection(&s_critsect);

    if (!s_msgsrcinit) {
        s_msgsrcinit = 1;
        AddStormMessages();
    }

    HINSTANCE module = nullptr;

    for (_MSGSRC* src = s_msgsrchead; src; src = src->next) {
        if (src->facility == ((errorcode >> 16) & 0xFFF)) {
            module = src->module;
            break;
        }
    }

    InternalLeaveCriticalSection(&s_critsect);
    *buffer = 0;

    if (errorcode == 108) {
        SStrPrintf(buffer, bufferchars, "%s", "Invalid or corrupt archive.\n");
        return 1;
    }

    return FormatMessageA(module ? FORMAT_MESSAGE_FROM_HMODULE : FORMAT_MESSAGE_FROM_SYSTEM, module, errorcode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buffer, bufferchars, nullptr) != 0;
}

// OFFSET: 0x00771B80
void STORMAPI SErrRegisterHandler(int32_t (STORMAPI* handler)(uint32_t, const char*, const char*, int32_t, const char*)) {
    InternalEnterCriticalSection(&s_critsect);
    s_handlerlist.NewNode(1, 0, 0x8000000)->handler = handler;
    InternalLeaveCriticalSection(&s_critsect);
}

// OFFSET: 0x00771C90
int32_t STORMAPI SErrDestroy() {
    s_handlerlist.Clear();

    _MSGSRC* src = s_msgsrchead;
    s_msgsrcinit = 0;

    while (src) {
        _MSGSRC* next = src->next;
        HeapFree(GetProcessHeap(), 0, src);
        src = next;
        s_msgsrchead = next;
    }

    if (s_critsectinit != -1) {
        DeleteCriticalSection(&s_critsect);
        DeleteCriticalSection(&s_exceptioncritsect);
        s_critsectinit = -1;
    }

    return 1;
}

// OFFSET: 0x00771D10
int32_t STORMAPI SErrDisplayError(uint32_t errorcode, const char* filename, int32_t linenumber, const char* description, int32_t recoverable, uint32_t exitcode, uint32_t unknown) {
    // STATIC: 0x00CAE934
    static int32_t lastlinenumber;
    // STATIC: 0x00CAE938
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

    // STATIC: 0x00CAE830
    static char appfilename[MAX_PATH];
    // STATIC: 0x00CAE728
    static char appname[MAX_PATH];
    appfilename[0] = 0;
    appname[0] = 0;
    GetModuleFileNameA(nullptr, appfilename, sizeof(appfilename));

    // STATIC: 0x00CAE5E8
    static WIN32_FIND_DATAA findfiledata;
    memset(&findfiledata, 0, sizeof(findfiledata));

    HANDLE find = FindFirstFileA(appfilename, &findfiledata);

    if (find) {
        FindClose(find);
    }

    SStrCopy(appname, findfiledata.cFileName, sizeof(appname));

    if (SStrChrR(appname, '.')) {
        *SStrChrR(appname, '.') = 0;
    }

    // STATIC: 0x00CAE4E8
    static char errorstr[256];
    errorstr[0] = 0;
    SErrGetErrorStr(errorcode, errorstr, sizeof(errorstr));

    if (!errorstr[0]) {
        SStrPrintf(errorstr, sizeof(errorstr), GetErrorString(SERR_STR_ERROR), errorcode, errorcode, SErrGetErrorText(errorcode));
    }

    // STATIC: 0x00CAE4E0
    static const char* localnameptr = filename;
    // STATIC: 0x00CAE3E0
    static char localnamebuffer[256];

    if (localnameptr && *localnameptr && (linenumber == SLOG_OBJECT || linenumber == SLOG_HANDLE) && UndecorateObjectName(localnameptr, localnamebuffer, sizeof(localnamebuffer))) {
        localnameptr = localnamebuffer;
    }

    // STATIC: 0x00CADBE0
    static char outstr[2048];
    // STATIC: 0x00ADE884
    static char* outptr = outstr;
    // STATIC: 0x00ADE880
    static char* outend = outstr + sizeof(outstr);

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

        // STATIC: 0x00CADBDC
        static EXCEPTION_POINTERS* exceptionPointers = s_exceptionPointers;
        s_exceptionPointers = nullptr;
        InternalLeaveCriticalSection(&s_exceptioncritsect);

        CONTEXT* context = exceptionPointers ? exceptionPointers->ContextRecord : nullptr;

        // STATIC: 0x00CADBCC
        static SYSTEMTIME time;
        GetLocalTime(&time);

        const char* suffix = context ? "Crash" : "Error";
        HANDLE logfile = CreateErrorLog(suffix, outstr, &time);

        if (logfile != INVALID_HANDLE_VALUE) {
            LogMachineState(LOG_OPTION_DEFAULT | LOG_OPTION_FRAMEPARAMS, WriteLine, logfile, context ? 0 : 2, context);
            CloseHandle(logfile);
        }

        char* details = SStrStr(outstr, "Debug Details:");

        if (details) {
            outptr = details;
        }

        if (LogMiniDumpIsAvailable() && !s_noMiniDumps) {
            // STATIC: 0x00CADAC8
            static char logpath[MAX_PATH];
            HANDLE dumpfile = CreateErrorLogFile(suffix, "dmp", logpath, sizeof(logpath), &time);

            if (dumpfile != INVALID_HANDLE_VALUE) {
                const char* title = s_logtitle;
                LogMiniDump(dumpfile, exceptionPointers, 1, &title);
                CloseHandle(dumpfile);
            }
        }

        UnloadMachineStateSymbols();
    }

    if (g_opt.unk_0028) {
        s_displaying = 0;
        return 1;
    }

    uint32_t icon;
    uint32_t buttons;
    int32_t defaultresult;
    int32_t keeprunning;
    int32_t breakresult;

    if (!CanBreakToDebugger()) {
        if (recoverable) {
            outptr += SStrCopy(outptr, GetErrorString(SERR_STR_KEEP_RUNNING), outend - outptr);
            icon = MB_ICONWARNING;
            buttons = MB_OKCANCEL | MB_DEFBUTTON2;
            defaultresult = IDCANCEL;
            keeprunning = IDOK;
            breakresult = 0;
        } else {
            outptr += SStrCopy(outptr, GetErrorString(SERR_STR_TERMINATE), outend - outptr);
            icon = MB_ICONERROR;
            buttons = MB_OK;
            defaultresult = IDOK;
            keeprunning = 0;
            breakresult = 0;
        }
    } else {
        if (!recoverable) {
            outptr += SStrCopy(outptr, GetErrorString(SERR_STR_BREAK), outend - outptr);
            icon = MB_ICONERROR;
            buttons = MB_YESNO | MB_DEFBUTTON2;
            defaultresult = IDNO;
            keeprunning = 0;
            breakresult = IDYES;
        } else {
            outptr += SStrCopy(outptr, GetErrorString(SERR_STR_BREAK_OR_RUN), outend - outptr);
            icon = MB_ICONWARNING;
            buttons = MB_YESNOCANCEL | MB_DEFBUTTON2;
            defaultresult = IDNO;
            keeprunning = IDCANCEL;
            breakresult = IDYES;
        }
    }

    InternalEnterCriticalSection(&s_critsect);

    UINT type = icon | buttons | MB_TASKMODAL | MB_SETFOREGROUND | MB_TOPMOST;
    int32_t result = keeprunning ? keeprunning : defaultresult;
    HANDLER* handler = s_handlerlist.Head();

    while (handler) {
        if (!handler->handler(errorcode, errorstr, localnameptr, linenumber, description ? description : "")) {
            break;
        }

        handler = s_handlerlist.Next(handler);
    }

    if (!handler) {
        HWND window = SMsgGetDefaultWindow();

        if (window && (!IsWindow(window) || !IsWindowVisible(window))) {
            window = nullptr;
        }

        result = MessageBoxA(window, outstr, appname, type);
    }

    InternalLeaveCriticalSection(&s_critsect);

    if (result == breakresult) {
        Breakpoint();
        s_displaying = 0;
        return 1;
    }

    if (result == keeprunning) {
        s_displaying = 0;
        return 1;
    }

    DWORD processexitcode;

    if (!(GetVersion() & 0x80000000) || !GetExitCodeProcess(GetCurrentProcess(), &processexitcode) || processexitcode == STILL_ACTIVE) {
        TerminateProcess(GetCurrentProcess(), exitcode);
    }

    s_displaying = 0;

    return 0;
}

// OFFSET: 0x00772670
void STORMAPI SErrReportNamedResourceLeak(const char* handlename, const char* resourcename) {
    // STATIC: 0x00CAE940
    static int32_t checked;
    // STATIC: 0x00CAE93C
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

// OFFSET: 0x00772760
static LONG WINAPI ExceptionFilterWin32(EXCEPTION_POINTERS* exceptionpointers) {
    EXCEPTION_RECORD* record = exceptionpointers->ExceptionRecord;

    InternalEnterCriticalSection(&s_exceptioncritsect);

    if (!s_exceptionPointers) {
        s_exceptionPointers = exceptionpointers;
    }

    InternalLeaveCriticalSection(&s_exceptioncritsect);

    char exceptionname[40];
    char buffer[120];
    char message[256];
    exceptionname[0] = 0;
    buffer[0] = 0;
    message[0] = 0;

    __try {
        GetExceptionNameWin32(record->ExceptionCode, exceptionname, sizeof(exceptionname));
#if defined(_M_ARM64)
        SStrPrintf(buffer, sizeof(buffer), "0x%p (%s) at %p", reinterpret_cast<void*>(static_cast<uintptr_t>(record->ExceptionCode)), exceptionname, record->ExceptionAddress);
#else
        SStrPrintf(buffer, sizeof(buffer), "0x%p (%s) at %04X:%p", reinterpret_cast<void*>(static_cast<uintptr_t>(record->ExceptionCode)), exceptionname, exceptionpointers->ContextRecord->SegCs, record->ExceptionAddress);
#endif

        if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters == 2) {
            char format[256];
            SStrCopy(format, GetErrorString(SERR_STR_ACCESS_VIOLATION), sizeof(format));
            SStrPrintf(message, sizeof(message), format, record->ExceptionAddress, reinterpret_cast<void*>(record->ExceptionInformation[1]), GetErrorString(record->ExceptionInformation[0] ? SERR_STR_WRITTEN : SERR_STR_READ));
        }

        SErrDisplayError(STORM_ERROR_FATAL, buffer, SLOG_EXCEPTION, message, 0, 1, 0x11111111);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HANDLE file = CreateFileA("Errors\\exception-failed.txt", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

        if (file != INVALID_HANDLE_VALUE) {
            DWORD byteswritten;
            WriteFile(file, "Exception handler died\r\n", static_cast<DWORD>(SStrLen("Exception handler died\r\n")), &byteswritten, nullptr);
            WriteFile(file, "Original exception:\r\n", static_cast<DWORD>(SStrLen("Original exception:\r\n")), &byteswritten, nullptr);
            WriteFile(file, buffer, static_cast<DWORD>(SStrLen(buffer)), &byteswritten, nullptr);
            WriteFile(file, "\r\n", static_cast<DWORD>(SStrLen("\r\n")), &byteswritten, nullptr);
            WriteFile(file, message, static_cast<DWORD>(SStrLen(message)), &byteswritten, nullptr);
            CloseHandle(file);
        }
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

// OFFSET: 0x007729B0
void SErrDisplayAppFatalCustomV(uint32_t errorcode, const char* format, va_list args) {
    const char* filename = nullptr;
    int32_t linenumber = 0;

    InternalEnterCriticalSection(&s_critsect);

    if (s_appFatInfo.threadId == GetCurrentThreadId()) {
        filename = s_appFatInfo.filename;
        linenumber = s_appFatInfo.linenumber;
        s_appFatInfo.filename = nullptr;
        s_appFatInfo.linenumber = 0;
        s_appFatInfo.threadId = 0;
    }

    InternalLeaveCriticalSection(&s_critsect);

    char buffer[2048];
    _vsnprintf(buffer, sizeof(buffer) - 1, format, args);
    buffer[sizeof(buffer) - 1] = 0;
    SErrDisplayError(errorcode, filename, linenumber, buffer, 0, 1, 0x11111111);
    StormExitProcess(1);
}

// OFFSET: 0x00772A80
void SErrDisplayAppFatalCustom(uint32_t errorcode, const char* format, ...) {
    va_list args;
    va_start(args, format);
    SErrDisplayAppFatalCustomV(errorcode, format, args);
}

// OFFSET: 0x00772AA0
void SErrDisplayAppFatal(const char* format, ...) {
    va_list args;
    va_start(args, format);
    SErrDisplayAppFatalCustomV(STORM_ERROR_FATAL_CONDITION, format, args);
}

// OFFSET: 0x00772AC0
int32_t SErrDisplayErrorFmt(uint32_t errorcode, const char* filename, int32_t linenumber, int32_t recoverable, uint32_t exitcode, const char* format, ...) {
    char buffer[2048];
    va_list args;
    va_start(args, format);
    _vsnprintf(buffer, sizeof(buffer) - 1, format, args);
    va_end(args);
    buffer[sizeof(buffer) - 1] = 0;

    return SErrDisplayError(errorcode, filename, linenumber, buffer, recoverable, exitcode, 0x11111111);
}

// OFFSET: 0x00772B20
void STORMAPI SErrCatchUnhandledExceptions() {
    SetUnhandledExceptionFilter(ExceptionFilterWin32);
}

// OFFSET: 0x008889B0
void SErrDisplayAppFatalMessage(const char* message, const char* filename, int32_t linenumber) {
    SErrPrepareAppFatal(filename, linenumber);
    SErrDisplayAppFatal("%s", message);
}

#endif
