#include "fmod_settings.h"

#ifdef FMOD_SUPPORT_ALSA

#include "fmod_memory.h"
#include "fmod_output_alsa.h"
#include "fmod_soundi.h"
#include "fmod_string.h"
#include "fmod_systemi.h"

namespace FMOD
{

FMOD_OUTPUT_DESCRIPTION_EX alsaoutput;

FMOD_OUTPUT_DESCRIPTION_EX *OutputALSA::getDescriptionEx()
{
    FMOD_memset(&alsaoutput, 0, sizeof(FMOD_OUTPUT_DESCRIPTION_EX));

    alsaoutput.name          = "FMOD ALSA Output";
    alsaoutput.version       = 0x00010100;
    alsaoutput.polling       = true;
    alsaoutput.getnumdrivers = &OutputALSA::getNumDriversCallback;
    alsaoutput.getdrivername = &OutputALSA::getDriverNameCallback;
    alsaoutput.getdrivercaps = &OutputALSA::getDriverCapsCallback;
    alsaoutput.init          = &OutputALSA::initCallback;
    alsaoutput.close         = &OutputALSA::closeCallback;
    alsaoutput.getposition   = &OutputALSA::getPositionCallback;
    alsaoutput.lock          = &OutputALSA::lockCallback;

    alsaoutput.mType         = FMOD_OUTPUTTYPE_ALSA;
    alsaoutput.mSize         = sizeof(OutputALSA);

    return &alsaoutput;
}

void *OutputALSA::writerThread(void *param)
{
    OutputALSA  *alsa       = (OutputALSA *)param;
    unsigned int framebytes = alsa->mBlockLengthBytes / alsa->mBlockLength;
    int          block      = 0;

    while (!alsa->mWriterQuit)
    {
        char             *data      = alsa->mBuffer + block * alsa->mBlockLengthBytes;
        snd_pcm_uframes_t remaining = alsa->mBlockLength;

        while (remaining && !alsa->mWriterQuit)
        {
            snd_pcm_sframes_t written = snd_pcm_writei(alsa->mHandle, data, remaining);
            if (written < 0)
            {
                if (snd_pcm_recover(alsa->mHandle, (int)written, 1) < 0)
                {
                    return 0;
                }
                continue;
            }

            data      += written * framebytes;
            remaining -= written;
        }

        block = (block + 1) % alsa->mNumBlocks;
        __atomic_add_fetch(&alsa->mPosition, alsa->mBlockLength, __ATOMIC_RELEASE);
    }

    return 0;
}

FMOD_RESULT OutputALSA::getNumDrivers(int *numdrivers)
{
    *numdrivers = 1;

    return FMOD_OK;
}

FMOD_RESULT OutputALSA::getDriverName(int driver, char *name, int namelen)
{
    if (!name)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    FMOD_strncpy(name, "ALSA Default", namelen);

    return FMOD_OK;
}

FMOD_RESULT OutputALSA::getDriverCaps(int id, FMOD_CAPS *caps)
{
    *caps = (FMOD_CAPS)(*caps | FMOD_CAPS_OUTPUT_MULTICHANNEL);
    *caps = (FMOD_CAPS)(*caps | FMOD_CAPS_OUTPUT_FORMAT_PCM16);

    return FMOD_OK;
}

FMOD_RESULT OutputALSA::init(int selecteddriver, FMOD_INITFLAGS flags, int *outputrate, int outputchannels, FMOD_SOUND_FORMAT *outputformat, int dspbufferlength, int dspnumbuffers, void *extradriverdata)
{
    Plugin::init();

    if (*outputrate <= 0 || outputchannels <= 0)
    {
        return FMOD_ERR_OUTPUT_FORMAT;
    }

    const char *device = extradriverdata ? *(const char **)extradriverdata : "default";

    if (snd_pcm_open(&mHandle, device, SND_PCM_STREAM_PLAYBACK, 0) < 0)
    {
        mHandle = 0;
        return FMOD_ERR_OUTPUT_INIT;
    }

    unsigned int latency = (unsigned int)((unsigned long long)dspbufferlength * dspnumbuffers * 1000000 / *outputrate);

    if (snd_pcm_set_params(mHandle, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, outputchannels, *outputrate, 1, latency) < 0)
    {
        snd_pcm_close(mHandle);
        mHandle = 0;
        return FMOD_ERR_OUTPUT_FORMAT;
    }

    *outputformat = FMOD_SOUND_FORMAT_PCM16;

    mBlockLength = dspbufferlength;
    mNumBlocks   = dspnumbuffers;
    mPosition    = 0;
    mWriterQuit  = false;

    SoundI::getBytesFromSamples(mBlockLength, &mBlockLengthBytes, outputchannels, *outputformat);

    mBuffer = (char *)FMOD_Memory_Calloc(mBlockLengthBytes * mNumBlocks);
    if (!mBuffer)
    {
        snd_pcm_close(mHandle);
        mHandle = 0;
        return FMOD_ERR_MEMORY;
    }

    if (pthread_create(&mWriterThread, NULL, &OutputALSA::writerThread, this))
    {
        FMOD_Memory_Free(mBuffer);
        mBuffer = 0;
        snd_pcm_close(mHandle);
        mHandle = 0;
        return FMOD_ERR_OUTPUT_INIT;
    }

    return FMOD_OK;
}

FMOD_RESULT OutputALSA::close()
{
    if (mHandle)
    {
        mWriterQuit = true;
        pthread_join(mWriterThread, NULL);

        snd_pcm_drop(mHandle);
        snd_pcm_close(mHandle);
        mHandle = 0;
    }

    if (mBuffer)
    {
        FMOD_Memory_Free(mBuffer);
        mBuffer = 0;
    }

    return FMOD_OK;
}

FMOD_RESULT OutputALSA::getPosition(unsigned int *pcm)
{
    *pcm = __atomic_load_n(&mPosition, __ATOMIC_ACQUIRE);

    return FMOD_OK;
}

FMOD_RESULT OutputALSA::lock(unsigned int offset, unsigned int length, void **ptr1, void **ptr2, unsigned int *len1, unsigned int *len2)
{
    unsigned int bufferbytes = mBlockLengthBytes * mNumBlocks;

    offset %= bufferbytes;

    if (offset + length > bufferbytes)
    {
        *ptr1 = mBuffer + offset;
        *ptr2 = mBuffer;
        *len1 = bufferbytes - offset;
        *len2 = length - (bufferbytes - offset);
    }
    else
    {
        *ptr1 = mBuffer + offset;
        *ptr2 = NULL;
        *len1 = length;
        *len2 = 0;
    }

    return FMOD_OK;
}

FMOD_RESULT F_CALLBACK OutputALSA::getNumDriversCallback(FMOD_OUTPUT_STATE *output, int *numdrivers)
{
    return ((OutputALSA *)output)->getNumDrivers(numdrivers);
}

FMOD_RESULT F_CALLBACK OutputALSA::getDriverNameCallback(FMOD_OUTPUT_STATE *output, int id, char *name, int namelen)
{
    return ((OutputALSA *)output)->getDriverName(id, name, namelen);
}

FMOD_RESULT F_CALLBACK OutputALSA::getDriverCapsCallback(FMOD_OUTPUT_STATE *output, int id, FMOD_CAPS *caps)
{
    return ((OutputALSA *)output)->getDriverCaps(id, caps);
}

FMOD_RESULT F_CALLBACK OutputALSA::initCallback(FMOD_OUTPUT_STATE *output, int selecteddriver, FMOD_INITFLAGS flags, int *outputrate, int outputchannels, FMOD_SOUND_FORMAT *outputformat, int dspbufferlength, int dspnumbuffers, void *extradriverdata)
{
    return ((OutputALSA *)output)->init(selecteddriver, flags, outputrate, outputchannels, outputformat, dspbufferlength, dspnumbuffers, extradriverdata);
}

FMOD_RESULT F_CALLBACK OutputALSA::closeCallback(FMOD_OUTPUT_STATE *output)
{
    return ((OutputALSA *)output)->close();
}

FMOD_RESULT F_CALLBACK OutputALSA::getPositionCallback(FMOD_OUTPUT_STATE *output, unsigned int *pcm)
{
    return ((OutputALSA *)output)->getPosition(pcm);
}

FMOD_RESULT F_CALLBACK OutputALSA::lockCallback(FMOD_OUTPUT_STATE *output, unsigned int offset, unsigned int length, void **ptr1, void **ptr2, unsigned int *len1, unsigned int *len2)
{
    return ((OutputALSA *)output)->lock(offset, length, ptr1, ptr2, len1, len2);
}

}

#endif
