#if defined(__linux__) || defined(__APPLE__)

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#if defined(__APPLE__)
#include <climits>
#include <mach-o/dyld.h>
#endif
#include <pthread.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include "Engine/Source/Storm/Source/SLog.h"
#include "Engine/Source/Storm/Source/SOption.h"
#include "Engine/Source/Storm/H/storm.h"

static int32_t s_logsysteminit;

static uintptr_t s_sequence;

static char s_defaultdir[MAX_PATH];

static pthread_mutex_t s_defaultdir_critsect;

static _LOG* s_loghead[4];

static pthread_mutex_t s_critsect[4];

static int LogFd(void* file) {
    return static_cast<int>(reinterpret_cast<intptr_t>(file));
}

static void FlushLog(_LOG* logptr) {
    if (logptr->bufferused) {
        ssize_t written = write(LogFd(logptr->file), logptr->buffer, logptr->bufferused);
        (void)written;
        fsync(LogFd(logptr->file));
        logptr->bufferused = 0;
        logptr->pendpoint = 0;
    }
}

static _LOG* LockLog(HSLOG log, HLOCKEDLOG* lockedhandle, int32_t createifnecessary) {
    if (!log) {
        *lockedhandle = reinterpret_cast<HLOCKEDLOG>(-1);
        return nullptr;
    }

    uintptr_t index = reinterpret_cast<uintptr_t>(log) & 3;
    pthread_mutex_lock(&s_critsect[index]);
    *lockedhandle = reinterpret_cast<HLOCKEDLOG>(index);

    _LOG** prevptr = &s_loghead[index];

    for (_LOG* logptr = s_loghead[index]; logptr; logptr = logptr->next) {
        if (logptr->log == log) {
            return logptr;
        }

        prevptr = &logptr->next;
    }

    if (!createifnecessary) {
        pthread_mutex_unlock(&s_critsect[index]);
        *lockedhandle = reinterpret_cast<HLOCKEDLOG>(-1);
        return nullptr;
    }

    _LOG* logptr = static_cast<_LOG*>(calloc(1, sizeof(_LOG)));
    *prevptr = logptr;

    if (!logptr) {
        pthread_mutex_unlock(&s_critsect[index]);
        *lockedhandle = reinterpret_cast<HLOCKEDLOG>(-1);
        return nullptr;
    }

    logptr->log = log;
    logptr->next = nullptr;
    logptr->filename[0] = 0;
    logptr->file = INVALID_HANDLE_VALUE;
    logptr->bufferused = 0;
    logptr->pendpoint = 0;

    return logptr;
}

static void OutputIndent(_LOG* logptr) {
    int32_t indent = logptr->indent;

    if (indent > 0) {
        if (indent >= 128) {
            indent = 128;
        }

        memset(&logptr->buffer[logptr->bufferused], ' ', indent);
        logptr->buffer[logptr->bufferused + indent] = 0;
        logptr->bufferused += indent;
    }
}

static void OutputReturn(_LOG* logptr) {
    strcpy(&logptr->buffer[logptr->bufferused], "\n");
    logptr->bufferused += 1;
}

static void OutputTime(_LOG* logptr, int32_t show) {
    static uint32_t lasttime;
    static char timestr[64];
    static uint32_t timestrlen;

    if (!logptr->timeStamp) {
        return;
    }

    timeval now;
    gettimeofday(&now, nullptr);
    uint32_t time = static_cast<uint32_t>(now.tv_sec * 1000 + now.tv_usec / 1000);

    if (time != lasttime) {
        lasttime = time;

        tm local;
        localtime_r(&now.tv_sec, &local);
        snprintf(timestr, sizeof(timestr), "%u/%u %02u:%02u:%02u.%03u  ", local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec, static_cast<uint32_t>(now.tv_usec / 1000));
        timestrlen = SStrLen(timestr);
    }

    if (show) {
        memcpy(&logptr->buffer[logptr->bufferused], timestr, timestrlen + 1);
    } else {
        memset(&logptr->buffer[logptr->bufferused], ' ', timestrlen);
        logptr->buffer[logptr->bufferused + timestrlen] = 0;
    }

    logptr->bufferused += timestrlen;
}

static void UnlockDeleteLog(_LOG* logptr, HLOCKEDLOG lockedhandle) {
    uintptr_t index = reinterpret_cast<uintptr_t>(lockedhandle);
    _LOG** prevptr = &s_loghead[index];

    for (_LOG* curr = s_loghead[index]; curr; curr = curr->next) {
        if (curr == logptr) {
            *prevptr = curr->next;
            free(curr);
            break;
        }

        prevptr = &curr->next;
    }

    pthread_mutex_unlock(&s_critsect[index]);
}

static void UnlockLog(HLOCKEDLOG lockedhandle) {
    pthread_mutex_unlock(&s_critsect[reinterpret_cast<uintptr_t>(lockedhandle)]);
}

static const char* PrependDefaultDir(char* newfilename, uint32_t newfilenamesize, const char* filename) {
    if (!filename || !*filename || SStrChr(filename, '/')) {
        return filename;
    }

    pthread_mutex_lock(&s_defaultdir_critsect);

    if (s_defaultdir[0]) {
        size_t chars = SStrCopy(newfilename, s_defaultdir, newfilenamesize);
        SStrCopy(&newfilename[chars], filename, newfilenamesize - chars);
    } else {
#if defined(__APPLE__)
        char executable[PATH_MAX];
        char resolved[PATH_MAX];
        uint32_t length = sizeof(executable);

        if (_NSGetExecutablePath(executable, &length)) {
            pthread_mutex_unlock(&s_defaultdir_critsect);
            return filename;
        }

        const char* path = realpath(executable, resolved);
        SStrCopy(newfilename, path ? path : executable, newfilenamesize);
#else
        ssize_t length = readlink("/proc/self/exe", newfilename, newfilenamesize - 1);
        newfilename[length > 0 ? length : 0] = 0;
#endif

        char* slash = SStrChrR(newfilename, '/');

        if (slash) {
            *slash = 0;
        }

        SStrPack(newfilename, "/", newfilenamesize);
        SStrPack(newfilename, filename, newfilenamesize);
    }

    pthread_mutex_unlock(&s_defaultdir_critsect);

    return newfilename;
}

void STORMAPI SLogClose(HSLOG log) {
    if (!s_logsysteminit) {
        return;
    }

    HLOCKEDLOG lockedhandle;
    _LOG* logptr = LockLog(log, &lockedhandle, 0);

    if (!logptr) {
        return;
    }

    if (logptr->file != INVALID_HANDLE_VALUE) {
        FlushLog(logptr);
        close(LogFd(logptr->file));
    }

    UnlockDeleteLog(logptr, lockedhandle);
}

void STORMAPI SLogFlush(HSLOG log) {
    HLOCKEDLOG lockedhandle;
    _LOG* logptr = LockLog(log, &lockedhandle, 0);

    if (logptr) {
        if (logptr->file != INVALID_HANDLE_VALUE) {
            FlushLog(logptr);
        }

        UnlockLog(lockedhandle);
    }
}

void STORMAPI SLogFlushAll() {
    for (int32_t i = 0; i < 4; i++) {
        pthread_mutex_lock(&s_critsect[i]);

        for (_LOG* logptr = s_loghead[i]; logptr; logptr = logptr->next) {
            if (logptr->file != INVALID_HANDLE_VALUE) {
                FlushLog(logptr);
            }
        }

        pthread_mutex_unlock(&s_critsect[i]);
    }
}

void STORMAPI SLogInitialize() {
    if (s_logsysteminit) {
        return;
    }

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);

    for (int32_t i = 0; i < 4; i++) {
        pthread_mutex_init(&s_critsect[i], &attr);
    }

    pthread_mutex_init(&s_defaultdir_critsect, &attr);
    pthread_mutexattr_destroy(&attr);
    s_logsysteminit = 1;
}

static int32_t CreateFileDirectory(const char* path) {
    if (!path) {
        SErrSetLastError(STORM_ERROR_INVALID_PARAMETER);
        return 0;
    }

    char buffer[MAX_PATH];
    SStrCopy(buffer, path, sizeof(buffer));

    char* last = SStrChrR(buffer, '/');

    if (!last) {
        return 0;
    }

    *last = 0;

    for (char* curr = SStrChr(buffer + 1, '/'); curr; curr = SStrChr(curr + 1, '/')) {
        *curr = 0;
        mkdir(buffer, 0755);
        *curr = '/';
    }

    return mkdir(buffer, 0755) == 0;
}

static int32_t OpenLogFile(const char* filename, void** file, uint32_t flags) {
    if (!filename || !*filename) {
        *file = INVALID_HANDLE_VALUE;
        return 0;
    }

    char newfilename[MAX_PATH];
    // ʕ •ᴥ•ʔ✿ Callers pass Windows-style 'Logs\x.log' names; normalise before touching the filesystem ✿ ʕ •ᴥ•ʔ
    char normalized[MAX_PATH];
    SStrCopy(normalized, filename, sizeof(normalized));
    for (char* c = normalized; *c; c++) if (*c == '\\') *c = '/';
    const char* path = PrependDefaultDir(newfilename, sizeof(newfilename), normalized);
    int32_t append = (flags & SLOG_FLAG_APPEND) != 0;
    CreateFileDirectory(path);

    int fd = open(path, O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC), 0644);
    *file = reinterpret_cast<void*>(static_cast<intptr_t>(fd));

    return fd >= 0;
}

int32_t STORMAPI SLogCreate(const char* filename, uint32_t flags, HSLOG* log) {
    if (!filename || !*filename || !log) {
        SErrSetLastError(STORM_ERROR_INVALID_PARAMETER);
        return 0;
    }

    *log = nullptr;

    if (flags & SLOG_FLAG_NOFILE) {
        filename = "";
        flags &= ~SLOG_FLAG_OPENFILE;
    }

    void* file = INVALID_HANDLE_VALUE;

    if ((flags & SLOG_FLAG_OPENFILE) && !OpenLogFile(filename, &file, flags)) {
        return 0;
    }

    *log = reinterpret_cast<HSLOG>(++s_sequence);

    HLOCKEDLOG lockedhandle;
    _LOG* logptr = LockLog(*log, &lockedhandle, 1);

    if (!logptr) {
        *log = nullptr;
        return 0;
    }

    logptr->file = file;
    SStrCopy(logptr->filename, filename, sizeof(logptr->filename));
    logptr->flags = flags;
    logptr->timeStamp = 1;
    logptr->indent = 0;
    UnlockLog(lockedhandle);

    return 1;
}

void STORMAPI SLogDestroy() {
    SLogFlushAll();

    for (int32_t i = 0; i < 4; i++) {
        pthread_mutex_lock(&s_critsect[i]);

        while (s_loghead[i]) {
            char filename[MAX_PATH];
            SStrCopy(filename, s_loghead[i]->filename, STORM_MAX_STR);
            SLogClose(s_loghead[i]->log);
            SErrReportNamedResourceLeak("HSLOG", filename);
        }

        pthread_mutex_unlock(&s_critsect[i]);
    }

    s_logsysteminit = 0;

    for (int32_t i = 0; i < 4; i++) {
        pthread_mutex_destroy(&s_critsect[i]);
    }

    pthread_mutex_destroy(&s_defaultdir_critsect);
}

static int32_t PrepareLog(_LOG* logptr) {
    if (logptr->file != INVALID_HANDLE_VALUE) {
        return 1;
    }

    if (OpenLogFile(logptr->filename, &logptr->file, logptr->flags)) {
        return 1;
    }

    logptr->filename[0] = 0;
    return 0;
}

void STORMAPI SLogVWrite(HSLOG log, const char* format, va_list arglist) {
    HLOCKEDLOG lockedhandle;
    _LOG* logptr = LockLog(log, &lockedhandle, 0);

    if (!logptr) {
        return;
    }

    if (PrepareLog(logptr)) {
        OutputTime(logptr, 1);
        OutputIndent(logptr);
        SStrVPrintf(&logptr->buffer[logptr->bufferused], sizeof(logptr->buffer) - logptr->bufferused, format, arglist);
        logptr->bufferused += SStrLen(&logptr->buffer[logptr->bufferused]);
        OutputReturn(logptr);

        if (g_opt.echotooutputdebugstring) {
            OutputDebugStringA(&logptr->buffer[logptr->pendpoint]);
        }

        logptr->pendpoint = logptr->bufferused;

        if (logptr->bufferused >= 0xC000) {
            FlushLog(logptr);
        }
    }

    UnlockLog(lockedhandle);
}

void SLogWrite(HSLOG log, const char* format, ...) {
    va_list arglist;
    va_start(arglist, format);
    SLogVWrite(log, format, arglist);
    va_end(arglist);
}

#endif
