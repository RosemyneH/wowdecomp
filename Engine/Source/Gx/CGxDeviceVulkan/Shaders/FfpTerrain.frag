#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

#include "FfpUniforms.glsl"

layout(set = 1, binding = 0) uniform sampler2D terrainTextures[];

layout(location = 0) in vec4 inColor;
layout(location = 1) in vec3 inTexCoord0;
layout(location = 2) in vec3 inTexCoord1;
layout(location = 3) in float inFog;
layout(location = 5) flat in ivec4 inLayerTex;
layout(location = 6) flat in ivec4 inAlphaTex;

layout(location = 0) out vec4 outColor;

vec4 TexOp(int op, vec4 tex, vec4 cur) {
    switch (op) {
        case 0: return tex * cur;
        case 1: return tex * cur * 2.0;
        case 2: return tex + cur;
        case 3: return cur;
        case 4: return mix(tex, cur, cur.a);
        case 5: return mix(cur, tex, inColor.a);
    }

    return cur;
}

vec4 Stage(int stage, vec4 tex, vec4 cur) {
    return clamp(vec4(TexOp(u.stageColorOp[stage], tex, cur).rgb, TexOp(u.stageAlphaOp[stage], tex, cur).a), 0.0, 1.0);
}

// ʕ •ᴥ•ʔ✿ 64² blend map. Raw 0..1 puts the chunk edge on the texel border, and clamp turns that border into a hard diagonal ✿ ʕ •ᴥ•ʔ
vec2 BlendUv(vec3 coord) {
    vec2 uv = coord.xy / coord.z;
    return uv * (63.0 / 64.0) + (0.5 / 64.0);
}

void main() {
    vec4 color = inColor;

    // ʕ •ᴥ•ʔ✿ Mode 3 is the chunk shadow overlay. One indirect draw, the shadow map picked from the bindless table ✿ ʕ •ᴥ•ʔ
    if (int(u.misc.y) == 3) {
        if (inLayerTex.x >= 0) {
            color = Stage(0, texture(terrainTextures[nonuniformEXT(inLayerTex.x)], inTexCoord0.xy / inTexCoord0.z), color);
        }

        if (u.stageColorOp.y >= 0 && inAlphaTex.x >= 0) {
            color = Stage(1, texture(terrainTextures[nonuniformEXT(inAlphaTex.x)], inTexCoord1.xy / inTexCoord1.z), color);
        }

        if (u.flags.w > 0 && color.a * 255.0 < float(u.flags.w)) {
            discard;
        }

        color.rgb = mix(u.fogColor.rgb, color.rgb, inFog);
        outColor = color;
        return;
    }

    int layers = 0;

    // -- ʕ •ᴥ•ʔ✿ One chunk. Layer 0 replaces. Later layers lerp by coverage, the same src-alpha blend the multi-pass used ✿ ʕ •ᴥ•ʔ
    if (u.stageColorOp.x >= 0) {
        for (int i = 0; i < 4; i++) {
            if (inLayerTex[i] < 0) {
                continue;
            }

            vec4 layer = Stage(0, texture(terrainTextures[nonuniformEXT(inLayerTex[i])], inTexCoord0.xy / inTexCoord0.z), inColor);

            // -- ʕ •ᴥ•ʔ✿ Coverage lives in alpha. An 8-bit map repeats that weight in rgb, and modulating with it squares the fade into blocks ✿ ʕ •ᴥ•ʔ
            if (inAlphaTex[i] >= 0) {
                layer.a *= texture(terrainTextures[nonuniformEXT(inAlphaTex[i])], BlendUv(inTexCoord1)).a;
            }

            // -- ʕ •ᴥ•ʔ✿ Alpha blend's ref is 1. A zero weight does not cover the base, and it does not discard the pixel ✿ ʕ •ᴥ•ʔ
            if (layers > 0 && layer.a * 255.0 < 1.0) {
                continue;
            }

            if (layers == 0) {
                color = layer;
            } else {
                color.rgb = mix(color.rgb, layer.rgb, layer.a);
            }

            layers++;
        }
    }

    if (u.flags.w > 0 && color.a * 255.0 < float(u.flags.w)) {
        discard;
    }

    // -- ʕ •ᴥ•ʔ✿ Fog is a lerp toward one color. Doing it once matches fogging each layer and then blending ✿ ʕ •ᴥ•ʔ
    color.rgb = mix(u.fogColor.rgb, color.rgb, inFog);
    outColor = color;
}
