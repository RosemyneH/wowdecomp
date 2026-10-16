#include "fmod_settings.h"

#include "fmod_debug.h"
#include "fmod_thread.h"
#include "fmod_memory.h"
#include "fmod_os_misc.h"
#include "fmod_time.h"

namespace FMOD
{


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: 2dd6e88b manual
// OFFSET: 0x008E5190
THREAD_RETURNTYPE THREAD_CALLCONV Thread::callback(void *data)
{
    FMOD_UINT_NATIVE id;
    Thread *thread = (Thread *)data;
    
    FMOD_OS_Thread_GetCurrentID(&id);

    gGlobal->gSystemPool->getCurrentThreadID(id);

    thread->mRunning = true;

    FLOG((FMOD_DEBUG_LEVEL_LOG, __FILE__, __LINE__, "Thread::callback", "* %s started\n", thread->mName));

    while (thread->mRunning)
    {
        if (thread->mSema)
        {
            FMOD_OS_Semaphore_Wait(thread->mSema);
        }
        
        if (thread->mRunning)
        {
            if (thread->mUserCallback)
            {
                thread->mUserCallback(thread->mUserData);
            }
            else
            {
                thread->threadFunc();
            }

            if (thread->mPeriod)
            {
                FMOD_OS_Time_Sleep(thread->mPeriod);
            }
        }
    }

    gGlobal->gSystemPool->clearThreadID(id);

    FLOG((FMOD_DEBUG_LEVEL_LOG, __FILE__, __LINE__, "Thread::callback", "* %s finished\n", thread->mName));

    FMOD_OS_Semaphore_Signal(thread->mEndSema, false);
    
    data = 0;
    
    THREAD_RETURN
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]

	[SEE_ALSO]
]
*/
FMOD_RESULT Thread::threadFunc()
{
    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]

	[SEE_ALSO]
]
*/
// VERIFY: 2cca6d4d auto
// OFFSET: 0x008E5260
Thread::Thread()
{
    mHandle       = 0;
    mRunning      = false;
    mStack        = 0;
    mUserData     = 0;
    mSema         = 0;
    mEndSema      = 0;
    mPeriod       = 0;
    mUserCallback = 0;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]

	[SEE_ALSO]
]
*/
// VERIFY: ea925780 auto
// OFFSET: 0x008E52A0
FMOD_RESULT Thread::initThread(const char *name, void (*func)(void *userdata), void *userdata, PRIORITY priority, void *stack, int stacksize, bool usesemaphore, int sleepperiod)
{
    FMOD_RESULT result;
    FMOD_THREAD_PRIORITY pri;

    FLOG((FMOD_DEBUG_LEVEL_LOG, __FILE__, __LINE__, "Thread::initThread", "Initializing %s.  priority %d\n", name ? name : "(null)", priority));

    mUserCallback = func;
    mUserData     = userdata;
    mPeriod       = sleepperiod;
    mRunning      = false;

    if (usesemaphore)
    {
        result = FMOD_OS_Semaphore_Create(&mSema);
        if (result != FMOD_OK)
        {
            return result;
        }
    }

    switch(priority)
    {
        case PRIORITY_VERYLOW:
        {
            pri = FMOD_THREAD_PRIORITY_VERYLOW;
            break;
        }
        case PRIORITY_LOW:
        {
            pri = FMOD_THREAD_PRIORITY_LOW;
            break;
        }
        case PRIORITY_NORMAL:
        {
            pri = FMOD_THREAD_PRIORITY_NORMAL;
            break;
        }
        case PRIORITY_HIGH:
        {
            pri = FMOD_THREAD_PRIORITY_HIGH;
            break;
        }
        case PRIORITY_VERYHIGH:
        {
            pri = FMOD_THREAD_PRIORITY_VERYHIGH;
            break;
        }
        case PRIORITY_CRITICAL:
        {
            pri = FMOD_THREAD_PRIORITY_CRITICAL;
            break;
        }
        default:
        {
            return FMOD_ERR_INVALID_PARAM;
        }
    }

    if (name)
    {
        FMOD_strncpy(mName, name, FMOD_STRING_MAXNAMELEN);
    }
    else
    {
        FMOD_strcpy(mName, "?????");
    }

    result = FMOD_OS_Thread_Create(name, callback, this, pri, stack, stacksize, &mHandle);
    if (result != FMOD_OK)
    {
        return result;
    }

    /*
        Wait for thread to actually start before continuing.
    */
    while (!mRunning)
    {
        FMOD_OS_Time_Sleep(1);
    }

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]

	[SEE_ALSO]
]
*/
// VERIFY: 80e9ff69 manual
// OFFSET: 0x008E53C0
FMOD_RESULT Thread::closeThread()
{
    if (mRunning)
    {
        FMOD_RESULT result;

        result = FMOD_OS_Semaphore_Create(&mEndSema);
        if (result != FMOD_OK)
        {
            return result;
        }

        mRunning = false;

        if (mSema)
        {
            result = FMOD_OS_Semaphore_Signal(mSema, false);  /* make thread execute */
            if (result != FMOD_OK)
            {
                mRunning = true;
                FMOD_OS_Semaphore_Free(mEndSema);
                mEndSema = 0;
                return result;
            }
        }

        result = FMOD_OS_Semaphore_Wait(mEndSema);        /* wait for thread to exit */
        if (result != FMOD_OK)
        {
            return result;
        }

        if (mSema)
        {
            result = FMOD_OS_Semaphore_Free(mSema);
            if (result != FMOD_OK)
            {
                return result;
            }
            mSema = 0;
        }

        result = FMOD_OS_Semaphore_Free(mEndSema);
        if (result != FMOD_OK)
        {
            return result;
        }
        mEndSema = 0;

        result = FMOD_OS_Thread_Destroy(mHandle);
        if (result != FMOD_OK)
        {
            return result;
        }
        mHandle = 0;

        if (mStack)
        {
            FMOD_Memory_Free(mStack);
            mStack = 0;
        }
    }

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]

	[SEE_ALSO]
]
*/
FMOD_RESULT Thread::getCurrentThreadID(FMOD_UINT_NATIVE *id)
{
    FMOD_OS_Thread_GetCurrentID(id);

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]

	[SEE_ALSO]
]
*/
// VERIFY: a9efda3d auto
// OFFSET: 0x008E54A0
FMOD_RESULT Thread::wakeupThread(bool frominterrupt)
{
    if (mSema)
    {
        return FMOD_OS_Semaphore_Signal(mSema, frominterrupt);
    }

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]

	[SEE_ALSO]
]
*/


}
