#version 300 es
// SPDX-License-Identifier: MIT
// Effects adapted from HyprRipple, Copyright (c) 2026 jgarza.
precision highp float;

layout(location = 0) out vec4 fragColor;

uniform sampler2D scene;
uniform vec2 sampleOrigin;
uniform vec2 sampleSize;
uniform vec2 center;
uniform float scale;
uniform float radius;
uniform float opacity;
uniform float waveAmp;
uniform float time;
uniform int mode;
uniform float fade;

// Must match EEffect in Effect.hpp.
const int CLASSIC = 0;
const int RING = 1;
const int SPARKLE = 2;
const int NEGATIVE = 3;
const int PRISM = 5;

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1, 0)), f.x),
               mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), f.x), f.y);
}

float disk(float distanceToCenter, float edge) {
    return 1.0 - smoothstep(radius - edge, radius + edge, distanceToCenter);
}

float sparkleInk(vec2 rel, float distanceToCenter) {
    vec2 direction = rel / max(distanceToCenter, 0.0001);
    float wobble = (valueNoise(direction * 2.2 + time * 0.9) - 0.5) * 0.10 * radius;
    float r = max(radius + wobble, 1.0);
    float body = 1.0 - smoothstep(0.65 * r, r, distanceToCenter);
    float glow = body * mix(1.0, 0.55, clamp(distanceToCenter / r, 0.0, 1.0));

    // The light flickers in 5 logical pixel cells to remain visible in video.
    vec2 grain = rel / (5.0 * scale);
    vec2 cell = floor(grain);
    float cellTime = floor(time * 24.0 + hash12(cell) * 7.0);
    float twinkle = hash12(cell + cellTime * 0.731);
    float dotMask = 1.0 - smoothstep(0.18, 0.5, length(fract(grain) - 0.5));
    float front = 1.0 - smoothstep(0.0, 0.42 * r, abs(distanceToCenter - r * 0.82));
    float sparks = step(mix(0.91, 0.70, front), twinkle) * dotMask * body;
    sparks *= (1.0 - smoothstep(0.0, 0.65, fade)) * (0.35 + 0.65 * front);
    return glow * 0.55 + sparks * 2.0;
}

vec3 backgroundAt(vec2 pixel) {
    vec2 sampleAt = clamp(pixel - sampleOrigin, vec2(0.5), sampleSize - 0.5);
    return texture(scene, sampleAt / vec2(textureSize(scene, 0))).rgb;
}

vec3 spectrum(float hue) {
    return clamp(abs(mod(hue * 6.0 + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0, 0.0, 1.0);
}

void main() {
    vec2 pixel = gl_FragCoord.xy;
    vec2 rel = pixel - center;
    float distanceToCenter = length(rel);
    vec3 color;
    float mask;

    if (mode == CLASSIC) {
        float edge = clamp(distanceToCenter / max(radius, 1.0), 0.0, 1.0);
        mask = disk(distanceToCenter, 4.0 * scale) * mix(0.72, 1.0, edge * edge);
        color = vec3(1.0, 0.72, 0.24);
    } else if (mode == RING) {
        float edge = 2.0 * scale;
        float inner = radius - 7.0 * scale;
        mask = disk(distanceToCenter, edge) * smoothstep(inner - edge, inner + edge, distanceToCenter);
        color = vec3(0.27, 0.86, 1.0);
    } else if (mode == SPARKLE) {
        mask = sparkleInk(rel, distanceToCenter);
        color = vec3(0.76, 0.68, 1.0);
    } else if (mode == NEGATIVE) {
        mask = disk(distanceToCenter, 4.0 * scale);
        if (mask <= 0.001)
            discard;
        color = vec3(1.0) - clamp(backgroundAt(pixel), 0.0, 1.0);
    } else if (mode == PRISM) {
        float width = max(0.22 * radius, 14.0 * scale);
        float band = (distanceToCenter - radius) / width;
        if (abs(band) > 2.5)
            discard;
        float envelope = exp(-band * band);
        vec2 direction = rel / max(distanceToCenter, 0.0001);
        vec2 offset = direction * waveAmp * (-2.3 * band * envelope);
        color = vec3(backgroundAt(pixel - offset).r,
                     backgroundAt(pixel - 1.5 * offset).g,
                     backgroundAt(pixel - 2.0 * offset).b);
        float angle = atan(rel.y, rel.x) / 6.2831853;
        color += spectrum(fract(angle + band * 0.25 + time * 0.15)) * envelope * 0.37;
        mask = min(envelope * 1.4, 1.0);
    } else {
        discard;
    }

    float alpha = clamp(mask * opacity, 0.0, 1.0);
    if (alpha <= 0.001)
        discard;
    fragColor = vec4(clamp(color, 0.0, 1.0) * alpha, alpha);
}
