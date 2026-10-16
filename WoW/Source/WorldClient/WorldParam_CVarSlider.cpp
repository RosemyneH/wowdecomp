#include "WorldParam_CVarSlider.h"
#include "WorldParam.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Storm/H/storm.h"
#include "WoW/Source/WorldClient/World.h"

bool CWorldParamSlider::ExtendedViewEnabled() {
    return CWorldParam::cvar_extendedView && CWorldParam::cvar_extendedView->m_intValue >= 1;
}

bool CWorldParamSlider::ExtendedHorizonZClipEnabled() {
    return CWorldParam::cvar_extendedHorizonZClip && CWorldParam::cvar_extendedHorizonZClip->m_intValue >= 1;
}

void CWorldParamSlider::ApplyDistanceFogFromCVar() {
    if (!g_theGxDevicePtr) {
        return;
    }

    const int32_t enabled = CWorldParam::cvar_distanceFog ? CWorldParam::cvar_distanceFog->m_intValue : 1;
    g_theGxDevicePtr->MasterEnableSet(GxMasterEnable_Fog, enabled ? 1 : 0);
}

bool CWorldParamSlider::GetSliderMin(const char* name, float* out) {
    if (!SStrCmpI(name, "farclip", STORM_MAX_STR)) {
        *out = kFarclipMin;
        return true;
    }

    if (!SStrCmpI(name, "horizonFarclipScale", STORM_MAX_STR)) {
        *out = kHorizonFarclipScaleMin;
        return true;
    }

    if (!SStrCmpI(name, "groundEffectDist", STORM_MAX_STR)) {
        *out = 0.0f;
        return true;
    }

    return false;
}

bool CWorldParamSlider::GetSliderMax(const char* name, float* out) {
    if (!SStrCmpI(name, "farclip", STORM_MAX_STR)) {
        *out = ExtendedViewEnabled() ? kFarclipMaxExtended : kFarclipMaxRetail;
        return true;
    }

    if (!SStrCmpI(name, "horizonFarclipScale", STORM_MAX_STR)) {
        *out = ExtendedHorizonZClipEnabled() ? kHorizonFarclipScaleMaxExtended : kHorizonFarclipScaleMaxRetail;
        return true;
    }

    if (!SStrCmpI(name, "groundEffectDist", STORM_MAX_STR)) {
        *out = ExtendedViewEnabled() ? kGroundEffectDistMaxExtended : kGroundEffectDistMaxRetail;
        return true;
    }

    return false;
}
