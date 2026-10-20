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
        float wx = cos(inWorld.x * 0.11 + t * 1.3) * 0.11 * sin(inWorld.y * 0.09 - t * 0.8);
        float wy = sin(inWorld.x * 0.11 + t * 1.3) * cos(inWorld.y * 0.09 - t * 0.8) * 0.09;
        vec3 normal = normalize(vec3(-wx * u.fogParams.z, -wy * u.fogParams.z, 1.0));
        vec3 viewDir = normalize(-inView);
        float fresnel = pow(1.0 - clamp(abs(dot(normal, viewDir)), 0.0, 1.0), 3.0);
        vec3 deep = mix(vec3(0.03, 0.16, 0.14), vec3(0.45, 0.22, 0.1), u.lightAtten[3].z);
        color.rgb = mix(deep, color.rgb, mix(0.45, 0.92, clamp(u.lightAtten[3].x, 0.0, 1.0)));
        vec3 sunDir = u.lightPos[0].w == 0.0 ? normalize(-u.lightPos[0].xyz) : vec3(0.3, 0.2, 0.9);
        vec3 halfDir = normalize(sunDir + viewDir);
        float spec = pow(max(dot(normal, halfDir), 0.0), 28.0);
        color.rgb += vec3(1.0, 0.96, 0.86) * (spec * 1.6 + fresnel * 0.45) * u.matEmissive.w;
        float crest = sin(inWorld.x * 0.11 + t * 1.3) * sin(inWorld.y * 0.09 - t * 0.8);
        color.rgb = mix(color.rgb, vec3(0.86, 0.91, 0.93), clamp(crest, 0.0, 1.0) * u.ambient.w);
        if (u.lightAtten[3].y > 0.05) {
            float ring = sin(length(inWorld.xy) * 1.7 - t * 6.0);
            color.rgb += vec3(0.75, 0.84, 0.88) * smoothstep(0.82, 1.0, ring) * u.lightAtten[3].y * 0.35;
        }
    }

    if (u.flags.w > 0 && color.a * 255.0 < float(u.flags.w)) {
        discard;
    }

    color.rgb = mix(u.fogColor.rgb, color.rgb, inFog);
    outColor = color;
}
