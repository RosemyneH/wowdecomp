#version 450
#extension GL_GOOGLE_include_directive : require

#include "FfpUniforms.glsl"

layout(set = 0, binding = 1) uniform sampler2D tex0;
layout(set = 0, binding = 2) uniform sampler2D tex1;

layout(location = 0) in vec4 inColor;
layout(location = 1) in vec3 inTexCoord0;
layout(location = 2) in vec3 inTexCoord1;
layout(location = 3) in float inFog;

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

void main() {
    vec4 color = inColor;

    if (u.stageColorOp.x >= 0) {
        color = Stage(0, texture(tex0, inTexCoord0.xy / inTexCoord0.z), color);

        if (u.stageColorOp.y >= 0) {
            color = Stage(1, texture(tex1, inTexCoord1.xy / inTexCoord1.z), color);
        }
    }

    if (u.flags.w > 0 && color.a * 255.0 < float(u.flags.w)) {
        discard;
    }

    color.rgb = mix(u.fogColor.rgb, color.rgb, inFog);
    outColor = color;
}
