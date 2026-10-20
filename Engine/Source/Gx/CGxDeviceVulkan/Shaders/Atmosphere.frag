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

void main() {
    vec3 color = textureLod(colorTex, uv, 0.0).rgb;
    float depth = a.texel.z > 0.5 ? textureLod(depthTex, uv, 0.0).r : 1.0;
    int sky = a.texel.z > 0.5 && depth > 0.998 ? 1 : 0;
    vec3 world = a.texel.z > 0.5 ? WorldAt(uv, depth) : a.camera.xyz;
    float nightAmt = a.sun.w;
    float dayAmt = 1.0 - nightAmt;
    float nightScale = mix(1.0, a.night.z, nightAmt);

    if (a.cloud.x > 0.5 && sky == 1) {
        vec3 ray = world - a.camera.xyz;
        float rayLen = length(ray);
        ray = rayLen > 1e-3 ? ray / rayLen : vec3(0.0, 0.0, 1.0);
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

    if (a.shadow.x > 0.5 && sky == 0 && a.texel.z > 0.5) {
        float vis = 1.0;
        int taps = a.shadow.z > 1.25 ? 3 : 1;
        for (int tap = 0; tap < 3; tap++) {
            if (tap >= taps) {
                break;
            }
            vec3 samplePos = world + vec3(float(tap - 1) * 0.35, 0.0, 0.0);
            float worldVis = a.shadow2.x > 0.5 ? SunHit(samplePos, 70.0, 8) : 1.0;
            float unitVis = a.shadow2.y > 0.5 ? SunHit(samplePos, 8.0, 4) : 1.0;
            vis = min(vis, min(worldVis, unitVis));
        }
        vec3 cool = vec3(0.49, 0.58, 0.78);
        vec3 warm = vec3(1.0, 0.9, 0.75);
        color = mix(color, color * cool, (1.0 - vis) * a.shadow.y);
        color = mix(color, color * warm, vis * a.shadow.w * dayAmt);
    }

    if (a.night.x > 0.5 && nightAmt > 0.0) {
        float dark = a.night.y * nightAmt;
        dark *= mix(1.0, 1.0 + a.night.w * 0.3, nightAmt);
        vec3 moon = vec3(0.61, 0.72, 1.0);
        color = mix(color, color * moon, dark * 0.65);
        color *= 1.0 - dark;
    }

    if (a.light2.z > 0.5 && sky == 0 && a.texel.z > 0.5) {
        float dist = length(world - a.camera.xyz);
        float height = world.z - a.player.z;
        float dens = a.light2.w * exp(-max(height, 0.0) / a.fog.x);
        float patchNoise = Noise(world.xy / 60.0 + a.wind.xy * a.wind.z * 0.15);
        dens *= mix(1.0, mix(0.35, 1.6, patchNoise), a.fog.z);
        if (height < 0.0) {
            dens *= mix(1.0, a.fog.w, clamp(-height / 25.0, 0.0, 1.0));
        }
        float reach = 1.0 - smoothstep(a.light.w * 0.6, a.light.w, dist);
        float fogAmt = (1.0 - exp(-dens * dist)) * reach;
        vec3 fogLit = a.fogColor.rgb * a.fog.y;
        float toward = max(dot(normalize(world - a.camera.xyz), a.sun.xyz), 0.0);
        fogLit += vec3(1.0, 0.9, 0.75) * toward * a.light.y * dayAmt;
        color = mix(color, fogLit, clamp(fogAmt, 0.0, 0.85));
    }

    if (a.light.x > 0.5 && a.texel.z > 0.5 && a.light.y > 0.0) {
        vec3 ray = world - a.camera.xyz;
        float rayLen = length(ray);
        if (rayLen > 0.5) {
            ray /= rayLen;
            float march = min(rayLen, a.light.w);
            int steps = a.light2.y > 2.5 ? 24 : (a.light2.y > 1.5 ? 16 : 8);
            float stepLen = march / float(steps);
            float cosTheta = dot(ray, a.sun.xyz);
            float phase = Henyey(cosTheta, a.light2.x);
            float accum = 0.0;
            for (int i = 0; i < 24; i++) {
                if (i >= steps) {
                    break;
                }
                vec3 p = a.camera.xyz + ray * (stepLen * (float(i) + 0.5));
                float vis = SunHit(p, 24.0, 4);
                float h = exp(-max(p.z - a.player.z, 0.0) / 40.0);
                accum += vis * h * stepLen;
            }
            vec3 shaft = vec3(1.0, 0.9, 0.75) * accum * a.light.z * phase * a.light.y * nightScale * 0.35;
            color += shaft;
        }
    }

    if (a.shadow2.z > 0.5 && a.shadow2.w > 0.0) {
        vec3 sunPoint = a.camera.xyz + a.sun.xyz * 400.0;
        vec2 sunUv = ProjectUv(sunPoint);
        float sunDepth = ProjectDepth(sunPoint);
        float covered = 0.0;
        if (sunUv.x > 0.0 && sunUv.y > 0.0 && sunUv.x < 1.0 && sunUv.y < 1.0 && sunDepth > 0.0 && a.texel.z > 0.5) {
            float atSun = textureLod(depthTex, sunUv, 0.0).r;
            covered = atSun < 0.998 ? 1.0 : 0.0;
        }
        if (sunUv.x > -0.2 && sunUv.y > -0.2 && sunUv.x < 1.2 && sunUv.y < 1.2 && covered < 0.5) {
            vec2 delta = (sunUv - uv) / 12.0;
            float illum = 0.0;
            float decay = 1.0;
            vec2 sampleUv = uv;
            for (int i = 0; i < 12; i++) {
                sampleUv += delta;
                if (sampleUv.x <= 0.0 || sampleUv.y <= 0.0 || sampleUv.x >= 1.0 || sampleUv.y >= 1.0) {
                    break;
                }
                float sampleDepth = a.texel.z > 0.5 ? textureLod(depthTex, sampleUv, 0.0).r : 1.0;
                float bright = Luma(textureLod(colorTex, sampleUv, 0.0).rgb);
                float skyCast = sampleDepth > 0.998 ? 1.0 : 0.0;
                illum += bright * skyCast * decay;
                decay *= 0.9;
            }
            color += vec3(1.0, 0.9, 0.75) * illum * a.shadow2.w * nightScale * 0.08;
        }
    }

    float lampDay = mix(a.lamp.w, 1.0, nightAmt);
    int lampCount = int(a.lamp.z + 0.5);
    if ((a.lamp.x > 0.5 || a.beacon.x > 0.5) && sky == 0) {
        for (int i = 0; i < 64; i++) {
            if (i >= lampCount) {
                break;
            }
            vec3 toLamp = a.lamps[i].posRange.xyz - world;
            float dist = length(toLamp);
            float range = max(a.lamps[i].posRange.w, 1.0);
            if (a.lamp.x > 0.5 && dist < range) {
                float atten = 1.0 - dist / range;
                atten *= atten;
                color += a.lamps[i].color.rgb * atten * a.lamp.y * lampDay;
            }
            if (a.beacon.x > 0.5 && a.lamps[i].color.w > 0.5 && a.beacon.y > 0.0) {
                float ang = a.wind.z * 0.52;
                vec3 dir = normalize(vec3(cos(ang), sin(ang), -0.04));
                float along = dot(-toLamp, dir);
                vec3 perp = -toLamp - dir * along;
                float beam = exp(-dot(perp, perp) / 8.0) * smoothstep(0.0, 12.0, along) * exp(-max(along - 20.0, 0.0) / 180.0);
                color += vec3(1.0, 0.89, 0.66) * beam * a.beacon.y * mix(a.lamp.w, 1.0, nightAmt);
            }
        }
    }

    if (a.grade.x > 0.5) {
        float sat = mix(a.grade.z, a.grade.y, dayAmt);
        float luma = Luma(color);
        color = mix(vec3(luma), color, sat);
    }

    outColor = vec4(max(color, vec3(0.0)), 1.0);
}
