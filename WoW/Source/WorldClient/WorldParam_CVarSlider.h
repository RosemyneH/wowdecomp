#ifndef WOW_SOURCE_WORLDCLIENT_WORLDPARAM_CVARSLIDER_H
#define WOW_SOURCE_WORLDCLIENT_WORLDPARAM_CVARSLIDER_H

#include <cstdint>

class CWorldParamSlider {
    public:
    static constexpr float kFarclipMin = 177.0f;
    static constexpr float kFarclipMaxRetail = 1277.0f;
    static constexpr float kFarclipMaxExtended = 30000.0f;
    static constexpr float kHorizonFarclipScaleMin = 3.0f;
    static constexpr float kHorizonFarclipScaleMaxRetail = 6.0f;
    static constexpr float kHorizonFarclipScaleMaxExtended = 48.0f;
    static constexpr float kGroundEffectDistMaxRetail = 140.0f;
    static constexpr float kGroundEffectDistMaxExtended = 220.0f;

    static bool ExtendedViewEnabled();
    static bool ExtendedHorizonZClipEnabled();
    static bool GetSliderMin(const char* name, float* out);
    static bool GetSliderMax(const char* name, float* out);
    static void ApplyDistanceFogFromCVar();
};

#endif
