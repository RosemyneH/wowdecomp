#version 450

layout(set = 0, binding = 0) uniform sampler2D colorTex;

layout(push_constant) uniform Push {
    vec4 params;
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

float Luma(vec3 color) {
    return dot(color, vec3(0.299, 0.587, 0.114));
}

void main() {
    vec2 texel = pc.params.xy;
    vec3 center = texture(colorTex, uv).rgb;
    vec3 north = texture(colorTex, uv + vec2(0.0, -texel.y)).rgb;
    vec3 south = texture(colorTex, uv + vec2(0.0, texel.y)).rgb;
    vec3 east = texture(colorTex, uv + vec2(texel.x, 0.0)).rgb;
    vec3 west = texture(colorTex, uv + vec2(-texel.x, 0.0)).rgb;

    float luma = Luma(center);
    float lumaNorth = Luma(north);
    float lumaSouth = Luma(south);
    float lumaEast = Luma(east);
    float lumaWest = Luma(west);
    float lumaMin = min(luma, min(min(lumaNorth, lumaSouth), min(lumaEast, lumaWest)));
    float lumaMax = max(luma, max(max(lumaNorth, lumaSouth), max(lumaEast, lumaWest)));
    float contrast = lumaMax - lumaMin;
    vec3 color = center;

    // ʕ •ᴥ•ʔ✿ A vertical luma step is a horizontal edge, so the blur runs along that edge ✿ ʕ •ᴥ•ʔ
    if (pc.params.z > 0.0 && contrast > max(0.0312, lumaMax * 0.125)) {
        float vertical = abs(lumaNorth + lumaSouth - 2.0 * luma);
        float horizontal = abs(lumaEast + lumaWest - 2.0 * luma);
        vec2 direction = vertical >= horizontal ? vec2(texel.x, 0.0) : vec2(0.0, texel.y);
        vec3 blur = 0.5 * (texture(colorTex, uv + direction).rgb + texture(colorTex, uv - direction).rgb);
        float blend = clamp(contrast / max(lumaMax, 1e-4), 0.0, 0.75);
        color = mix(center, blur, blend);
    }

    if (pc.params.w > 0.0) {
        vec3 blur = (north + south + east + west) * 0.25;
        color = clamp(color + (color - blur) * pc.params.w, 0.0, 1.0);
    }

    outColor = vec4(color, 1.0);
}
