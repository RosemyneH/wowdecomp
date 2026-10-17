#if defined(WOWDECOMP_UNIX)

#include <fenv.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#if !defined(__ANDROID__)
#include <ucontext.h>
#endif
#if defined(__ANDROID__)
#include <curl/curl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#include <SDL3/SDL.h>
#include "Engine/Source/Os/Path.h"
#endif
#include "Engine/Source/Base/Job/JobSystem.h"
#include "Engine/Source/Storm/H/storm.h"
#include "WoW/Source/Client.h"

static int32_t s_fiberResult = -1;

#if !defined(__ANDROID__)
static ucontext_t s_mainContext;
static ucontext_t s_fiberContext;
#endif

static bool ReturnFalse() {
    return false;
}

static int32_t ClientMain() {
    feclearexcept(FE_ALL_EXCEPT);
    fedisableexcept(FE_ALL_EXCEPT);
    fesetround(FE_TONEAREST);
    ReturnFalse();

    Job::Init();
    int32_t result = CommonMain();
    Job::Shutdown();
    return result;
}

#if !defined(__ANDROID__)
static void MainFiberProc() {
    s_fiberResult = ClientMain();
}
#endif

#if defined(__ANDROID__)

static const char kStreamingMfil[] =
    "version=1\n"
    "isTrial=0\n"
    "source=%s\n"
    "Data;0;12340;0\n"
    "Data/enUS;0;12340;0\n"
    "Data/common.MPQ;2881154862;12340;0\n"
    "Data/common-2.MPQ;1810430636;12340;0\n"
    "Data/expansion.MPQ;1921219911;12340;0\n"
    "Data/lichking.MPQ;2553948549;12340;0\n"
    "Data/patch.MPQ;4004713057;12340;0\n"
    "Data/patch-2.MPQ;1401729059;12340;0\n"
    "Data/patch-3.MPQ;605089137;12340;0\n"
    "Data/enUS/base-enUS.MPQ;29176975;12340;0\n"
    "Data/enUS/locale-enUS.MPQ;204291376;12340;0\n"
    "Data/enUS/expansion-locale-enUS.MPQ;17389181;12340;0\n"
    "Data/enUS/expansion-speech-enUS.MPQ;241033297;12340;0\n"
    "Data/enUS/lichking-locale-enUS.MPQ;12354378;12340;0\n"
    "Data/enUS/lichking-speech-enUS.MPQ;354008839;12340;0\n"
    "Data/enUS/speech-enUS.MPQ;438430439;12340;0\n"
    "Data/enUS/backup-enUS.MPQ;167245856;12340;0\n"
    "Data/enUS/patch-enUS.MPQ;296616080;12340;0\n"
    "Data/enUS/patch-enUS-2.MPQ;225570171;12340;0\n"
    "Data/enUS/patch-enUS-3.MPQ;100373935;12340;0\n";

static void WriteFile(const char* path, const char* text) {
    FILE* file = fopen(path, "wb");
    if (!file) {
        return;
    }

    fputs(text, file);
    fclose(file);
}

#if defined(__ANDROID__)

static size_t AndroidCurlWrite(void* contents, size_t size, size_t count, void* user) {
    FILE* file = static_cast<FILE*>(user);
    return fwrite(contents, size, count, file);
}

static void AndroidPrefetchSignatureFile(const char* cdnHost) {
    struct stat info {};
    if (stat("signaturefile", &info) == 0 && info.st_size > 1024) {
        return;
    }

    char url[128];
    snprintf(url, sizeof(url), "http://%s:1119/signaturefile", cdnHost);

    FILE* out = fopen("signaturefile.part", "wb");
    if (!out) {
        return;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        fclose(out);
        return;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, AndroidCurlWrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);

    CURLcode result = curl_easy_perform(curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);
    fclose(out);

    if (result != CURLE_OK || code != 200) {
        unlink("signaturefile.part");
        return;
    }

    rename("signaturefile.part", "signaturefile");
}

#endif

static void AndroidPrepareGameRoot() {
    const char* root = SDL_GetAndroidExternalStoragePath();
    if (!root || !root[0]) {
        root = SDL_GetAndroidInternalStoragePath();
    }
    if (!root || !root[0]) {
        return;
    }

    mkdir(root, 0755);
    chdir(root);

    char executable[512];
    snprintf(executable, sizeof(executable), "%s/WowClient", root);
    AndroidSetExecutablePath(executable);

    mkdir("Data", 0755);
    mkdir("Data/enUS", 0755);
    mkdir("WTF", 0755);

    // ʕ •ᴥ•ʔ✿ Same LAN as the Docker CDN. A subnet scan was missing it and falling back to yourcdn.com. ✿ ʕ •ᴥ•ʔ
    const char* cdnHost = "192.168.1.129";

    char source[96];
    snprintf(source, sizeof(source), "http://%s:1119/", cdnHost);
    char mfil[2048];
    snprintf(mfil, sizeof(mfil), kStreamingMfil, source);
    WriteFile("WoW.mfil", mfil);

    char realm[160];
    snprintf(realm, sizeof(realm), "SET realmList \"%s\"\nSET locale \"enUS\"\n", cdnHost);
    WriteFile("WTF/Config.wtf", realm);
    snprintf(realm, sizeof(realm), "SET realmList \"%s\"\n", cdnHost);
    WriteFile("Data/enUS/realmlist.wtf", realm);
    WriteFile("streaming-credits.txt",
        "WoW.mfil streaming follows brian8544's 3.3.5a CDN client.\n"
        "https://github.com/brian8544/streamingclient-3.3.5\n"
        "Server module (GPL-2.0): https://github.com/brian8544/mod-streamingclient\n"
        "Copyright brian8544. Sources live in ThirdParty/streamingclient/ of the wowdecomp tree.\n");

    AndroidPrefetchSignatureFile(cdnHost);
}

static void* AndroidClientThread(void*) {
    StormRtlInitialize();
    s_fiberResult = ClientMain();
    return nullptr;
}

extern "C" __attribute__((visibility("default"))) int SDL_main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    AndroidPrepareGameRoot();

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 0x800000);

    pthread_t thread;
    int created = pthread_create(&thread, &attr, AndroidClientThread, nullptr);
    pthread_attr_destroy(&attr);
    if (created) {
        return -1;
    }

    pthread_join(thread, nullptr);
    return s_fiberResult;
}

#else

int main(int argc, char** argv) {
    StormRtlInitialize();

    constexpr size_t stackSize = 0x800000;
    void* stack = malloc(stackSize);

    if (!stack || getcontext(&s_fiberContext)) {
        return -1;
    }

    s_fiberContext.uc_stack.ss_sp = stack;
    s_fiberContext.uc_stack.ss_size = stackSize;
    s_fiberContext.uc_link = &s_mainContext;
    makecontext(&s_fiberContext, MainFiberProc, 0);

    if (swapcontext(&s_mainContext, &s_fiberContext)) {
        free(stack);
        return -1;
    }

    free(stack);

    return s_fiberResult;
}

#endif

#endif
