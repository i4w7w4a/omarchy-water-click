#version 300 es
// SPDX-License-Identifier: MIT
// Water refraction adapted from HyprRipple, Copyright (c) 2026 jgarza.
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

void main() {
    vec2 rel = gl_FragCoord.xy - center;
    float distanceToCenter = length(rel);
    vec2 direction = rel / max(distanceToCenter, 0.0001);
    float wobble = (valueNoise(direction * 1.6 + time * 0.6) - 0.5) * 0.04 * radius;
    float width = max(0.10 * radius, 8.0 * scale);
    float band = (distanceToCenter - radius - wobble) / width;
    if (abs(band) > 2.5)
        discard;

    float envelope = exp(-band * band);
    float shimmer = (valueNoise(rel / (7.0 * scale) + vec2(time * 1.7, -time * 1.3)) - 0.5) * 0.25 * envelope;
    float swell = -band * envelope * 2.3 + shimmer;
    vec2 offset = direction * waveAmp * swell;
    vec2 sampleAt = clamp(gl_FragCoord.xy - offset - sampleOrigin, vec2(0.5), sampleSize - 0.5);
    vec3 color = texture(scene, sampleAt / vec2(textureSize(scene, 0))).rgb;
    float crest = max(swell, 0.0);
    float alpha = min(envelope * 1.5, 1.0) * opacity;
    fragColor = vec4((color + vec3(crest * crest * 0.02)) * alpha, alpha);
}
