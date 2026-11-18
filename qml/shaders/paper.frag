#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 paperColor;
    vec4 grainColor;
    vec2 resolution;
    float grainIntensity;
    float vignetteIntensity;
    float shadowIntensity;
    float cornerRadius;
} ubuf;

// High-frequency pseudo-random hash
float hash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// 2D Value Noise with Hermite interpolation
float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);

    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));

    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// Multiscale procedural paper fiber structure (FBM)
float paperGrain(vec2 uv) {
    float n1 = noise(uv * 1.5);
    float n2 = noise(vec2(uv.x * 3.0, uv.y * 0.75));
    float n3 = hash(uv);
    return (n1 * 0.5 + n2 * 0.35 + n3 * 0.15);
}

// Exact distance to rounded rectangle with corner radius r
float roundedBoxSDF(vec2 centerPos, vec2 halfSize, float r) {
    vec2 q = abs(centerPos) - halfSize + vec2(r);
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

void main() {
    vec2 pixelPos = qt_TexCoord0 * ubuf.resolution;
    vec2 halfSize = ubuf.resolution * 0.5;
    vec2 centerPos = pixelPos - halfSize;

    // Smoothly anti-aliased rounded corners directly in the shader
    float alpha = 1.0;
    if (ubuf.cornerRadius > 0.5) {
        float dist = roundedBoxSDF(centerPos, halfSize, ubuf.cornerRadius);
        // Antialias over 1.5 pixels
        alpha = clamp(0.5 - dist, 0.0, 1.0);
        if (alpha <= 0.0) {
            discard;
        }
    }

    // 1. Procedural Grain & Pulp fibers
    float fiber = paperGrain(pixelPos * 0.85);
    float fiberOffset = (fiber - 0.5) * ubuf.grainIntensity;

    // 2. Subtle organic vignette (aging/depth)
    vec2 uv = qt_TexCoord0;
    vec2 edgeDist = uv * (1.0 - uv);
    float vignette = edgeDist.x * edgeDist.y * 16.0;
    vignette = clamp(pow(vignette, 0.18), 0.0, 1.0);
    float edgeShadow = mix(1.0 - ubuf.vignetteIntensity, 1.0, vignette);

    // 3. Combine base paper tint with fibers and edge depth
    vec3 base = ubuf.paperColor.rgb;
    vec3 grain = ubuf.grainColor.rgb;

    vec3 col = mix(base, grain, fiberOffset * 2.0);
    col *= edgeShadow;

    fragColor = vec4(col, ubuf.paperColor.a * alpha) * ubuf.qt_Opacity;
}
