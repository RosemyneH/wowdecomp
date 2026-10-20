#ifndef ENGINE_SOURCE_GX_ATMOSPHERE_H
#define ENGINE_SOURCE_GX_ATMOSPHERE_H

#include <cstdint>

enum {
    Atmosphere_GrassDraw = 1,
    Atmosphere_WaterDraw = 2,
    Atmosphere_ShadeLamps = 64,
    Atmosphere_CollectLamps = 256,
};

struct AtmosLampGpu {
    float posRange[4];
    float color[4];
};

// ʕ •ᴥ•ʔ✿ std140, row-major matrices. Field order is the Atmosphere.frag block ✿ ʕ •ᴥ•ʔ
struct AtmosGpu {
    float viewProj[16];
    float invViewProj[16];
    float camera[4];
    float sun[4];
    float fogColor[4];
    float player[4];
    float light[4];
    float light2[4];
    float fog[4];
    float shadow[4];
    float shadow2[4];
    float night[4];
    float cloud[4];
    float grade[4];
    float lamp[4];
    float beacon[4];
    float texel[4];
    float wind[4];
    AtmosLampGpu lamps[Atmosphere_ShadeLamps];
};

static_assert(sizeof(AtmosLampGpu) == 32, "AtmosLampGpu");
static_assert(sizeof(AtmosGpu) == 128 + 16 * 16 + 32 * Atmosphere_ShadeLamps, "AtmosGpu");

void Atmosphere_Register();
void Atmosphere_BeginFrame();
int32_t Atmosphere_Master();
int32_t Atmosphere_PostActive();
float Atmosphere_GlowScale();
float Atmosphere_TimeSec();
void Atmosphere_Camera(float& x, float& y, float& z);
void Atmosphere_ApplyTime(float& dayProgression, uint32_t& timeOfDay);
void Atmosphere_NoteWorld(float playerX, float playerY, float playerZ, float cameraX, float cameraY, float cameraZ, float day, float weather, int32_t interior, float fogR, float fogG, float fogB, float timeSec);
void Atmosphere_NoteSun(float x, float y, float z, float brightness);
void Atmosphere_NoteLamp(float x, float y, float z, float range, float r, float g, float b, int32_t beacon);
void Atmosphere_BeginDraw(int32_t kind);
void Atmosphere_EndDraw();
int32_t Atmosphere_DrawKind();
void Atmosphere_GrassParams(float& wind, float& time, float& radius, float& force, float& playerX, float& playerY, float& playerZ);
void Atmosphere_WaterParams(float& height, float& time, float& foam, float& glint, float& clarity, float& rain, float& sunset);
void Atmosphere_FillGpu(AtmosGpu& gpu, const float viewProj[16], const float invViewProj[16], float nearZ, float farZ, uint32_t width, uint32_t height, int32_t depthValid);

#endif
