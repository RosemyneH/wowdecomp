#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

#include "FfpUniforms.glsl"

layout(set = 1, binding = 0) uniform sampler2D terrainTextures[];

layout(location = 0) in vec4 inColor;
layout(location = 1) in vec3 inTexCoord0;
layout(location = 2) in vec3 inTexCoord1;
layout(location = 3) in float inFog;
layout(location = 4) flat in ivec2 inTexIndex;

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

    if (u.stageColorOp.x >= 0) {
        color = Stage(0, texture(terrainTextures[nonuniformEXT(inTexIndex.x)], inTexCoord0.xy / inTexCoord0.z), color);

        // ʕ •ᴥ•ʔ✿ A base layer has no blend map. Sampling the default image here punches a second texture through the ground ✿ ʕ •ᴥ•ʔ
        if (u.stageColorOp.y >= 0 && inTexIndex.y >= 0) {
            vec4 blend = texture(terrainTextures[nonuniformEXT(inTexIndex.y)], BlendUv(inTexCoord1));
            // ʕ •ᴥ•ʔ✿ Coverage lives in alpha. An 8-bit map repeats that weight in rgb, and modulating with it squares the fade into blocks ✿ ʕ •ᴥ•ʔ
            color.a *= blend.a;
        }
    }

    if (u.flags.w > 0 && color.a * 255.0 < float(u.flags.w)) {
        discard;
    }

    color.rgb = mix(u.fogColor.rgb, color.rgb, inFog);
    outColor = color;
}
