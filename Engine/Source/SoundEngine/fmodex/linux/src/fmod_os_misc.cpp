#include "fmod_settings.h"

#include "fmod.h"
#include "fmod_os_misc.h"
#include "fmod_memory.h"

#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

struct FMOD_OS_THREADSTART
{
    THREAD_RETURNTYPE (THREAD_CALLCONV *func)(void *param);
    void *param;
#if defined(__APPLE__)
    char name[16];
#endif
};

static pthread_mutex_t gMemoryCrit;

static void *FMOD_OS_Thread_Start(void *data)
{
    FMOD_OS_THREADSTART start = *(FMOD_OS_THREADSTART *)data;
    free(data);
#if defined(__APPLE__)
    if (start.name[0])
    {
        pthread_setname_np(start.name);
    }
#endif
    start.func(start.param);
    return 0;
}

static unsigned long long FMOD_OS_Time_GetMonotonicUs()
{
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (unsigned long long)now.tv_sec * 1000000 + now.tv_nsec / 1000;
}

// OFFSET: 0x008D1CB0
void *FMOD_OS_Memory_Alloc(int size, FMOD_MEMORY_TYPE type)
{
    return malloc(size);
}

// OFFSET: 0x008D1CC0
void *FMOD_OS_Memory_Realloc(void *ptr, int size, FMOD_MEMORY_TYPE type)
{
    return realloc(ptr, size);
}

// OFFSET: 0x004A9CE0
void FMOD_OS_Memory_Free(void *ptr, FMOD_MEMORY_TYPE type)
{
    free(ptr);
}

// OFFSET: 0x008D1CD0
FMOD_RESULT FMOD_OS_File_Open(const char *name, char *mode, int unicode, unsigned int *filesize, void **handle)
{
    if (unicode)
    {
        return FMOD_ERR_FILE_BAD;
    }

    FILE *file = fopen(name, "rb");
    if (!file)
    {
        if (errno != ENOENT && errno != ENOTDIR && errno != ENAMETOOLONG)
        {
            return FMOD_ERR_FILE_BAD;
        }

        return FMOD_ERR_FILE_NOTFOUND;
    }

    struct stat info;
    fstat(fileno(file), &info);

    *handle = file;
    *filesize = (unsigned int)info.st_size;

    return FMOD_OK;
}

// OFFSET: 0x008D1D50
FMOD_RESULT FMOD_OS_File_Close(void *handle)
{
    if (!handle)
    {
        return FMOD_ERR_FILE_BAD;
    }

    fclose((FILE *)handle);

    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_File_Cancel(void *handle)
{
    return FMOD_OK;
}

// OFFSET: 0x008D1D70
FMOD_RESULT FMOD_OS_File_Read(void *handle, void *buf, unsigned int count, unsigned int *read)
{
    size_t rd = fread(buf, 1, count, (FILE *)handle);

    if (rd != count && ferror((FILE *)handle))
    {
        return FMOD_ERR_FILE_BAD;
    }

    *read = (unsigned int)rd;

    if (rd != count)
    {
        return FMOD_ERR_FILE_EOF;
    }

    return FMOD_OK;
}

// OFFSET: 0x008D1DC0
FMOD_RESULT FMOD_OS_File_Seek(void *handle, unsigned int offset)
{
    if (fseek((FILE *)handle, offset, SEEK_SET))
    {
        return FMOD_ERR_FILE_BAD;
    }

    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_Debug_OutputStr(const char *s)
{
    fputs(s, stderr);

    return FMOD_OK;
}

// OFFSET: 0x008D1DF0
FMOD_RESULT FMOD_OS_Time_GetNs(unsigned int *ns)
{
    *ns = (unsigned int)FMOD_OS_Time_GetMonotonicUs();

    return FMOD_OK;
}

// OFFSET: 0x008D1E60
FMOD_RESULT FMOD_OS_Time_GetMs(unsigned int *ms)
{
    *ms = (unsigned int)(FMOD_OS_Time_GetMonotonicUs() / 1000);

    return FMOD_OK;
}

// OFFSET: 0x008D1E80
FMOD_RESULT FMOD_OS_Time_Sleep(unsigned int ms)
{
    usleep(ms * 1000);

    return FMOD_OK;
}

// OFFSET: 0x008D1EA0
FMOD_RESULT FMOD_OS_Thread_GetCurrentID(FMOD_UINT_NATIVE *id)
{
    *id = (FMOD_UINT_NATIVE)pthread_self();

    return FMOD_OK;
}

// OFFSET: 0x008D2110
FMOD_RESULT FMOD_OS_Thread_Create(const char *name, THREAD_RETURNTYPE (THREAD_CALLCONV *func)(void *param), void *param, FMOD_THREAD_PRIORITY priority, void *stack, int stacksize, void **handle)
{
    if (!handle)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    FMOD_OS_THREADSTART *start = (FMOD_OS_THREADSTART *)malloc(sizeof(FMOD_OS_THREADSTART));
    if (!start)
    {
        return FMOD_ERR_MEMORY;
    }

    start->func = func;
    start->param = param;

    char shortname[16];
    shortname[0] = '\0';
    if (name)
    {
        snprintf(shortname, sizeof(shortname), "%s", name);
    }
#if defined(__APPLE__)
    snprintf(start->name, sizeof(start->name), "%s", shortname);
#endif

    pthread_t thread;
    if (pthread_create(&thread, NULL, FMOD_OS_Thread_Start, start))
    {
        free(start);
        return FMOD_ERR_MEMORY;
    }

#if !defined(__APPLE__)
    if (name)
    {
        pthread_setname_np(thread, shortname);
    }
#endif

    *handle = (void *)thread;

    return FMOD_OK;
}

// OFFSET: 0x008D1EC0
FMOD_RESULT FMOD_OS_Thread_Destroy(void *handle)
{
    pthread_detach((pthread_t)handle);

    return FMOD_OK;
}

// OFFSET: 0x008D1EE0
FMOD_RESULT FMOD_OS_CriticalSection_Create(FMOD_OS_CRITICALSECTION **crit, bool memorycrit)
{
    if (!crit)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    if (memorycrit)
    {
        *crit = (FMOD_OS_CRITICALSECTION *)&gMemoryCrit;
    }
    else
    {
        *crit = (FMOD_OS_CRITICALSECTION *)FMOD_Memory_Alloc(sizeof(pthread_mutex_t));
        if (!*crit)
        {
            return FMOD_ERR_MEMORY;
        }
    }

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init((pthread_mutex_t *)*crit, &attr);
    pthread_mutexattr_destroy(&attr);

    return FMOD_OK;
}

// OFFSET: 0x008D1F40
FMOD_RESULT FMOD_OS_CriticalSection_Free(FMOD_OS_CRITICALSECTION *crit)
{
    if (!crit)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    pthread_mutex_destroy((pthread_mutex_t *)crit);

    if (crit != (FMOD_OS_CRITICALSECTION *)&gMemoryCrit)
    {
        FMOD_Memory_Free(crit);
    }

    return FMOD_OK;
}

// OFFSET: 0x008D1F80
FMOD_RESULT FMOD_OS_CriticalSection_Enter(FMOD_OS_CRITICALSECTION *crit)
{
    if (!crit)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    pthread_mutex_lock((pthread_mutex_t *)crit);

    return FMOD_OK;
}

// OFFSET: 0x008D1FA0
FMOD_RESULT FMOD_OS_CriticalSection_Leave(FMOD_OS_CRITICALSECTION *crit)
{
    if (!crit)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    pthread_mutex_unlock((pthread_mutex_t *)crit);

    return FMOD_OK;
}

// OFFSET: 0x008D1FC0
FMOD_RESULT FMOD_OS_Semaphore_Create(FMOD_OS_SEMAPHORE **sema)
{
    if (!sema)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    sem_t *semaphore = (sem_t *)malloc(sizeof(sem_t));
    if (!semaphore)
    {
        return FMOD_ERR_MEMORY;
    }

    sem_init(semaphore, 0, 0);
    *sema = (FMOD_OS_SEMAPHORE *)semaphore;

    return FMOD_OK;
}

// OFFSET: 0x008D1FF0
FMOD_RESULT FMOD_OS_Semaphore_Free(FMOD_OS_SEMAPHORE *sema)
{
    if (!sema)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    sem_destroy((sem_t *)sema);
    free(sema);

    return FMOD_OK;
}

// OFFSET: 0x008D2010
FMOD_RESULT FMOD_OS_Semaphore_Wait(FMOD_OS_SEMAPHORE *sema)
{
    if (!sema)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    while (sem_wait((sem_t *)sema) && errno == EINTR)
    {
    }

    return FMOD_OK;
}

// OFFSET: 0x008D2030
FMOD_RESULT FMOD_OS_Semaphore_Signal(FMOD_OS_SEMAPHORE *sema, bool interrupt)
{
    if (!sema)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    sem_post((sem_t *)sema);

    return FMOD_OK;
}

// OFFSET: 0x008D2060
FMOD_RESULT FMOD_OS_Library_Load(const char *dllname, FMOD_OS_LIBRARY **handle)
{
    if (!dllname || !handle)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    *handle = (FMOD_OS_LIBRARY *)dlopen(dllname, RTLD_NOW | RTLD_LOCAL);
    if (!*handle)
    {
        return FMOD_ERR_FILE_NOTFOUND;
    }

    return FMOD_OK;
}

// OFFSET: 0x008D20A0
FMOD_RESULT FMOD_OS_Library_GetProcAddress(FMOD_OS_LIBRARY *handle, const char *procname, void **address)
{
    if (!handle || !address)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    *address = dlsym(handle, procname);
    if (!*address)
    {
        return FMOD_ERR_FORMAT;
    }

    return FMOD_OK;
}

// OFFSET: 0x008D20E0
FMOD_RESULT FMOD_OS_Library_Free(FMOD_OS_LIBRARY *handle)
{
    if (!handle)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    if (dlclose(handle))
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_CheckDriverList(bool *devicelistchanged)
{
    *devicelistchanged = false;

    return FMOD_OK;
}
