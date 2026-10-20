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

float MsdfMedian(float a, float b, float c) {
    return max(min(a, b), min(max(a, b), c));
}

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
    vec2 uv = inTexCoord0.xy / inTexCoord0.z;
    vec2 uvWidth = max(fwidth(uv), vec2(1e-6));
    int colorOp = u.stageColorOp.x;

    // ʕ •ᴥ•ʔ✿ Color ops 6/7/8 are MSDF, 1px outline, and 2px outline. fogParams.zw and stageColorOp.z are the live range, sharpness, and view ✿ ʕ •ᴥ•ʔ
    if (colorOp >= 6 && colorOp <= 8) {
        vec3 msdf = texture(tex0, uv).rgb;
        float sd = MsdfMedian(msdf.r, msdf.g, msdf.b) - 0.5;
        int msdfView = u.stageColorOp.z;

        if (msdfView == 1) {
            outColor = vec4(msdf, 1.0);
            return;
        }

        if (msdfView == 2) {
            float gray = clamp(sd + 0.5, 0.0, 1.0);
            outColor = vec4(vec3(gray), 1.0);
            return;
        }

        float range = u.fogParams.z > 0.0 ? u.fogParams.z : 4.0;
        float sharp = u.fogParams.w > 0.0 ? u.fogParams.w : 1.0;
        float screenPxRange = max(0.5 * dot(vec2(range / 256.0), vec2(1.0) / uvWidth), 1.0) * sharp;
        float dist = sd * screenPxRange;
        float outlinePx = colorOp == 8 ? 2.0 : (colorOp == 7 ? 1.0 : 0.0);
        // ʕ •ᴥ•ʔ✿ The field only covers the chosen range. An outline that reaches the rim stays a box ✿ ʕ •ᴥ•ʔ
        outlinePx = min(outlinePx, max(0.5 * screenPxRange - 1.0, 0.0));
        float body = clamp(dist + 0.5, 0.0, 1.0);
        float alpha = clamp(dist + 0.5 + outlinePx, 0.0, 1.0);
        vec4 color = vec4(inColor.rgb * body / max(alpha, 1e-4), inColor.a * alpha);

        if (u.flags.w > 0 && color.a * 255.0 < float(u.flags.w)) {
            discard;
        }

        color.rgb = mix(u.fogColor.rgb, color.rgb, inFog);
        outColor = color;
        return;
    }

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
