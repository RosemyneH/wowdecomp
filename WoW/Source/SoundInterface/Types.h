#ifndef WOW_SOURCE_SOUNDINTERFACE_TYPES_H
#define WOW_SOURCE_SOUNDINTERFACE_TYPES_H

#include <cstddef>
#include <cstdint>

enum ITEMSOUNDTYPE : int32_t {
    ITEMSOUND_PICKUP = 0,
    ITEMSOUND_DROP = 1,
    ITEMSOUND_USE = 2,
    ITEMSOUND_UNKNOWN_3 = 3, // Historical close operation is unconfirmed in 3.3.5.
    NUM_ITEMSOUNDS = 4,

    // Unused 0.5.3 name for slot 3; its 3.3.5 meaning is unverified.
    // ITEMSOUND_CLOSE = 3,
};

enum SI2_DSP_TYPE {
    SI2_DSP_TYPE_NONE = 0,
    SI2_DSP_TYPE_PITCHSHIFT = 1,
    SI2_DSP_TYPE_HIGHPASS = 2,
    SI2_DSP_TYPE_LOWPASS = 3,
    SI2_DSP_TYPE_CHORUS = 4,
    SI2_DSP_TYPE_ECHO = 5,
    SI2_DSP_TYPE_VOLUME = 6,
};

#endif
