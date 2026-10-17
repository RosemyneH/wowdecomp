#version 450
#extension GL_GOOGLE_include_directive : require

#include "FfpUniforms.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec2 inTexCoord0;
layout(location = 4) in vec2 inTexCoord1;
layout(location = 5) in vec4 inWeights;
layout(location = 6) in uvec4 inIndices;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec3 outTexCoord0;
layout(location = 2) out vec3 outTexCoord1;
layout(location = 3) out float outFog;
layout(location = 4) flat out ivec2 outTexIndex;

// ʕ •ᴥ•ʔ✿ Row-major, matching C44Matrix. One record per instance or per terrain draw ✿ ʕ •ᴥ•ʔ
struct GpuDrawItem {
    mat4 world;
    mat4 tex0;
    mat4 tex1;
    vec4 lightPos[4];
    vec4 lightDiffuse[4];
    vec4 lightAmbient[4];
    vec4 lightAtten[4];
};

layout(std430, set = 0, binding = 3, row_major) readonly buffer GpuDrawItems {
    GpuDrawItem items[];
} gpu;

layout(std430, set = 0, binding = 4, row_major) readonly buffer BonePalette {
    mat4 bones[];
} skin;

layout(push_constant) uniform SkinPush {
    uint base;
    uint influences;
    uint stride;
} pc;

vec3 StageTexCoord(int stage, vec3 viewPos, vec3 viewNormal, mat4 texMatrix) {
    int gen = u.stageTexGen[stage];
    vec4 src;

    if (gen == 0) {
        src = vec4(u.stageCoord[stage] == 0 ? inTexCoord0 : inTexCoord1, 1.0, 0.0);
    } else if (gen <= 3) {
        src = vec4(viewPos, 1.0);
    } else if (gen == 5) {
        src = vec4(viewNormal, 1.0);
    } else {
        src = vec4(reflect(normalize(viewPos), viewNormal), 1.0);
    }

    int mode = u.stageTexMode[stage];

    if (mode == 0) {
        return vec3(src.xy, 1.0);
    }

    vec4 t = src * texMatrix;
    return mode == 2 ? t.xyz : vec3(t.xy, 1.0);
}

void main() {
    int gpuMode = int(u.misc.y);
    mat4 worldMat = u.world;
    mat4 tex0 = u.texMatrix[0];
    mat4 tex1 = u.texMatrix[1];
    vec4 lightPos[4] = u.lightPos;
    vec4 lightDiffuse[4] = u.lightDiffuse;
    vec4 lightAmbient[4] = u.lightAmbient;
    vec4 lightAtten[4] = u.lightAtten;
    ivec2 texIndex = ivec2(0);

    if (gpuMode != 0) {
        GpuDrawItem item = gpu.items[gl_InstanceIndex];
        worldMat = item.world;

        if (gpuMode == 2) {
            tex0 = item.tex0;
            tex1 = item.tex1;
            lightPos = item.lightPos;
            lightDiffuse = item.lightDiffuse;
            lightAmbient = item.lightAmbient;
            lightAtten = item.lightAtten;
            // ʕ •ᴥ•ʔ✿ Slot ids are plain floats. Raw bits below 2^23 are denormals and flush to zero ✿ ʕ •ᴥ•ʔ
            texIndex = ivec2(int(item.lightAmbient[0].w + 0.5), int(item.lightAmbient[1].w + 0.5));
        }
    }

    outTexIndex = texIndex;

    vec3 position = inPosition;
    vec3 normal = inNormal;

    // ʕ •ᴥ•ʔ✿ Palette base is a push constant. One draw can cover many copies via gl_InstanceIndex ✿ ʕ •ᴥ•ʔ
    if (u.misc[2] != 0.0) {
        uint palette = pc.base + gl_InstanceIndex * pc.stride;
        int influences = int(pc.influences);

        if (influences == 1) {
            mat4 bone = skin.bones[palette + inIndices.x];
            position = (vec4(inPosition, 1.0) * bone).xyz;
            normal = (vec4(inNormal, 0.0) * bone).xyz;
        } else {
            mat4 bone0 = skin.bones[palette + inIndices.x];
            position = (vec4(inPosition, 1.0) * bone0).xyz * inWeights.x;
            normal = (vec4(inNormal, 0.0) * bone0).xyz * inWeights.x;

            for (int influence = 1; influence < 4; influence++) {
                float weight = inWeights[influence];

                if (weight == 0.0) {
                    break;
                }

                mat4 bone = skin.bones[palette + inIndices[influence]];
                position += (vec4(inPosition, 1.0) * bone).xyz * weight;
                normal += (vec4(inNormal, 0.0) * bone).xyz * weight;
            }
        }
    }

    vec4 worldPos = vec4(position, 1.0) * worldMat;
    vec4 viewPos = worldPos * u.view;
    gl_Position = viewPos * u.proj;
    gl_PointSize = u.misc.x;

    vec3 worldNormal = (vec4(normal, 0.0) * worldMat).xyz;
    worldNormal = dot(worldNormal, worldNormal) > 0.0 ? normalize(worldNormal) : worldNormal;
    vec3 viewNormal = (vec4(worldNormal, 0.0) * u.view).xyz;

    vec4 color = inColor;

    if (u.flags.x != 0) {
        vec4 diffuse = u.flags.y == 0 ? inColor : u.matDiffuse;
        vec3 emissive = u.flags.y == 2 ? inColor.rgb : u.matEmissive.rgb;
        vec3 amb = u.ambient.rgb;
        vec3 diff = vec3(0.0);

        for (int i = 0; i < 4; i++) {
            if (lightDiffuse[i].w == 0.0) {
                continue;
            }

            vec3 dir;
            float atten = 1.0;

            if (lightPos[i].w != 0.0) {
                vec3 delta = lightPos[i].xyz - worldPos.xyz;
                float dist = length(delta);
                dir = delta / max(dist, 1e-6);
                atten = 1.0 / max(dot(lightAtten[i].xyz, vec3(1.0, dist, dist * dist)), 1e-6);
            } else {
                dir = -normalize(lightPos[i].xyz);
            }

            amb += lightAmbient[i].rgb * atten;
            diff += lightDiffuse[i].rgb * max(dot(worldNormal, dir), 0.0) * atten;
        }

        color = vec4(emissive + diffuse.rgb * (amb + diff), diffuse.a);
    }

    outColor = clamp(color, 0.0, 1.0);
    outTexCoord0 = StageTexCoord(0, viewPos.xyz, viewNormal, tex0);
    outTexCoord1 = StageTexCoord(1, viewPos.xyz, viewNormal, tex1);
    outFog = u.flags.z != 0 ? clamp((u.fogParams.y - abs(viewPos.z)) / (u.fogParams.y - u.fogParams.x), 0.0, 1.0) : 1.0;
}
