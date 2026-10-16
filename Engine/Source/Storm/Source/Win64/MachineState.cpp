#if defined(WOWDECOMP_ARCH_64)

#if defined(_M_ARM64)

#include <windows.h>
#include "Engine/Source/Storm/H/storm.h"
#include "Engine/Source/Storm/Source/MachineState.h"

int32_t g_memFullError;

void LoadMachineStateSymbols() {
}

void UnloadMachineStateSymbols() {
}

void LogComputerInfoHeader(uint32_t logOptions, void (*logLineProc)(void*, const char*, ...), void* logLineProcParam, const char* headerTitleLine, const SYSTEMTIME* time) {
    if (logLineProc && headerTitleLine) {
        logLineProc(logLineProcParam, "%s", headerTitleLine);
    }
}

void LogMachineState(uint32_t logOptions, void (*logLineProc)(void*, const char*, ...), void* logLineProcParam, uint32_t stackFramesToSkip, CONTEXT* context) {
    if (logLineProc) {
        logLineProc(logLineProcParam, "**** Stack/crash dump helpers are not implemented on Windows ARM64 yet.");
    }
}

int32_t LogMiniDump(void* logfile, EXCEPTION_POINTERS* exceptionpointers, uint32_t count, const char** strings) {
    return 0;
}

int32_t LogMiniDumpIsAvailable() {
    return 0;
}

#elif defined(_M_X64)

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>
#include <winternl.h>
#include "Engine/Source/Storm/H/storm.h"
#include "Engine/Source/Storm/Source/MachineState.h"

#define CV_SIGNATURE_NB10 0x3031424E
#define CV_SIGNATURE_RSDS 0x53445352

#define DBGHELP_64 "64"
#define LOG_PTR "%016llX"
#define LOG_PTR_EXTRA 8

class CDbgHelpDll {
    public:
    /* 0000 */ HINSTANCE hInstance;
    /* 0004 */ uint32_t loadCount;
    /* 0008 */ decltype(&::StackWalk) StackWalk;
    /* 000C */ decltype(&::SymFunctionTableAccess) SymFunctionTableAccess;
    /* 0010 */ decltype(&::SymGetLineFromAddr) SymGetLineFromAddr;
    /* 0014 */ decltype(&::SymGetModuleBase) SymGetModuleBase;
    /* 0018 */ decltype(&::SymGetModuleInfo) SymGetModuleInfo;
    /* 001C */ decltype(&::SymGetOptions) SymGetOptions;
    /* 0020 */ decltype(&::SymGetSymFromAddr) SymGetSymFromAddr;
    /* 0024 */ decltype(&::SymInitialize) SymInitialize;
    /* 0028 */ decltype(&::SymCleanup) SymCleanup;
    /* 002C */ decltype(&::SymSetOptions) SymSetOptions;
    /* 0030 */ decltype(&::SymEnumerateModules) SymEnumerateModules;
    /* 0034 */ decltype(&::SymEnumerateSymbols) SymEnumerateSymbols;
    /* 0038 */ decltype(&::MiniDumpWriteDump) MiniDumpWriteDump;

    int32_t Load();
    void Unload();
};

struct LOADEDMODULE {
    /* 0000 */ char name[256];
    /* 0100 */ void* base;
    /* 0104 */ uint32_t size;
};

struct MEMDUMP {
    /* 0000 */ void (*logLineProc)(void*, const char*, ...);
    /* 0004 */ void* logLineProcParam;
};

struct MiniDumpParam {
    /* 0000 */ void* logfile;
    /* 0004 */ EXCEPTION_POINTERS* exceptionPointers;
    /* 0008 */ uint32_t threadId;
    /* 000C */ int32_t result;
    /* 0010 */ uint32_t userStringCount;
    /* 0014 */ const char** userStrings;
};

struct MODULEDEBUGINFO {
    /* 0000 */ char pdbName[64];
    /* 0040 */ GUID guid;
    /* 0050 */ uint32_t age;
    /* 0054 */ uint32_t signature;
    /* 0058 */ uint32_t checksum;
    /* 005C */ uint32_t timeDateStamp;
};

// STATIC: 0x00ADEDE8
static char s_monthNames[12][4] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

// STATIC: 0x00CAC120
int32_t g_memFullError;

// STATIC: 0x00CB7484
static CRITICAL_SECTION s_CrawlCritsect;

// STATIC: 0x00CB749C
static LONG sgRecursionLevel;

// STATIC: 0x00CB74A0
static CDbgHelpDll sgDbgHelpDll;

// OFFSET: 0x0077CA70
int32_t CDbgHelpDll::Load() {
    ++this->loadCount;

    if (this->hInstance) {
        return 1;
    }

    InitializeCriticalSection(&s_CrawlCritsect);
    EnterCriticalSection(&s_CrawlCritsect);

    HINSTANCE module = GetModuleHandleA("dbghelp.dll");

    if (!module) {
        module = LoadLibraryA("dbghelp.dll");
    }

    if (module
        && (this->StackWalk = reinterpret_cast<decltype(this->StackWalk)>(GetProcAddress(module, "StackWalk" DBGHELP_64)))
        && (this->SymFunctionTableAccess = reinterpret_cast<decltype(this->SymFunctionTableAccess)>(GetProcAddress(module, "SymFunctionTableAccess" DBGHELP_64)))
        && (this->SymGetLineFromAddr = reinterpret_cast<decltype(this->SymGetLineFromAddr)>(GetProcAddress(module, "SymGetLineFromAddr" DBGHELP_64)))
        && (this->SymGetModuleBase = reinterpret_cast<decltype(this->SymGetModuleBase)>(GetProcAddress(module, "SymGetModuleBase" DBGHELP_64)))
        && (this->SymGetModuleInfo = reinterpret_cast<decltype(this->SymGetModuleInfo)>(GetProcAddress(module, "SymGetModuleInfo" DBGHELP_64)))
        && (this->SymGetOptions = reinterpret_cast<decltype(this->SymGetOptions)>(GetProcAddress(module, "SymGetOptions")))
        && (this->SymGetSymFromAddr = reinterpret_cast<decltype(this->SymGetSymFromAddr)>(GetProcAddress(module, "SymGetSymFromAddr" DBGHELP_64)))
        && (this->SymInitialize = reinterpret_cast<decltype(this->SymInitialize)>(GetProcAddress(module, "SymInitialize")))
        && (this->SymCleanup = reinterpret_cast<decltype(this->SymCleanup)>(GetProcAddress(module, "SymCleanup")))
        && (this->SymSetOptions = reinterpret_cast<decltype(this->SymSetOptions)>(GetProcAddress(module, "SymSetOptions")))
        && (this->SymEnumerateModules = reinterpret_cast<decltype(this->SymEnumerateModules)>(GetProcAddress(module, "SymEnumerateModules" DBGHELP_64)))
        && (this->SymEnumerateSymbols = reinterpret_cast<decltype(this->SymEnumerateSymbols)>(GetProcAddress(module, "SymEnumerateSymbols" DBGHELP_64)))) {
        this->MiniDumpWriteDump = reinterpret_cast<decltype(this->MiniDumpWriteDump)>(GetProcAddress(module, "MiniDumpWriteDump"));
        this->hInstance = module;
        LeaveCriticalSection(&s_CrawlCritsect);

        return 1;
    }

    LeaveCriticalSection(&s_CrawlCritsect);
    DeleteCriticalSection(&s_CrawlCritsect);

    return 0;
}

// INLINED
inline void CDbgHelpDll::Unload() {
    --this->loadCount;
}

// OFFSET: 0x0077CBD0
static void sLogSeparatorLine(void (*logLineProc)(void*, const char*, ...), void* logLineProcParam, char character, int32_t longLine) {
    char line[80];
    uint32_t length = longLine ? 78 : 40;
    memset(line, character, length);
    line[length] = 0;
    logLineProc(logLineProcParam, "%s", line);
}

// OFFSET: 0x0077CC10
static void sLogHeader(void (*logLineProc)(void*, const char*, ...), void* logLineProcParam, const char* headerString) {
    logLineProc(logLineProcParam, "");
    sLogSeparatorLine(logLineProc, logLineProcParam, '-', 0);
    logLineProc(logLineProcParam, "    %s", headerString);
    sLogSeparatorLine(logLineProc, logLineProcParam, '-', 0);
    logLineProc(logLineProcParam, "");
}

// OFFSET: 0x0077CCA0
static void sLogMemoryHexDump(void (*logLineProc)(void*, const char*, ...), void* logLineProcParam, uint8_t* address, uint32_t numBytes, int32_t alignedLines) {
    char buffer[80 + LOG_PTR_EXTRA];
    uint32_t numLines = numBytes >> 4;
    uint32_t offset = reinterpret_cast<uintptr_t>(address) & 0xF;

    if (alignedLines) {
        memset(buffer, ' ', 78 + LOG_PTR_EXTRA);
        buffer[0] = '*';
        char* marker = &buffer[10 + LOG_PTR_EXTRA + 3 * offset + (offset >> 2)];
        buffer[78 + LOG_PTR_EXTRA] = 0;
        buffer[2] = '=';
        memcpy(&buffer[4], "addr", 4);
        marker[0] = '*';
        marker[1] = '*';
        buffer[62 + LOG_PTR_EXTRA + offset] = '*';
        logLineProc(logLineProcParam, "%s", buffer);

        if (offset) {
            address -= offset;
            ++numLines;
        }
    }

    for (; numLines; --numLines) {
        char* text = buffer + sprintf(buffer, LOG_PTR ": ", reinterpret_cast<uintptr_t>(address));

        if (IsBadReadPtr(address, 16)) {
            strcpy(text, "<can't read from this address>");
            logLineProc(logLineProcParam, "%s", buffer);
            return;
        }

        text += sprintf(text, "%02X %02X %02X %02X  ", address[0], address[1], address[2], address[3]);
        text += sprintf(text, "%02X %02X %02X %02X  ", address[4], address[5], address[6], address[7]);
        text += sprintf(text, "%02X %02X %02X %02X  ", address[8], address[9], address[10], address[11]);
        text += sprintf(text, "%02X %02X %02X %02X  ", address[12], address[13], address[14], address[15]);

        for (uint32_t i = 0; i < 16; ++i) {
            text[i] = isprint(address[i]) ? address[i] : '.';
        }

        text[16] = 0;
        logLineProc(logLineProcParam, "%s", buffer);
        address += 16;
    }
}

// OFFSET: 0x0077CE70
static const char* sGetPathLeaf(const char* path) {
    const char* leaf = nullptr;
    const char* slash = strrchr(path, '/');

    if (slash) {
        leaf = slash;
    }

    const char* backslash = strrchr(path, '\\');

    if (backslash && backslash > leaf) {
        leaf = backslash;
    }

    const char* colon = strrchr(path, ':');

    if (colon && colon > leaf) {
        leaf = colon;
    }

    return leaf ? leaf + 1 : path;
}

// OFFSET: 0x0077CEC0
static void sLogExeFile(void (*logLineProc)(void*, const char*, ...), void* logLineProcParam) {
    char exeFullPath[MAX_PATH];
    GetModuleFileNameA(nullptr, exeFullPath, sizeof(exeFullPath));
    logLineProc(logLineProcParam, "%-10s%s", "Exe:", exeFullPath);
}

// OFFSET: 0x0077CF00
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

// OFFSET: 0x0077CF70
static void sLogUserName(void (*logLineProc)(void*, const char*, ...), void* logLineProcParam) {
    char userName[MAX_PATH];
    DWORD size = 257;

    if (!GetUserNameA(userName, &size)) {
        strcpy(userName, "<unknown>");
    }

    logLineProc(logLineProcParam, "%-10s%s", "User:", userName);
}

// OFFSET: 0x0077CFE0
static void sLogComputerName(void (*logLineProc)(void*, const char*, ...), void* logLineProcParam) {
    char computerName[16];
    DWORD size = sizeof(computerName);

    if (!GetComputerNameA(computerName, &size)) {
        strcpy(computerName, "<unknown>");
    }

    logLineProc(logLineProcParam, "%-10s%s", "Computer:", computerName);
}

// OFFSET: 0x0077D040
static void sLogMemory(uint32_t logOptions, void (*logLineProc)(void*, const char*, ...), void* logLineProcParam, uint8_t* instructionPointer, uint8_t* stackPointer) {
    sLogHeader(logLineProc, logLineProcParam, "Memory Dump");

    if (logOptions & LOG_OPTION_CODE) {
        logLineProc(logLineProcParam, "Code: %d bytes starting at (EIP = " LOG_PTR ")", 16, reinterpret_cast<uintptr_t>(instructionPointer));
        logLineProc(logLineProcParam, "");
        sLogMemoryHexDump(logLineProc, logLineProcParam, instructionPointer, 16, 0);
        logLineProc(logLineProcParam, "");
        logLineProc(logLineProcParam, "");
    }

    if (logOptions & LOG_OPTION_STACK) {
        logLineProc(logLineProcParam, "Stack: %d bytes starting at (ESP = " LOG_PTR ")", 1024, reinterpret_cast<uintptr_t>(stackPointer));
        logLineProc(logLineProcParam, "");
        sLogMemoryHexDump(logLineProc, logLineProcParam, stackPointer, 1024, 1);
        logLineProc(logLineProcParam, "");
        logLineProc(logLineProcParam, "");
    }
}

// OFFSET: 0x0077D0F0
static int __cdecl sModuleCompareProc(const void* elem1, const void* elem2) {
    const LOADEDMODULE* module1 = static_cast<const LOADEDMODULE*>(elem1);
    const LOADEDMODULE* module2 = static_cast<const LOADEDMODULE*>(elem2);

    if (module1->base > module2->base) {
        return 1;
    }

    return module1->base < module2->base ? -1 : 0;
}

// OFFSET: 0x0077D120
static const char* GetFileNameFromPath(const char* path, size_t maxchars) {
    if (!memchr(path, 0, maxchars)) {
        return "";
    }

    char* leaf = SStrChrR(const_cast<char*>(path), '/');
    char* backslash = SStrChrR(const_cast<char*>(path), '\\');

    if (!leaf || (backslash && leaf < backslash)) {
        leaf = backslash;
    }

    return leaf ? leaf + 1 : path;
}

// OFFSET: 0x0077D170
static void sReadCodeViewInfo(MODULEDEBUGINFO* info, uint32_t size, uint32_t type, const uint8_t* data) {
    if (size < 4) {
        return;
    }

    const uint32_t* header = reinterpret_cast<const uint32_t*>(data);

    if (header[0] == CV_SIGNATURE_NB10) {
        if (size >= 0x14) {
            info->signature = header[2];
            SStrCopy(info->pdbName, GetFileNameFromPath(reinterpret_cast<const char*>(data + 0x10), 64), sizeof(info->pdbName));
            info->age = header[3];
            memset(&info->guid, 0, sizeof(info->guid));
        }
    } else if (header[0] == CV_SIGNATURE_RSDS && size >= 0x1C) {
        SStrCopy(info->pdbName, GetFileNameFromPath(reinterpret_cast<const char*>(data + 0x18), MAX_PATH), sizeof(info->pdbName));
        info->age = header[5];
        memcpy(&info->guid, data + 4, sizeof(info->guid));
    }
}

// OFFSET: 0x0077D210
static void sGetLogicalAddress(void* addr, char* moduleName, uint32_t moduleNameSize, uint32_t* section, uint32_t* offset) {
    lstrcpynA(moduleName, "<unknown>", moduleNameSize);
    *offset = 0;
    *section = 0;

    MEMORY_BASIC_INFORMATION memInfo;

    if (!VirtualQuery(addr, &memInfo, sizeof(memInfo))) {
        return;
    }

    HMODULE module = static_cast<HMODULE>(memInfo.AllocationBase);

    if (!module) {
        module = GetModuleHandleA(nullptr);
    }

    if (!GetModuleFileNameA(module, moduleName, moduleNameSize)) {
        lstrcpynA(moduleName, "<unknown>", moduleNameSize);
        return;
    }

    if (!module) {
        return;
    }

    const IMAGE_DOS_HEADER* dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);

    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE || !dosHeader->e_lfanew) {
        return;
    }

    const IMAGE_NT_HEADERS* ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const uint8_t*>(module) + dosHeader->e_lfanew);

    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) {
        return;
    }

    const IMAGE_SECTION_HEADER* sectionHeader = IMAGE_FIRST_SECTION(ntHeaders);
    uint32_t rva = static_cast<uint32_t>(static_cast<uint8_t*>(addr) - reinterpret_cast<uint8_t*>(module));

    for (uint32_t i = 0; i < ntHeaders->FileHeader.NumberOfSections; ++i, ++sectionHeader) {
        uint32_t sectionStart = sectionHeader->VirtualAddress;
        uint32_t sectionLength = sectionHeader->SizeOfRawData > sectionHeader->Misc.VirtualSize ? sectionHeader->SizeOfRawData : sectionHeader->Misc.VirtualSize;

        if (rva >= sectionStart && rva <= sectionStart + sectionLength) {
            *section = i + 1;
            *offset = rva - sectionStart;
            return;
        }
    }
}

// OFFSET: 0x0077D310
static int32_t sDbgHelpGetStackFrameInfo(DWORD_PTR address, char* moduleName, char* symbolName, uint32_t* symbolDisplacement, char* fileName, uint32_t* lineNumber) {
    int32_t err = 0;

    IMAGEHLP_MODULE module;
    memset(&module, 0, sizeof(module));
    module.SizeOfStruct = sizeof(module);

    if (sgDbgHelpDll.SymGetModuleInfo(GetCurrentProcess(), address, &module)) {
        strcpy(moduleName, sGetPathLeaf(module.ImageName));
    } else {
        strcpy(moduleName, "<unknown module>");
        err = 1;
    }

    *fileName = 0;
    *lineNumber = 0;

    uint8_t buffer[sizeof(IMAGEHLP_SYMBOL) + 256];
    memset(buffer, 0, sizeof(buffer));

    IMAGEHLP_SYMBOL* symbol = reinterpret_cast<IMAGEHLP_SYMBOL*>(buffer);
    symbol->SizeOfStruct = sizeof(IMAGEHLP_SYMBOL);
    symbol->Address = address;
    symbol->MaxNameLength = 256;

    DWORD_PTR displacement;

    if (!sgDbgHelpDll.SymGetSymFromAddr(GetCurrentProcess(), address, &displacement, symbol)) {
        strcpy(symbolName, "<unknown symbol>");
        *symbolDisplacement = 0;

        return err | 4;
    }

    SStrCopy(symbolName, symbol->Name, 256);
    *symbolDisplacement = static_cast<uint32_t>(displacement);

    IMAGEHLP_LINE line;
    memset(&line, 0, sizeof(line));
    line.SizeOfStruct = sizeof(line);

    if (!sgDbgHelpDll.SymGetLineFromAddr(GetCurrentProcess(), address, reinterpret_cast<PDWORD>(&displacement), &line)) {
        return err | 2;
    }

    strncpy(fileName, sGetPathLeaf(line.FileName), 259);
    fileName[259] = 0;
    *lineNumber = line.LineNumber;

    return err;
}

// INLINED
inline void sLogVerboseMessage(uint32_t logOptions, void (*logLineProc)(void*, const char*, ...), void* logLineProcParam, const char* format, uint32_t error) {
    if (logOptions & LOG_OPTION_VERBOSE) {
        logLineProc(logLineProcParam, format, error);
    }
}

// OFFSET: 0x0077D4F0
static void sLogDbgHelpStackFrame(uint32_t logOptions, void (*logLineProc)(void*, const char*, ...), void* logLineProcParam, const STACKFRAME* stackFrame) {
    char symbolName[256];
    char fileName[MAX_PATH];
    char moduleName[32];
    uint32_t lineNumber;
    uint32_t symbolDisplacement;
    DWORD_PTR address = stackFrame->AddrPC.Offset;
    int32_t err = sDbgHelpGetStackFrameInfo(address, moduleName, symbolName, &symbolDisplacement, fileName, &lineNumber);

    if (err & 1) {
        sLogVerboseMessage(logOptions, logLineProc, logLineProcParam, "**** SymGetModuleInfo() failed, error: %d", GetLastError());
    }

    if (err & 2) {
        sLogVerboseMessage(logOptions, logLineProc, logLineProcParam, "**** SymGetLineFromAddr() failed, error: %d", GetLastError());
    }

    if (err & 4) {
        sLogVerboseMessage(logOptions, logLineProc, logLineProcParam, "**** SymGetSymFromAddr() failed, error: %d", GetLastError());
    }

    if (logOptions & LOG_OPTION_FRAMEPARAMS) {
        const char* format = fileName[0] ? LOG_PTR " %-12s %s+%d (0x" LOG_PTR ",0x" LOG_PTR ",0x" LOG_PTR ",0x" LOG_PTR ") (%s,%d)" : LOG_PTR " %-12s %s+%d (0x" LOG_PTR ",0x" LOG_PTR ",0x" LOG_PTR ",0x" LOG_PTR ")";
        logLineProc(logLineProcParam, format, address, moduleName, symbolName, symbolDisplacement, stackFrame->Params[0], stackFrame->Params[1], stackFrame->Params[2], stackFrame->Params[3], fileName, lineNumber);
    } else {
        const char* format = fileName[0] ? LOG_PTR " %-12s %s+%d (%s,%d)" : LOG_PTR " %-12s %s+%d";
        logLineProc(logLineProcParam, format, address, moduleName, symbolName, symbolDisplacement, fileName, lineNumber);
    }
}

// OFFSET: 0x0077D630
static BOOL sSymInitialize(HANDLE process) {
    char path[MAX_PATH];
    path[0] = 0;
    GetModuleFileNameA(nullptr, path, sizeof(path));

    char* slash = SStrChrR(path, '\\');

    if (slash) {
        *slash = 0;
    }

    return sgDbgHelpDll.SymInitialize(process, path, TRUE);
}

// OFFSET: 0x0077D690
static void STORMAPI CmdMemOutput(void* hOutput, const MEMREPORTENTRY* entry) {
    MEMDUMP* dump = static_cast<MEMDUMP*>(hOutput);
    dump->logLineProc(dump->logLineProcParam, "%5d %5d %s(%d)", entry->unk_0010, entry->unk_0014, entry->unk_0024, entry->unk_0020 >> 1);
}

// OFFSET: 0x0077D6C0
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

// INLINED: [0x0077D6CF->0x0077D6E7]
void SMemGenerateReport(SMEMREPORTTYPE type, void (STORMAPI* callback)(void*, const MEMREPORTENTRY*), void* context) {
    StormCallService(STORM_SERVICE_GENERATEREPORT, type, callback, context);
}

// OFFSET: 0x0077D710
static void sEnumerateThreads(DWORD* threads, uint32_t maxThreads, uint32_t* numThreads, uint32_t* totalThreads) {
    *numThreads = 0;
    *totalThreads = 0;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);

    if (snapshot != INVALID_HANDLE_VALUE) {
        THREADENTRY32 entry;
        entry.dwSize = sizeof(entry);

        if (Thread32First(snapshot, &entry)) {
            DWORD processId = GetCurrentProcessId();

            while (Thread32Next(snapshot, &entry)) {
                if (entry.th32ThreadID && entry.th32OwnerProcessID == processId) {
                    if (*numThreads < maxThreads) {
                        threads[(*numThreads)++] = entry.th32ThreadID;
                    }

                    ++*totalThreads;
                }
            }
        }

        CloseHandle(snapshot);
    }

    if (!*numThreads) {
        threads[0] = GetCurrentThreadId();
        *numThreads = 1;
        *totalThreads = 1;
    }
}

// OFFSET: 0x0077D7C0
static void sLogDbgHelpStackFrames(STACKFRAME* stackFrame, HANDLE process, HANDLE thread, uint32_t logOptions, void (*logLineProc)(void*, const char*, ...), void* logLineProcParam, CONTEXT* context, uint32_t stackFramesToSkip) {
    const DWORD machineType = IMAGE_FILE_MACHINE_AMD64;

    for (uint32_t frame = 0; frame < 100; ++frame) {
        if (!sgDbgHelpDll.StackWalk(machineType, process, thread, stackFrame, context, nullptr, sgDbgHelpDll.SymFunctionTableAccess, sgDbgHelpDll.SymGetModuleBase, nullptr)) {
            sLogVerboseMessage(logOptions, logLineProc, logLineProcParam, "**** StackWalk() returned FALSE, error: %d", GetLastError());
            return;
        }

        if (!stackFrame->AddrPC.Offset) {
            sLogVerboseMessage(logOptions, logLineProc, logLineProcParam, "**** StackWalk() returned zero address - skipping stack frame", 0);
        } else if (frame >= stackFramesToSkip) {
            sLogDbgHelpStackFrame(logOptions, logLineProc, logLineProcParam, stackFrame);
        }
    }
}

// OFFSET: 0x0077D860
static void sLogDbgHelpStackTrace(uint32_t logOptions, void (*logLineProc)(void*, const char*, ...), void* logLineProcParam, DWORD_PTR registerEip, DWORD_PTR registerEbp, DWORD_PTR registerEsp, uint32_t stackFramesToSkip) {
    sLogHeader(logLineProc, logLineProcParam, "Stack Trace (Using DBGHELP.DLL)");

    if (!sgDbgHelpDll.Load()) {
        logLineProc(logLineProcParam, "****  Couldn't load DBGHELP.DLL, error: %d", GetLastError());
    } else {
        sgDbgHelpDll.SymSetOptions(sgDbgHelpDll.SymGetOptions() | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
        HANDLE process = GetCurrentProcess();

        if (!sSymInitialize(process)) {
            logLineProc(logLineProcParam, "****  Couldn't initialize Debug Help library, error: %d", GetLastError());
        } else if (logOptions & LOG_OPTION_DBGHELPSTACK) {
            DWORD currentThreadId = GetCurrentThreadId();
            DWORD threads[128];
            uint32_t numThreads = 0;
            uint32_t totalThreads = 0;
            sEnumerateThreads(threads, 128, &numThreads, &totalThreads);
            logLineProc(logLineProcParam, "Showing %d/%d threads...", numThreads, totalThreads);

            for (uint32_t i = 0; i < numThreads; ++i) {
                DWORD threadId = threads[i];
                HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, threadId);
                logLineProc(logLineProcParam, "");
                logLineProc(logLineProcParam, threadId == currentThreadId ? "--- Thread ID: %d [Current Thread] ---" : "--- Thread ID: %d ---", threadId);

                if (!thread && (numThreads != 1 || !(thread = GetCurrentThread()))) {
                    logLineProc(logLineProcParam, "**** Unable to gain access to the thread, error: ", GetLastError());
                    continue;
                }

                STACKFRAME stackFrame;
                CONTEXT context;
                memset(&stackFrame, 0, sizeof(stackFrame));
                memset(&context, 0, sizeof(context));

                DWORD_PTR stackPointer;

                if (threadId != currentThreadId) {
                    SuspendThread(thread);
                    context.ContextFlags = CONTEXT_FULL;
                    BOOL result = GetThreadContext(thread, &context);
                    ResumeThread(thread);

                    if (!result) {
                        logLineProc(logLineProcParam, "**** Unable to retrieve thread context, error: %d", GetLastError());
                        continue;
                    }

                    stackFrame.AddrPC.Offset = context.Rip;
                    stackFrame.AddrFrame.Offset = context.Rbp;
                    stackPointer = context.Rsp;
                } else {
                    stackFrame.AddrPC.Offset = registerEip;
                    stackFrame.AddrFrame.Offset = registerEbp;
                    stackPointer = registerEsp;
                    RtlCaptureContext(&context);
                }

                stackFrame.AddrPC.Mode = AddrModeFlat;
                stackFrame.AddrFrame.Mode = AddrModeFlat;
                stackFrame.AddrStack.Offset = stackPointer;
                stackFrame.AddrStack.Mode = AddrModeFlat;

                sLogDbgHelpStackFrames(&stackFrame, process, thread, logOptions, logLineProc, logLineProcParam, &context, threadId != currentThreadId ? 1 : stackFramesToSkip);
                CloseHandle(thread);
            }
        }
    }

    sgDbgHelpDll.Unload();
    logLineProc(logLineProcParam, "");
}

// OFFSET: 0x0077DB10
static void sLogX86ManualStackTrace(void (*logLineProc)(void*, const char*, ...), void* logLineProcParam, DWORD_PTR registerEip, DWORD_PTR registerEbp, uint32_t stackFramesToSkip) {
    sLogHeader(logLineProc, logLineProcParam, "Stack Trace (Manual)");
    logLineProc(logLineProcParam, "%s", "Address  Frame    Logical addr  Module");
    logLineProc(logLineProcParam, "");

    DWORD currentThreadId = GetCurrentThreadId();
    DWORD threads[128];
    uint32_t numThreads = 0;
    uint32_t totalThreads = 0;
    sEnumerateThreads(threads, 128, &numThreads, &totalThreads);
    logLineProc(logLineProcParam, "Showing %d/%d threads...", numThreads, totalThreads);

    for (uint32_t i = 0; i < numThreads; ++i) {
        DWORD threadId = threads[i];
        HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, threadId);
        logLineProc(logLineProcParam, "");
        logLineProc(logLineProcParam, threadId == currentThreadId ? "--- Thread ID: %d [Current Thread] ---" : "--- Thread ID: %d ---", threadId);

        if (!thread && numThreads == 1) {
            thread = GetCurrentThread();
        }

        DWORD_PTR eip;
        DWORD_PTR* ebp;

        if (threadId == currentThreadId) {
            eip = registerEip;
            ebp = reinterpret_cast<DWORD_PTR*>(registerEbp);
        } else {
            SuspendThread(thread);

            CONTEXT context;
            memset(&context, 0, sizeof(context));
            context.ContextFlags = CONTEXT_FULL;
            BOOL result = GetThreadContext(thread, &context);
            ResumeThread(thread);

            if (!result) {
                logLineProc(logLineProcParam, "**** Unable to retrieve thread context, error: %d", GetLastError());
                continue;
            }

            eip = context.Rip;
            ebp = reinterpret_cast<DWORD_PTR*>(context.Rbp);
        }

        for (uint32_t frame = 0; frame < 100; ++frame) {
            if (frame >= (threadId != currentThreadId ? 1 : stackFramesToSkip)) {
                char modulePath[MAX_PATH];
                uint32_t section;
                uint32_t offset;
                sGetLogicalAddress(reinterpret_cast<void*>(eip), modulePath, sizeof(modulePath), &section, &offset);
                logLineProc(logLineProcParam, LOG_PTR " " LOG_PTR " %04X:%08X %s", eip, reinterpret_cast<DWORD_PTR>(ebp), section, offset, modulePath);
            }

            if (IsBadWritePtr(ebp, 2 * sizeof(DWORD_PTR))) {
                break;
            }

            eip = ebp[1];
            DWORD_PTR* previous = ebp;
            ebp = reinterpret_cast<DWORD_PTR*>(ebp[0]);

            if (reinterpret_cast<DWORD_PTR>(ebp) & 3) {
                break;
            }

            if (ebp <= previous) {
                break;
            }

            if (IsBadWritePtr(ebp, 2 * sizeof(DWORD_PTR))) {
                break;
            }
        }

        CloseHandle(thread);
    }
}

// OFFSET: 0x0077DD50
static void sLogX86ContextRegisters(void (*logLineProc)(void*, const char*, ...), void* logLineProcParam, const CONTEXT* context) {
    sLogHeader(logLineProc, logLineProcParam, "x64 Registers");
    logLineProc(logLineProcParam, "RAX=%016llX  RBX=%016llX  RCX=%016llX  RDX=%016llX", context->Rax, context->Rbx, context->Rcx, context->Rdx);
    logLineProc(logLineProcParam, "RSI=%016llX  RDI=%016llX  RBP=%016llX  RSP=%016llX", context->Rsi, context->Rdi, context->Rbp, context->Rsp);
    logLineProc(logLineProcParam, "R8 =%016llX  R9 =%016llX  R10=%016llX  R11=%016llX", context->R8, context->R9, context->R10, context->R11);
    logLineProc(logLineProcParam, "R12=%016llX  R13=%016llX  R14=%016llX  R15=%016llX", context->R12, context->R13, context->R14, context->R15);
    logLineProc(logLineProcParam, "RIP=%016llX  FLG=%08X", context->Rip, context->EFlags);
    logLineProc(logLineProcParam, "CS =%04X      DS =%04X      ES =%04X      SS =%04X      FS =%04X      GS =%04X", context->SegCs, context->SegDs, context->SegEs, context->SegSs, context->SegFs, context->SegGs);
    logLineProc(logLineProcParam, "");
}

// OFFSET: 0x0077DE10
void LoadMachineStateSymbols() {
    sgDbgHelpDll.Load();
}

// OFFSET: 0x0077DE20
void UnloadMachineStateSymbols() {
    sgDbgHelpDll.Unload();
}

// OFFSET: 0x0077DE30
void LogComputerInfoHeader(uint32_t logOptions, void (*logLineProc)(void*, const char*, ...), void* logLineProcParam, const char* headerTitleLine, const SYSTEMTIME* time) {
    if (InterlockedIncrement(&sgRecursionLevel) == 1) {
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
                GetLocalTime(&currentTime);
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

    InterlockedDecrement(&sgRecursionLevel);
}

// OFFSET: 0x0077DF40
static DWORD WINAPI MiniDumpThreadProc(void* param) {
    MiniDumpParam* miniDumpParam = static_cast<MiniDumpParam*>(param);

    if (!sgDbgHelpDll.Load()) {
        miniDumpParam->result = 0;
        return 0;
    }

    int32_t result = 0;

    if (sgDbgHelpDll.MiniDumpWriteDump) {
        MINIDUMP_EXCEPTION_INFORMATION miniDumpExceptionInfo;
        miniDumpExceptionInfo.ThreadId = miniDumpParam->threadId;
        miniDumpExceptionInfo.ExceptionPointers = miniDumpParam->exceptionPointers;
        miniDumpExceptionInfo.ClientPointers = FALSE;

        MINIDUMP_USER_STREAM miniDumpUserStreamArray[16];
        uint32_t count = 0;

        for (uint32_t i = 0; i < miniDumpParam->userStringCount; ++i) {
            const char* string = miniDumpParam->userStrings[i];

            if (string && count < 16) {
                miniDumpUserStreamArray[count].Type = count + 0x1000;
                miniDumpUserStreamArray[count].BufferSize = static_cast<ULONG>(strlen(string) + 1);
                miniDumpUserStreamArray[count].Buffer = const_cast<char*>(string);
                ++count;
            }
        }

        MINIDUMP_USER_STREAM_INFORMATION miniDumpUserStreamInfo;
        miniDumpUserStreamInfo.UserStreamCount = count;
        miniDumpUserStreamInfo.UserStreamArray = miniDumpUserStreamArray;
        result = sgDbgHelpDll.MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), miniDumpParam->logfile, MiniDumpWithIndirectlyReferencedMemory, miniDumpParam->exceptionPointers ? &miniDumpExceptionInfo : nullptr, &miniDumpUserStreamInfo, nullptr);
    }

    sgDbgHelpDll.Unload();
    miniDumpParam->result = result;

    return 0;
}

// OFFSET: 0x0077E040
int32_t LogMiniDump(void* logfile, EXCEPTION_POINTERS* exceptionpointers, uint32_t count, const char** strings) {
    int32_t result = 0;
    DWORD threadId = 0;

    if (InterlockedIncrement(&sgRecursionLevel) == 1) {
        MiniDumpParam miniDumpParam;
        miniDumpParam.logfile = logfile;
        miniDumpParam.threadId = GetCurrentThreadId();
        miniDumpParam.userStringCount = count;
        miniDumpParam.userStrings = strings;
        miniDumpParam.exceptionPointers = exceptionpointers;

        if (!GetModuleHandleA("psapi.dll")) {
            LoadLibraryA("psapi.dll");
        }

        HANDLE thread = CreateThread(nullptr, 0, MiniDumpThreadProc, &miniDumpParam, 0, &threadId);

        if (thread) {
            WaitForSingleObject(thread, INFINITE);
            CloseHandle(thread);
            result = miniDumpParam.result;
        }
    }

    InterlockedDecrement(&sgRecursionLevel);

    return result;
}

// OFFSET: 0x0077E0E0
int32_t LogMiniDumpIsAvailable() {
    if (!sgDbgHelpDll.Load()) {
        return 0;
    }

    int32_t result = sgDbgHelpDll.MiniDumpWriteDump != nullptr;
    sgDbgHelpDll.Unload();

    return result;
}

// OFFSET: [0x0077E110, 0x0077E190]
template <class T>
static void sReadDebugDirectory(const T* ntHeaders, MODULEDEBUGINFO* info, const uint8_t* base) {
    info->timeDateStamp = ntHeaders->FileHeader.TimeDateStamp;

    if (ntHeaders->OptionalHeader.NumberOfRvaAndSizes < IMAGE_DIRECTORY_ENTRY_DEBUG + 1) {
        return;
    }

    const IMAGE_DATA_DIRECTORY* directory = &ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];

    if (!directory->VirtualAddress || directory->Size < sizeof(IMAGE_DEBUG_DIRECTORY)) {
        return;
    }

    const IMAGE_DEBUG_DIRECTORY* debugDirectory = reinterpret_cast<const IMAGE_DEBUG_DIRECTORY*>(base + directory->VirtualAddress);
    uint32_t count = directory->Size / sizeof(IMAGE_DEBUG_DIRECTORY);

    for (uint32_t i = 0; i < count; ++i, ++debugDirectory) {
        if (debugDirectory->Type == IMAGE_DEBUG_TYPE_CODEVIEW) {
            sReadCodeViewInfo(info, debugDirectory->SizeOfData, IMAGE_DEBUG_TYPE_CODEVIEW, base + debugDirectory->AddressOfRawData);
            return;
        }
    }
}

// OFFSET: 0x0077E230
static void sReadModuleDebugInfo(MODULEDEBUGINFO* info, const uint8_t* base) {
    memset(info, 0, sizeof(*info));

    const IMAGE_DOS_HEADER* dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);

    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
        return;
    }

    if (dosHeader->e_res2[8] == 0xB74F && dosHeader->e_res2[9] == 0x2D98) {
        info->checksum = dosHeader->e_res2[6] + (dosHeader->e_res2[7] << 16);

        if (info->checksum) {
            info->checksum *= 0xFDCA75BB;
        }
    }

    const IMAGE_NT_HEADERS32* ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dosHeader->e_lfanew);

    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) {
        return;
    }

    switch (ntHeaders->OptionalHeader.Magic) {
        case IMAGE_NT_OPTIONAL_HDR32_MAGIC:
            sReadDebugDirectory(ntHeaders, info, base);
            break;
        case IMAGE_NT_OPTIONAL_HDR64_MAGIC:
            sReadDebugDirectory(reinterpret_cast<const IMAGE_NT_HEADERS64*>(ntHeaders), info, base);
            break;
    }
}

// OFFSET: 0x0077E2B0
static void sLogModuleLine(void (*logLineProc)(void*, const char*, ...), void* logLineProcParam, void* base, const char* name, uint32_t size) {
    MODULEDEBUGINFO info;

    __try {
        sReadModuleDebugInfo(&info, static_cast<const uint8_t*>(base));
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION || GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        memset(&info, 0, sizeof(info));
    }

    char checksum[10];

    if (info.checksum) {
        sprintf_s(checksum, " %08lX", info.checksum);
    } else {
        checksum[0] = 0;
    }

    logLineProc(logLineProcParam, "DBG-MODULE<%08llX %08lX \"%s\" \"%s\" %lu {%08lx-%04hx-%04hx-%02x%02x%02x%02x%02x%02x%02x%02x} %lu %lu%s>", static_cast<uint64_t>(reinterpret_cast<uintptr_t>(base)), size, name, info.pdbName, info.signature, info.guid.Data1, info.guid.Data2, info.guid.Data3, info.guid.Data4[0], info.guid.Data4[1], info.guid.Data4[2], info.guid.Data4[3], info.guid.Data4[4], info.guid.Data4[5], info.guid.Data4[6], info.guid.Data4[7], info.age, info.timeDateStamp, checksum);
}

// OFFSET: 0x0077E3D0
static void sLogLoadedModules(void (*logLineProc)(void*, const char*, ...), void* logLineProcParam) {
    LOADEDMODULE modules[256];

    logLineProc(logLineProcParam, "");
    sLogHeader(logLineProc, logLineProcParam, "Loaded Modules");

    size_t numModules = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0);

    if (snapshot != INVALID_HANDLE_VALUE) {
        MODULEENTRY32 entry;
        entry.dwSize = sizeof(entry);

        if (Module32First(snapshot, &entry)) {
            do {
                if (numModules == 256) {
                    break;
                }

                LOADEDMODULE* module = &modules[numModules];
                module->base = entry.modBaseAddr;
                module->size = entry.modBaseSize;
                SStrCopy(module->name, entry.szModule, sizeof(module->name));
                ++numModules;
                entry.dwSize = sizeof(entry);
            } while (Module32Next(snapshot, &entry));
        }

        CloseHandle(snapshot);
    } else if (GetModuleHandleW(L"ntdll.dll")) {
        __try {
            LIST_ENTRY* head = &NtCurrentTeb()->ProcessEnvironmentBlock->Ldr->InMemoryOrderModuleList;

            for (LIST_ENTRY* link = head->Flink; link != head && numModules != 256; link = link->Flink) {
                LDR_DATA_TABLE_ENTRY* ldrEntry = CONTAINING_RECORD(link, LDR_DATA_TABLE_ENTRY, InMemoryOrderLinks);
                LOADEDMODULE* module = &modules[numModules];
                module->base = ldrEntry->DllBase;
                module->size = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ldrEntry->Reserved3[1]));

                uint32_t length;

                if (SUniConvertUTF16to8(module->name, 255, reinterpret_cast<const uint16_t*>(ldrEntry->FullDllName.Buffer), ldrEntry->FullDllName.Length, &length, nullptr) > 0) {
                    length = 0;
                }

                module->name[length] = 0;

                char* leaf = SStrChrR(module->name, '/');
                char* backslash = SStrChrR(module->name, '\\');

                if (backslash && (!leaf || leaf < backslash)) {
                    leaf = backslash;
                }

                if (leaf) {
                    ++leaf;
                }

                memmove(module->name, leaf, module->name + length - leaf + 1);
                ++numModules;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    qsort(modules, numModules, sizeof(LOADEDMODULE), sModuleCompareProc);

    for (size_t i = 0; i < numModules; ++i) {
        sLogModuleLine(logLineProc, logLineProcParam, modules[i].base, modules[i].name, modules[i].size);
    }

    if (!numModules) {
        logLineProc(logLineProcParam, "****  CreateToolhelp32Snapshot couldn't enumerate modules, error: %d", GetLastError());
    }
}

// OFFSET: 0x0077E670
void LogMachineState(uint32_t logOptions, void (*logLineProc)(void*, const char*, ...), void* logLineProcParam, uint32_t stackFramesToSkip, CONTEXT* context) {
    if (InterlockedIncrement(&sgRecursionLevel) == 1) {
        if (!logOptions || (logOptions & LOG_OPTION_DEFAULT)) {
            logOptions |= LOG_OPTION_DBGHELPSTACK | LOG_OPTION_MANUALSTACK | LOG_OPTION_STACK | LOG_OPTION_MODULES;

            if (context) {
                logOptions |= LOG_OPTION_REGISTERS | LOG_OPTION_CODE;
            }
        }

        DWORD_PTR registerEip;
        DWORD_PTR registerEbp;
        DWORD_PTR registerEsp;

        if (context) {
            registerEip = context->Rip;
            registerEbp = context->Rbp;
            registerEsp = context->Rsp;
            stackFramesToSkip = 0;
        } else {
            CONTEXT current;
            RtlCaptureContext(&current);
            registerEip = current.Rip;
            registerEbp = current.Rbp;
            registerEsp = current.Rsp;
            logOptions &= ~static_cast<uint32_t>(LOG_OPTION_REGISTERS | LOG_OPTION_CODE);
            stackFramesToSkip = stackFramesToSkip + 1;
        }

        sLogSeparatorLine(logLineProc, logLineProcParam, '-', 1);

        if ((logOptions & LOG_OPTION_REGISTERS) && context) {
            sLogX86ContextRegisters(logLineProc, logLineProcParam, context);
        }

        if (logOptions & LOG_OPTION_MANUALSTACK) {
            sLogX86ManualStackTrace(logLineProc, logLineProcParam, registerEip, registerEbp, stackFramesToSkip);
        }

        if (logOptions & LOG_OPTION_DBGHELPSTACK) {
            sLogDbgHelpStackTrace(logOptions, logLineProc, logLineProcParam, registerEip, registerEbp, registerEsp, stackFramesToSkip);
        }

        if (logOptions & LOG_OPTION_MODULES) {
            sLogLoadedModules(logLineProc, logLineProcParam);
        }

        if (logOptions & (LOG_OPTION_CODE | LOG_OPTION_STACK)) {
            sLogMemory(logOptions, logLineProc, logLineProcParam, reinterpret_cast<uint8_t*>(registerEip), reinterpret_cast<uint8_t*>(registerEsp));
        }

        sLogSeparatorLine(logLineProc, logLineProcParam, '-', 1);
        sShowOutOfMemory(logLineProc, logLineProcParam);
    }

    InterlockedDecrement(&sgRecursionLevel);
}

#endif

#endif
