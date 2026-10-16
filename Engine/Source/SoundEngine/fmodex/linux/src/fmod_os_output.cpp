#include "fmod_settings.h"

#include "fmod.h"
#include "fmod_output.h"
#include "fmod_output_alsa.h"
#include "fmod_pluginfactory.h"

// OFFSET: 0x00915FC0
FMOD_RESULT FMOD_OS_Output_Register(FMOD::PluginFactory *pluginfactory)
{
    #ifdef FMOD_SUPPORT_ALSA
    pluginfactory->registerOutput(FMOD::OutputALSA::getDescriptionEx());
    #endif

    return FMOD_OK;
}

// OFFSET: 0x00916030
FMOD_RESULT FMOD_OS_Output_GetDefault(FMOD_OUTPUTTYPE *outputtype)
{
    if (!outputtype)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

#ifdef FMOD_SUPPORT_ALSA
    *outputtype = FMOD_OUTPUTTYPE_ALSA;
#else
    *outputtype = FMOD_OUTPUTTYPE_NOSOUND;
#endif

    return FMOD_OK;
}
