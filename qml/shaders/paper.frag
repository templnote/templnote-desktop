#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D wallpaperSource;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 paperColor;
    vec4 grainColor;
    vec4 gradientColor1;
    vec4 gradientColor2;
    vec2 resolution;
    float grainIntensity;
    float vignetteIntensity;
    float shadowIntensity;
    float cornerRadius;
    int bgMode; // 0 = Solid Theme Color, 1 = Gradient, 2 = Wallpaper Image
    float wallpaperDim; // 0.0 to 1.0 dimming / blending toward paperColor
    float gradientAngle; // angle in degrees
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

// Soft-Light blending function for realistic paper ink/fiber absorption
float blendSoftLightComponent(float base, float blend) {
    return (blend < 0.5) 
        ? (2.0 * base * blend + base * base * (1.0 - 2.0 * blend)) 
        : (sqrt(base) * (2.0 * blend - 1.0) + 2.0 * base * (1.0 - blend));
}

vec3 blendSoftLight(vec3 base, vec3 blend) {
    return vec3(
        blendSoftLightComponent(base.r, blend.r),
        blendSoftLightComponent(base.g, blend.g),
        blendSoftLightComponent(base.b, blend.b)
    );
}

void main() {
    vec2 pixelPos = qt_TexCoord0 * ubuf.resolution;
    vec2 halfSize = ubuf.resolution * 0.5;
    vec2 centerPos = pixelPos - halfSize;

    // Smoothly anti-aliased rounded corners directly in the shader
    float alpha = 1.0;
    if (ubuf.cornerRadius > 0.5) {
        float dist = roundedBoxSDF(centerPos, halfSize, ubuf.cornerRadius);
        alpha = clamp(0.5 - dist, 0.0, 1.0);
        if (alpha <= 0.0) {
            discard;
        }
    }

    // 1. Determine base substrate color (Solid, Gradient, or Wallpaper Texture)
    vec3 baseSubstrate = ubuf.paperColor.rgb;

    if (ubuf.bgMode == 1) {
        // Linear gradient at specified angle
        float rad = radians(ubuf.gradientAngle);
        vec2 dir = vec2(cos(rad), sin(rad));
        float t = clamp(dot(qt_TexCoord0 - vec2(0.5), dir) + 0.5, 0.0, 1.0);
        vec3 grad = mix(ubuf.gradientColor1.rgb, ubuf.gradientColor2.rgb, t);
        baseSubstrate = mix(grad, ubuf.paperColor.rgb, ubuf.wallpaperDim);
    } else if (ubuf.bgMode == 2) {
        // Custom wallpaper texture
        vec4 img = texture(wallpaperSource, qt_TexCoord0);
        // Dim / blend toward paper color to ensure text legibility
        baseSubstrate = mix(img.rgb, ubuf.paperColor.rgb, clamp(ubuf.wallpaperDim, 0.0, 0.95));
    }

    // 2. Procedural Grain & Pulp fibers
    float fiber = paperGrain(pixelPos * 0.85);
    // Normalized fiber tone centered around 0.5 for Soft-Light blending
    float fiberBlendVal = clamp(0.5 + (fiber - 0.5) * (ubuf.grainIntensity * 6.0), 0.0, 1.0);

    // Apply Soft-Light substrate blend
    vec3 col = blendSoftLight(baseSubstrate, vec3(fiberBlendVal));

    // 3. Subtle organic vignette (aging/depth around paper boundaries)
    vec2 uv = qt_TexCoord0;
    vec2 edgeDist = uv * (1.0 - uv);
    float vignette = edgeDist.x * edgeDist.y * 16.0;
    vignette = clamp(pow(vignette, 0.18), 0.0, 1.0);
    float edgeShadow = mix(1.0 - ubuf.vignetteIntensity, 1.0, vignette);
    col *= edgeShadow;

    fragColor = vec4(col, ubuf.paperColor.a * alpha) * ubuf.qt_Opacity;
}
