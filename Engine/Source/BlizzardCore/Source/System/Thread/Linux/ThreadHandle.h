#ifndef ENGINE_SOURCE_BLIZZARDCORE_SOURCE_SYSTEM_THREAD_LINUX_THREADHANDLE_H
#define ENGINE_SOURCE_BLIZZARDCORE_SOURCE_SYSTEM_THREAD_LINUX_THREADHANDLE_H

#include <pthread.h>

namespace System_Thread {

struct ThreadHandle {
    pthread_t m_thread;
    pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;
    bool m_exited;
};

ThreadHandle* ThreadHandleCreate();
void ThreadHandleDestroy(ThreadHandle* handle);
void ThreadHandleSignalExit(ThreadHandle* handle);
void ThreadHandleWait(ThreadHandle* handle);

}

#endif
