#version 450
#extension GL_GOOGLE_include_directive : require

#include "FfpUniforms.glsl"

layout(set = 0, binding = 1) uniform sampler2D tex0;
layout(set = 0, binding = 2) uniform sampler2D tex1;

layout(location = 0) in vec4 inColor;
layout(location = 1) in vec3 inTexCoord0;
layout(location = 2) in vec3 inTexCoord1;
layout(location = 3) in float inFog;
layout(location = 7) in vec3 inWorld;
layout(location = 8) in vec3 inWorldNormal;
layout(location = 9) in vec3 inView;

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

    // ʕ •ᴥ•ʔ✿ Desaturate.bls writes luma into rgb and keeps only vertex alpha. The 0.65 icon tint never reaches the shader ✿ ʕ •ᴥ•ʔ
    if (u.stageColorOp.z > 0) {
        vec4 tex = texture(tex0, uv);
        float luma = dot(tex.rgb, vec3(0.299, 0.587, 0.114));
        vec4 color = vec4(luma, luma, luma, tex.a * inColor.a);

        if (u.flags.w > 0 && color.a * 255.0 < float(u.flags.w)) {
            discard;
        }

        color.rgb = mix(u.fogColor.rgb, color.rgb, inFog);
        outColor = color;
        return;
    }

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
        float spread = max(0.5 * dot(vec2(range / 256.0), vec2(1.0) / uvWidth), 1.0);
        // ʕ •ᴥ•ʔ✿ Sharpness can tighten the edge, but a subpixel step turns Friz into stair pixels ✿ ʕ •ᴥ•ʔ
        float px = clamp(spread * sharp, 1.15, 2.4);
        float dist = sd * px;
        // ʕ •ᴥ•ʔ✿ The stored field ends at half a pixel-range. Past that the quad must stay clear, or each word sits on a box ✿ ʕ •ᴥ•ʔ
        float budget = max(0.5 * px - 0.55, 0.0);
        float outlinePx = colorOp == 8 ? 1.25 : (colorOp == 7 ? 0.75 : 0.0);
        outlinePx = min(outlinePx, budget);
        float weight = min(0.35, max(budget - outlinePx, 0.0));
        float body = clamp(dist + 0.5 + weight, 0.0, 1.0);
        float alpha = clamp(dist + 0.5 + weight + outlinePx, 0.0, 1.0);
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

    if (u.stageColorOp.w == 2) {
        float t = u.fogParams.w;
        vec3 viewDir = normalize(-inView);
        float fresnel = pow(1.0 - clamp(abs(viewDir.z), 0.0, 1.0), 2.0);
        vec3 deep = mix(vec3(0.04, 0.2, 0.22), vec3(0.35, 0.18, 0.08), clamp(u.lightAtten[3].z, 0.0, 1.0));
        color.rgb = mix(deep, color.rgb, mix(0.55, 0.9, clamp(u.lightAtten[3].x, 0.0, 1.0)));
        float wave = 0.5 + 0.5 * sin(inWorld.x * 0.08 + t * 0.7) * sin(inWorld.y * 0.07 - t * 0.5);
        color.rgb = mix(color.rgb, vec3(0.75, 0.84, 0.86), wave * u.ambient.w * 0.35);
        color.rgb += vec3(0.85, 0.9, 0.95) * fresnel * u.matEmissive.w * 0.35;
    }

    // ʕ •ᴥ•ʔ✿ Alpha-key foliage keeps its mask in the texture. Combiners that select vertex alpha would otherwise draw solid cards ✿ ʕ •ᴥ•ʔ
    if (u.flags.w > 1 && u.stageColorOp.x >= 0) {
        float texA = texture(tex0, inTexCoord0.xy / inTexCoord0.z).a;
        if (u.stageAlphaOp.x == 3) {
            color.a *= texA;
        }
        if (texA * 255.0 < float(u.flags.w)) {
            discard;
        }
    } else if (u.stageColorOp.x >= 0 && u.stageAlphaOp.x == 3 && u.flags.w > 0) {
        color.a *= texture(tex0, inTexCoord0.xy / inTexCoord0.z).a;
    }

    if (u.flags.w > 0 && color.a * 255.0 < float(u.flags.w)) {
        discard;
    }

    color.rgb = mix(u.fogColor.rgb, color.rgb, inFog);
    outColor = color;
}
