#include "Engine/Source/Gx/Atmosphere.h"
#include <cmath>
#include <ctime>
#include "Engine/Source/ConsoleBase/ConsoleVar.h"

enum {
    A_Master,
    A_Volume,
    A_VolStrength,
    A_VolDensity,
    A_VolDist,
    A_VolToward,
    A_VolQuality,
    A_Fog,
    A_FogDensity,
    A_FogHeight,
    A_FogBright,
    A_FogPatches,
    A_FogWater,
    A_Shadows,
    A_ShWorld,
    A_ShUnits,
    A_ShStrength,
    A_ShSoft,
    A_Sunlight,
    A_Rays,
    A_RayStrength,
    A_Night,
    A_NightDark,
    A_NightStrength,
    A_NightIndoors,
    A_Clouds,
    A_CloudCover,
    A_CloudSpeed,
    A_CloudShapes,
    A_Color,
    A_DaySat,
    A_NightSat,
    A_Glow,
    A_Lamps,
    A_LampGlow,
    A_LampCount,
    A_LampDay,
    A_Beacon,
    A_Beam,
    A_Grass,
    A_GrassWind,
    A_GrassPart,
    A_Water,
    A_WaterFoam,
    A_WaterGlint,
    A_WaterWaves,
    A_WaterClarity,
    A_Time,
    A_TimeMode,
    A_TimeHour,
    A_Count
};

struct AtmosVar {
    const char* name;
    const char* help;
    const char* def;
};

static const AtmosVar kVars[A_Count] = {
    { "atmos", "Atmosphere effects", "0" },
    { "atmosVolume", "Volumetric light", "1" },
    { "atmosVolumeStrength", "Volumetric light strength", "20" },
    { "atmosVolumeDensity", "Volumetric light density", "12" },
    { "atmosVolumeDistance", "Volumetric light distance", "190" },
    { "atmosVolumeToward", "Volumetric light toward the sun", "7" },
    { "atmosVolumeQuality", "Volumetric light quality", "2" },
    { "atmosFog", "Volumetric fog", "1" },
    { "atmosFogDensity", "Fog density", "15" },
    { "atmosFogHeight", "Fog height", "25" },
    { "atmosFogBrightness", "Fog brightness", "45" },
    { "atmosFogPatches", "Fog patches", "50" },
    { "atmosFogWater", "Fog over low ground and water", "210" },
    { "atmosShadows", "Sun shadows", "1" },
    { "atmosShadowWorld", "World sun shadows", "1" },
    { "atmosShadowUnits", "Character sun shadows", "1" },
    { "atmosShadowStrength", "Sun shadow strength", "20" },
    { "atmosShadowSoft", "Sun shadow softness", "1" },
    { "atmosSunlight", "Sunlight on open ground", "35" },
    { "atmosRays", "Sun rays", "1" },
    { "atmosRaysStrength", "Sun ray strength", "40" },
    { "atmosNight", "Night darkness", "1" },
    { "atmosNightDarkness", "Night darkness amount", "55" },
    { "atmosNightStrength", "Light and rays at night", "25" },
    { "atmosNightIndoors", "Night darkness indoors", "0" },
    { "atmosClouds", "Cloud layer", "1" },
    { "atmosCloudCover", "Cloud cover", "45" },
    { "atmosCloudSpeed", "Cloud speed", "15" },
    { "atmosCloudShapes", "Shaped clouds", "1" },
    { "atmosColor", "Color grade", "1" },
    { "atmosDaySat", "Day saturation", "100" },
    { "atmosNightSat", "Night saturation", "100" },
    { "atmosGlow", "Game glow", "100" },
    { "atmosLamps", "Lamps", "1" },
    { "atmosLampGlow", "Lamp glow", "26" },
    { "atmosLampCount", "Lamp count", "32" },
    { "atmosLampsDay", "Lamps by day", "51" },
    { "atmosBeacon", "Lighthouses", "1" },
    { "atmosBeaconBeam", "Lighthouse beam", "12" },
    { "atmosGrass", "Moving grass", "1" },
    { "atmosGrassWind", "Grass wind", "2" },
    { "atmosGrassPart", "Grass parting", "1" },
    { "atmosWater", "Water", "1" },
    { "atmosWaterFoam", "Water foam", "25" },
    { "atmosWaterGlint", "Sun glint on water", "100" },
    { "atmosWaterWaves", "Water waves", "100" },
    { "atmosWaterClarity", "Water clarity", "50" },
    { "atmosTime", "Time of day on this screen", "0" },
    { "atmosTimeMode", "Time source", "0" },
    { "atmosTimeHour", "Fixed hour", "13" },
};

static CVar* s_vars[A_Count];
static float s_v[A_Count] = {
    0, 1, 20, 12, 190, 7, 2,
    1, 15, 25, 45, 50, 210,
    1, 1, 1, 20, 1, 35,
    1, 40,
    1, 55, 25, 0,
    1, 45, 15, 1,
    1, 100, 100, 100,
    1, 26, 32, 51,
    1, 12,
    1, 2, 1,
    1, 25, 100, 100, 50,
    0, 0, 13,
};

struct AtmosLamp {
    float x, y, z, range;
    float r, g, b;
    int32_t beacon;
};

static AtmosLamp s_lamps[Atmosphere_CollectLamps];
static int32_t s_lampCount;
static float s_sun[3] = { 0.42f, 0.28f, 0.86f };
static float s_sunBrightness = -1.0f;
static float s_player[3];
static float s_camera[3];
static float s_fog[3] = { 0.45f, 0.55f, 0.7f };
static float s_day = 0.5f;
static float s_weather;
static float s_timeSec;
static int32_t s_interior;
static int32_t s_drawKind;
static int32_t s_inWorld;

static int32_t On(int id) {
    return s_v[id] > 0.5f;
}

static float Clamped(int id, float minValue, float maxValue) {
    float value = s_v[id];
    if (value < minValue) {
        value = minValue;
    }
    if (value > maxValue) {
        value = maxValue;
    }
    return value;
}

static void Refresh() {
    if (!s_vars[A_Master]) {
        return;
    }

    for (int32_t i = 0; i < A_Count; i++) {
        if (s_vars[i]) {
            s_v[i] = s_vars[i]->m_floatValue;
        }
    }
}

static float NightAmount() {
    float hour = s_day * 24.0f;
    const float dusk = 20.0f;
    const float dawn = 5.0f;
    const float fade = 1.5f;

    if (hour >= dawn + fade && hour <= dusk) {
        return 0.0f;
    }
    if (hour > dusk && hour < dusk + fade) {
        return (hour - dusk) / fade;
    }
    if (hour >= dawn && hour < dawn + fade) {
        return 1.0f - (hour - dawn) / fade;
    }
    return 1.0f;
}

static void NormalizeSun() {
    float len = sqrtf(s_sun[0] * s_sun[0] + s_sun[1] * s_sun[1] + s_sun[2] * s_sun[2]);
    if (len < 1.0e-4f) {
        s_sun[0] = 0.42f;
        s_sun[1] = 0.28f;
        s_sun[2] = 0.86f;
        return;
    }
    s_sun[0] /= len;
    s_sun[1] /= len;
    s_sun[2] /= len;
}

void Atmosphere_Register() {
    if (s_vars[A_Master]) {
        return;
    }

    for (int32_t i = 0; i < A_Count; i++) {
        s_vars[i] = CVar::Register(kVars[i].name, kVars[i].help, 1, kVars[i].def, nullptr, 1, false, nullptr, false);
    }
    Refresh();
}

void Atmosphere_SetInWorld(int32_t inWorld) {
    s_inWorld = inWorld != 0;
}

void Atmosphere_BeginFrame() {
    Refresh();
    s_lampCount = 0;
    s_sunBrightness = -1.0f;
}

int32_t Atmosphere_Master() {
    return s_inWorld && On(A_Master);
}

int32_t Atmosphere_PostActive() {
    if (!s_inWorld || !On(A_Master)) {
        return 0;
    }
    return On(A_Volume) || On(A_Fog) || On(A_Shadows) || On(A_Rays) || On(A_Night) || On(A_Clouds) || On(A_Color) || On(A_Lamps) || On(A_Beacon);
}

float Atmosphere_GlowScale() {
    if (!s_inWorld || !On(A_Master)) {
        return 1.0f;
    }
    return Clamped(A_Glow, 0.0f, 200.0f) * 0.01f;
}

float Atmosphere_TimeSec() {
    return s_timeSec;
}

void Atmosphere_Camera(float& x, float& y, float& z) {
    x = s_camera[0];
    y = s_camera[1];
    z = s_camera[2];
}

void Atmosphere_ApplyTime(float& dayProgression, uint32_t& timeOfDay) {
    Refresh();
    if (!s_inWorld || !On(A_Master) || !On(A_Time) || s_v[A_TimeMode] < 0.5f) {
        return;
    }

    float hour;
    if (s_v[A_TimeMode] < 1.5f) {
        time_t now = time(nullptr);
        tm local = {};
#if defined(_WIN32)
        localtime_s(&local, &now);
#else
        localtime_r(&now, &local);
#endif
        hour = static_cast<float>(local.tm_hour) + static_cast<float>(local.tm_min) / 60.0f;
    } else {
        hour = Clamped(A_TimeHour, 0.0f, 24.0f);
        if (hour >= 24.0f) {
            hour = 0.0f;
        }
    }

    dayProgression = hour / 24.0f;
    timeOfDay = static_cast<uint32_t>(hour * 60.0f);
}

void Atmosphere_NoteWorld(float playerX, float playerY, float playerZ, float cameraX, float cameraY, float cameraZ, float day, float weather, int32_t interior, float fogR, float fogG, float fogB, float timeSec) {
    s_player[0] = playerX;
    s_player[1] = playerY;
    s_player[2] = playerZ;
    s_camera[0] = cameraX;
    s_camera[1] = cameraY;
    s_camera[2] = cameraZ;
    s_day = day < 0.0f ? 0.0f : (day > 1.0f ? 1.0f : day);
    s_weather = weather < 0.0f ? 0.0f : weather;
    s_interior = interior != 0;
    s_fog[0] = fogR;
    s_fog[1] = fogG;
    s_fog[2] = fogB;
    s_timeSec = timeSec;
}

void Atmosphere_NoteSun(float x, float y, float z, float brightness) {
    if (z < 0.05f || brightness <= s_sunBrightness) {
        return;
    }
    s_sunBrightness = brightness;
    s_sun[0] = s_sun[0] * 0.8f + x * 0.2f;
    s_sun[1] = s_sun[1] * 0.8f + y * 0.2f;
    s_sun[2] = s_sun[2] * 0.8f + z * 0.2f;
}

void Atmosphere_NoteLamp(float x, float y, float z, float range, float r, float g, float b, int32_t beacon) {
    if (!s_inWorld || !On(A_Master) || s_lampCount >= Atmosphere_CollectLamps) {
        return;
    }

    float ox = x - s_camera[0];
    float oy = y - s_camera[1];
    float oz = z - s_camera[2];
    float fromOrigin = x * x + y * y + z * z;
    float cameraSpan = s_camera[0] * s_camera[0] + s_camera[1] * s_camera[1] + s_camera[2] * s_camera[2];
    // ʕ •ᴥ•ʔ✿ Depth comes back camera-relative. A light still in world yards has to come with it ✿ ʕ •ᴥ•ʔ
    if (cameraSpan > 400.0f * 400.0f && fromOrigin > 600.0f * 600.0f && ox * ox + oy * oy + oz * oz < 800.0f * 800.0f) {
        x -= s_camera[0];
        y -= s_camera[1];
        z -= s_camera[2];
    }

    int32_t limit = static_cast<int32_t>(Clamped(A_LampCount, 16.0f, static_cast<float>(Atmosphere_CollectLamps)));
    if (s_lampCount >= limit) {
        return;
    }

    for (int32_t i = 0; i < s_lampCount; i++) {
        float dx = s_lamps[i].x - x;
        float dy = s_lamps[i].y - y;
        float dz = s_lamps[i].z - z;
        if (dx * dx + dy * dy + dz * dz < 1.0f) {
            if (beacon && !s_lamps[i].beacon) {
                s_lamps[i].beacon = 1;
            }
            return;
        }
    }

    AtmosLamp& lamp = s_lamps[s_lampCount++];
    lamp.x = x;
    lamp.y = y;
    lamp.z = z;
    lamp.range = range;
    lamp.r = r;
    lamp.g = g;
    lamp.b = b;
    lamp.beacon = beacon;
}

void Atmosphere_BeginDraw(int32_t kind) {
    if (!s_inWorld || !On(A_Master)) {
        return;
    }
    if (kind == Atmosphere_GrassDraw && On(A_Grass)) {
        s_drawKind = kind;
    } else if (kind == Atmosphere_WaterDraw && On(A_Water)) {
        s_drawKind = kind;
    }
}

void Atmosphere_EndDraw() {
    s_drawKind = 0;
}

int32_t Atmosphere_DrawKind() {
    return s_drawKind;
}

void Atmosphere_GrassParams(float& wind, float& time, float& radius, float& force, float& playerX, float& playerY, float& playerZ) {
    wind = Clamped(A_GrassWind, 0.0f, 5.0f);
    time = s_timeSec;
    radius = On(A_GrassPart) ? 2.0f : 0.0f;
    force = 0.5f;
    playerX = s_player[0];
    playerY = s_player[1];
    playerZ = s_player[2];
}

void Atmosphere_WaterParams(float& height, float& time, float& foam, float& glint, float& clarity, float& rain, float& sunset) {
    height = Clamped(A_WaterWaves, 0.0f, 300.0f) * 0.01f * 2.4f;
    time = s_timeSec;
    foam = Clamped(A_WaterFoam, 0.0f, 100.0f) * 0.01f;
    glint = Clamped(A_WaterGlint, 0.0f, 300.0f) * 0.01f;
    clarity = Clamped(A_WaterClarity, 0.0f, 100.0f) * 0.01f;
    rain = s_weather;
    float up = s_sun[2];
    sunset = up > 0.0f && up < 0.45f ? 1.0f - up / 0.45f : 0.0f;
}

void Atmosphere_FillGpu(AtmosGpu& gpu, const float viewProj[16], const float invViewProj[16], float nearZ, float farZ, uint32_t width, uint32_t height, int32_t depthValid) {
    Refresh();
    gpu = {};

    for (int32_t i = 0; i < 16; i++) {
        gpu.viewProj[i] = viewProj[i];
        gpu.invViewProj[i] = invViewProj[i];
    }

    NormalizeSun();
    float night = NightAmount();
    if (s_interior && !On(A_NightIndoors)) {
        night = 0.0f;
    }

    gpu.camera[0] = s_camera[0];
    gpu.camera[1] = s_camera[1];
    gpu.camera[2] = s_camera[2];
    gpu.camera[3] = s_timeSec;
    gpu.sun[0] = s_sun[0];
    gpu.sun[1] = s_sun[1];
    gpu.sun[2] = s_sun[2];
    gpu.sun[3] = night;
    gpu.fogColor[0] = s_fog[0];
    gpu.fogColor[1] = s_fog[1];
    gpu.fogColor[2] = s_fog[2];
    gpu.fogColor[3] = s_weather;
    gpu.player[0] = s_player[0];
    gpu.player[1] = s_player[1];
    gpu.player[2] = s_player[2];
    gpu.player[3] = farZ;

    int32_t master = On(A_Master);
    gpu.light[0] = master && On(A_Volume) ? 1.0f : 0.0f;
    gpu.light[1] = Clamped(A_VolStrength, 0.0f, 100.0f) * 0.01f;
    gpu.light[2] = Clamped(A_VolDensity, 0.0f, 40.0f) * 0.001f;
    gpu.light[3] = Clamped(A_VolDist, 20.0f, 300.0f);
    gpu.light2[0] = Clamped(A_VolToward, 0.0f, 80.0f) * 0.01f;
    gpu.light2[1] = Clamped(A_VolQuality, 1.0f, 3.0f);
    gpu.light2[2] = master && On(A_Fog) ? 1.0f : 0.0f;
    gpu.light2[3] = Clamped(A_FogDensity, 0.0f, 80.0f) * 0.0001f;

    gpu.fog[0] = Clamped(A_FogHeight, 4.0f, 80.0f);
    gpu.fog[1] = Clamped(A_FogBright, 0.0f, 100.0f) * 0.01f;
    gpu.fog[2] = Clamped(A_FogPatches, 0.0f, 100.0f) * 0.01f;
    gpu.fog[3] = Clamped(A_FogWater, 0.0f, 400.0f) * 0.01f;

    gpu.shadow[0] = master && On(A_Shadows) ? 1.0f : 0.0f;
    gpu.shadow[1] = Clamped(A_ShStrength, 0.0f, 100.0f) * 0.01f;
    gpu.shadow[2] = Clamped(A_ShSoft, 0.0f, 2.0f);
    gpu.shadow[3] = Clamped(A_Sunlight, 0.0f, 50.0f) * 0.01f;
    gpu.shadow2[0] = On(A_ShWorld) ? 1.0f : 0.0f;
    gpu.shadow2[1] = On(A_ShUnits) ? 1.0f : 0.0f;
    gpu.shadow2[2] = master && On(A_Rays) ? 1.0f : 0.0f;
    gpu.shadow2[3] = Clamped(A_RayStrength, 0.0f, 100.0f) * 0.01f;

    gpu.night[0] = master && On(A_Night) ? 1.0f : 0.0f;
    gpu.night[1] = Clamped(A_NightDark, 0.0f, 90.0f) * 0.01f;
    gpu.night[2] = Clamped(A_NightStrength, 0.0f, 100.0f) * 0.01f;
    gpu.night[3] = s_weather;

    gpu.cloud[0] = master && On(A_Clouds) ? 1.0f : 0.0f;
    gpu.cloud[1] = Clamped(A_CloudCover, 0.0f, 100.0f) * 0.01f;
    gpu.cloud[2] = Clamped(A_CloudSpeed, 0.0f, 40.0f);
    gpu.cloud[3] = On(A_CloudShapes) ? 1.0f : 0.0f;

    gpu.grade[0] = master && On(A_Color) ? 1.0f : 0.0f;
    gpu.grade[1] = Clamped(A_DaySat, 0.0f, 200.0f) * 0.01f;
    gpu.grade[2] = Clamped(A_NightSat, 0.0f, 200.0f) * 0.01f;
    gpu.grade[3] = 0.0f;

    int32_t lampCap = static_cast<int32_t>(Clamped(A_LampCount, 16.0f, static_cast<float>(Atmosphere_CollectLamps)));
    if (lampCap > Atmosphere_ShadeLamps) {
        lampCap = Atmosphere_ShadeLamps;
    }
    gpu.lamp[0] = master && On(A_Lamps) ? 1.0f : 0.0f;
    gpu.lamp[1] = Clamped(A_LampGlow, 0.0f, 50.0f) * 0.55f;
    gpu.lamp[2] = static_cast<float>(lampCap);
    gpu.lamp[3] = Clamped(A_LampDay, 0.0f, 100.0f) * 0.01f;

    gpu.beacon[0] = master && On(A_Beacon) ? 1.0f : 0.0f;
    gpu.beacon[1] = Clamped(A_Beam, 0.0f, 100.0f) * 0.01f;
    gpu.beacon[2] = s_interior ? 1.0f : 0.0f;
    gpu.beacon[3] = 1.0f - night;

    gpu.texel[0] = width ? 1.0f / static_cast<float>(width) : 1.0f;
    gpu.texel[1] = height ? 1.0f / static_cast<float>(height) : 1.0f;
    gpu.texel[2] = depthValid ? 1.0f : 0.0f;
    gpu.texel[3] = nearZ;

    float wind = 0.78539816f;
    gpu.wind[0] = cosf(wind);
    gpu.wind[1] = sinf(wind);
    gpu.wind[2] = s_timeSec;
    gpu.wind[3] = farZ > nearZ ? farZ : 1000.0f;

    if (gpu.lamp[0] < 0.5f && gpu.beacon[0] < 0.5f) {
        return;
    }

    int32_t chosen[Atmosphere_ShadeLamps];
    int32_t chosenCount = 0;
    float chosenDist[Atmosphere_ShadeLamps];

    for (int32_t i = 0; i < s_lampCount; i++) {
        float dist = s_lamps[i].x * s_lamps[i].x + s_lamps[i].y * s_lamps[i].y + s_lamps[i].z * s_lamps[i].z;
        if (chosenCount < lampCap) {
            chosen[chosenCount] = i;
            chosenDist[chosenCount] = dist;
            chosenCount++;
            continue;
        }
        int32_t worst = 0;
        for (int32_t j = 1; j < chosenCount; j++) {
            if (chosenDist[j] > chosenDist[worst]) {
                worst = j;
            }
        }
        if (dist < chosenDist[worst]) {
            chosen[worst] = i;
            chosenDist[worst] = dist;
        }
    }

    for (int32_t i = 0; i < chosenCount; i++) {
        const AtmosLamp& lamp = s_lamps[chosen[i]];
        gpu.lamps[i].posRange[0] = lamp.x;
        gpu.lamps[i].posRange[1] = lamp.y;
        gpu.lamps[i].posRange[2] = lamp.z;
        gpu.lamps[i].posRange[3] = lamp.range;
        gpu.lamps[i].color[0] = lamp.r;
        gpu.lamps[i].color[1] = lamp.g;
        gpu.lamps[i].color[2] = lamp.b;
        gpu.lamps[i].color[3] = lamp.beacon ? 1.0f : 0.0f;
    }
    gpu.lamp[2] = static_cast<float>(chosenCount);
}
