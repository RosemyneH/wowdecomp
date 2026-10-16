#version 450
#extension GL_GOOGLE_include_directive : require

#include "FfpUniforms.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec2 inTexCoord0;
layout(location = 4) in vec2 inTexCoord1;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec3 outTexCoord0;
layout(location = 2) out vec3 outTexCoord1;
layout(location = 3) out float outFog;

vec3 StageTexCoord(int stage, vec3 viewPos, vec3 viewNormal) {
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

    vec4 t = src * u.texMatrix[stage];
    return mode == 2 ? t.xyz : vec3(t.xy, 1.0);
}

void main() {
    vec4 worldPos = vec4(inPosition, 1.0) * u.world;
    vec4 viewPos = worldPos * u.view;
    gl_Position = viewPos * u.proj;
    gl_PointSize = u.misc.x;

    vec3 worldNormal = (vec4(inNormal, 0.0) * u.world).xyz;
    worldNormal = dot(worldNormal, worldNormal) > 0.0 ? normalize(worldNormal) : worldNormal;
    vec3 viewNormal = (vec4(worldNormal, 0.0) * u.view).xyz;

    vec4 color = inColor;

    if (u.flags.x != 0) {
        vec4 diffuse = u.flags.y == 0 ? inColor : u.matDiffuse;
        vec3 emissive = u.flags.y == 2 ? inColor.rgb : u.matEmissive.rgb;
        vec3 amb = u.ambient.rgb;
        vec3 diff = vec3(0.0);

        for (int i = 0; i < 4; i++) {
            if (u.lightDiffuse[i].w == 0.0) {
                continue;
            }

            vec3 dir;
            float atten = 1.0;

            if (u.lightPos[i].w != 0.0) {
                vec3 delta = u.lightPos[i].xyz - worldPos.xyz;
                float dist = length(delta);
                dir = delta / max(dist, 1e-6);
                atten = 1.0 / max(dot(u.lightAtten[i].xyz, vec3(1.0, dist, dist * dist)), 1e-6);
            } else {
                dir = -normalize(u.lightPos[i].xyz);
            }

            amb += u.lightAmbient[i].rgb * atten;
            diff += u.lightDiffuse[i].rgb * max(dot(worldNormal, dir), 0.0) * atten;
        }

        color = vec4(emissive + diffuse.rgb * (amb + diff), diffuse.a);
    }

    outColor = clamp(color, 0.0, 1.0);
    outTexCoord0 = StageTexCoord(0, viewPos.xyz, viewNormal);
    outTexCoord1 = StageTexCoord(1, viewPos.xyz, viewNormal);
    outFog = u.flags.z != 0 ? clamp((u.fogParams.y - abs(viewPos.z)) / (u.fogParams.y - u.fogParams.x), 0.0, 1.0) : 1.0;
}
