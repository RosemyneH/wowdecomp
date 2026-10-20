#version 450

layout(set = 0, binding = 0) uniform sampler2D colorTex;
layout(set = 0, binding = 1) uniform sampler2D depthTex;

struct Lamp {
    vec4 posRange;
    vec4 color;
};

layout(std140, set = 0, binding = 2, row_major) uniform Atmos {
    mat4 viewProj;
    mat4 invViewProj;
    vec4 camera;
    vec4 sun;
    vec4 fogColor;
    vec4 player;
    vec4 light;
    vec4 light2;
    vec4 fog;
    vec4 shadow;
    vec4 shadow2;
    vec4 night;
    vec4 cloud;
    vec4 grade;
    vec4 lamp;
    vec4 beacon;
    vec4 texel;
    vec4 wind;
    Lamp lamps[64];
} a;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

float Hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float Noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(Hash(i), Hash(i + vec2(1.0, 0.0)), f.x), mix(Hash(i + vec2(0.0, 1.0)), Hash(i + vec2(1.0, 1.0)), f.x), f.y);
}

float Luma(vec3 color) {
    return dot(color, vec3(0.299, 0.587, 0.114));
}

vec3 WorldAt(vec2 sampleUv, float depth) {
    vec2 ndc = vec2(sampleUv.x * 2.0 - 1.0, 1.0 - sampleUv.y * 2.0);
    vec4 h = vec4(ndc, depth, 1.0) * a.invViewProj;
    return h.xyz / max(h.w, 1e-5);
}

vec2 ProjectUv(vec3 world) {
    vec4 clip = vec4(world, 1.0) * a.viewProj;
    if (clip.w <= 0.05) {
        return vec2(-1.0);
    }
    vec2 ndc = clip.xy / clip.w;
    return vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
}

float ProjectDepth(vec3 world) {
    vec4 clip = vec4(world, 1.0) * a.viewProj;
    if (clip.w <= 0.05) {
        return -1.0;
    }
    return clip.z / clip.w;
}

float SunHit(vec3 world, float yards, int steps) {
    vec3 pos = world + a.sun.xyz * 0.75;
    float stepLen = yards / float(steps);
    for (int i = 0; i < 12; i++) {
        if (i >= steps) {
            break;
        }
        pos += a.sun.xyz * stepLen;
        vec2 suv = ProjectUv(pos);
        if (suv.x <= 0.0 || suv.y <= 0.0 || suv.x >= 1.0 || suv.y >= 1.0) {
            continue;
        }
        float scene = textureLod(depthTex, suv, 0.0).r;
        float ray = ProjectDepth(pos);
        if (ray > 0.0 && scene + 0.0015 < ray) {
            return 0.0;
        }
    }
    return 1.0;
}

float Henyey(float cosTheta, float g) {
    float g2 = g * g;
    return (1.0 - g2) / max(pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5), 1e-3);
}

float ViewDist(float depth) {
    float nearZ = max(a.texel.w, 0.05);
    float farZ = max(a.player.w, nearZ + 1.0);
    return nearZ * farZ / max(farZ - depth * (farZ - nearZ), 0.05);
}

void main() {
    vec3 color = textureLod(colorTex, uv, 0.0).rgb;
    int depthOk = a.texel.z > 0.5 ? 1 : 0;
    float depth = depthOk == 1 ? textureLod(depthTex, uv, 0.0).r : 1.0;
    float viewDist = ViewDist(depth);
    float farZ = max(a.player.w, 80.0);
    // ʕ •ᴥ•ʔ✿ With a near plane of a fifth of a yard, depth is already 0.998 a hundred yards out. Sky is the far clip ✿ ʕ •ᴥ•ʔ
    int sky = depthOk == 1 && viewDist > farZ * 0.92 ? 1 : 0;
    vec3 rel = depthOk == 1 ? WorldAt(uv, depth) : vec3(0.0);
    float nightAmt = a.sun.w;
    float nightScale = mix(1.0, a.night.z, nightAmt);
    float playerZ = a.player.z - a.camera.z;

    if (a.cloud.x > 0.5 && sky == 1) {
        float rayLen = length(rel);
        vec3 ray = rayLen > 1e-3 ? rel / rayLen : vec3(0.0, 0.0, 1.0);
        if (ray.z > 0.05) {
            float t = 2600.0 / ray.z;
            vec2 p = a.camera.xy + ray.xy * t;
            vec2 drift = a.wind.xy * a.wind.z * a.cloud.z;
            float n = Noise(p / 2600.0 + drift * 0.002);
            n = mix(n, Noise(p / 900.0 + drift * 0.004), 1.0 - 0.45);
            float cover = smoothstep(1.0 - a.cloud.y, 1.0 - a.cloud.y + 0.35, n);
            if (a.cloud.w > 0.5) {
                vec2 cell = floor(p / 1400.0);
                float pick = Hash(cell);
                if (pick > 0.82) {
                    vec2 local = (p - (cell + 0.5) * 1400.0) / 700.0;
                    float blob = 1.0 - smoothstep(0.35, 0.85, length(local * vec2(1.0, 0.65)));
                    cover = max(cover, blob * 0.85);
                }
            }
            vec3 cloudColor = mix(a.fogColor.rgb, vec3(1.0, 0.98, 0.95), 0.55);
            float shade = 0.55 + 0.45 * max(dot(a.sun.xyz, vec3(0.0, 0.0, 1.0)), 0.0);
            color = mix(color, cloudColor * shade, cover * 0.8);
        }
    }

    if (a.shadow.x > 0.5 && sky == 0 && depthOk == 1) {
        float worldVis = a.shadow2.x > 0.5 ? SunHit(rel, 50.0, 6) : 1.0;
        float unitVis = a.shadow2.y > 0.5 ? SunHit(rel, 6.0, 3) : 1.0;
        float vis = min(worldVis, unitVis);
        color = mix(color, color * vec3(0.62, 0.7, 0.84), (1.0 - vis) * a.shadow.y * 0.65);
        color = mix(color, color * vec3(1.0, 0.94, 0.82), vis * a.shadow.w * max(1.0 - nightAmt, 0.0));
    }

    if (a.night.x > 0.5 && nightAmt > 0.0) {
        float dark = clamp(a.night.y * nightAmt, 0.0, 0.9);
        color = mix(color, color * vec3(0.55, 0.64, 0.9), dark * 0.7);
        color *= 1.0 - dark;
    }

    if (a.light2.z > 0.5 && sky == 0 && depthOk == 1) {
        float height = rel.z - playerZ;
        float ground = exp(-max(height, 0.0) / max(a.fog.x, 4.0));
        float patchNoise = Noise((rel.xy + a.camera.xy) / 90.0 + a.wind.xy * a.wind.z * 0.04);
        float dens = a.light2.w * ground * mix(1.0, mix(0.7, 1.35, patchNoise), a.fog.z);
        if (height < 0.0) {
            dens *= mix(1.0, min(a.fog.w, 1.8), clamp(-height / 30.0, 0.0, 1.0));
        }
        float along = max(viewDist, length(rel));
        float fogAmt = (1.0 - exp(-dens * along)) * smoothstep(8.0, 40.0, viewDist);
        fogAmt = max(fogAmt, smoothstep(farZ * 0.55, farZ * 0.92, viewDist));
        fogAmt = min(fogAmt, 0.82);
        color = mix(color, a.fogColor.rgb * max(a.fog.y, 0.35), fogAmt);
    }

    if (a.light.x > 0.5 && sky == 0 && depthOk == 1 && a.light.y > 0.0) {
        float rayLen = length(rel);
        if (rayLen > 1.0) {
            vec3 ray = rel / rayLen;
            float march = min(viewDist, a.light.w);
            int steps = a.light2.y > 2.5 ? 16 : (a.light2.y > 1.5 ? 10 : 6);
            float stepLen = march / float(steps);
            float phase = Henyey(dot(ray, a.sun.xyz), a.light2.x);
            float accum = 0.0;
            for (int i = 0; i < 16; i++) {
                if (i >= steps) {
                    break;
                }
                vec3 p = ray * (stepLen * (float(i) + 0.5));
                float vis = SunHit(p, 18.0, 3);
                float h = exp(-max(p.z - playerZ, 0.0) / 50.0);
                accum += vis * h * stepLen;
            }
            color += vec3(1.0, 0.93, 0.8) * accum * a.light.z * phase * a.light.y * nightScale * 0.08;
        }
    }

    if (a.shadow2.z > 0.5 && a.shadow2.w > 0.0) {
        vec3 sunPoint = a.sun.xyz * 400.0;
        vec2 sunUv = ProjectUv(sunPoint);
        float open = 0.0;
        if (sunUv.x > 0.0 && sunUv.y > 0.0 && sunUv.x < 1.0 && sunUv.y < 1.0 && depthOk == 1) {
            open = smoothstep(farZ * 0.75, farZ * 0.92, ViewDist(textureLod(depthTex, sunUv, 0.0).r));
        }
        if (open > 0.02 && sunUv.x > -0.15 && sunUv.y > -0.15 && sunUv.x < 1.15 && sunUv.y < 1.15) {
            vec2 delta = (sunUv - uv) / 12.0;
            float illum = 0.0;
            float decay = 1.0;
            vec2 sampleUv = uv;
            for (int i = 0; i < 12; i++) {
                sampleUv += delta;
                if (sampleUv.x <= 0.0 || sampleUv.y <= 0.0 || sampleUv.x >= 1.0 || sampleUv.y >= 1.0) {
                    break;
                }
                float sampleDist = depthOk == 1 ? ViewDist(textureLod(depthTex, sampleUv, 0.0).r) : farZ;
                float bright = Luma(textureLod(colorTex, sampleUv, 0.0).rgb);
                illum += bright * (sampleDist > farZ * 0.92 ? 1.0 : 0.0) * decay;
                decay *= 0.9;
            }
            color += vec3(1.0, 0.9, 0.75) * illum * a.shadow2.w * nightScale * open * 0.08;
        }
    }

    float lampDay = mix(a.lamp.w, 1.0, nightAmt);
    int lampCount = int(a.lamp.z + 0.5);
    if ((a.lamp.x > 0.5 || a.beacon.x > 0.5) && sky == 0 && depthOk == 1) {
        for (int i = 0; i < 64; i++) {
            if (i >= lampCount) {
                break;
            }
            vec3 lamp = a.lamps[i].posRange.xyz;
            vec3 toLamp = lamp - rel;
            float dist = length(toLamp);
            float range = max(a.lamps[i].posRange.w, 8.0);
            if (a.lamp.x > 0.5 && dist < range) {
                float atten = 1.0 - dist / range;
                color += a.lamps[i].color.rgb * atten * atten * a.lamp.y * lampDay;
            }
            if (a.beacon.x > 0.5 && a.lamps[i].color.w > 0.5 && a.beacon.y > 0.0) {
                float ang = a.wind.z * 0.15;
                vec3 dir = normalize(vec3(cos(ang), sin(ang), -0.04));
                float along = dot(-toLamp, dir);
                vec3 perp = -toLamp - dir * along;
                float beam = exp(-dot(perp, perp) / 18.0) * smoothstep(0.0, 8.0, along) * exp(-max(along, 0.0) / 220.0);
                color += vec3(1.0, 0.9, 0.7) * beam * a.beacon.y * 2.0;
            }
        }
    }

    if (a.grade.x > 0.5) {
        float sat = mix(a.grade.z, a.grade.y, 1.0 - nightAmt);
        float luma = Luma(color);
        color = mix(vec3(luma), color, sat);
    }

    outColor = vec4(max(color, vec3(0.0)), 1.0);
}
