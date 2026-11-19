#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D source;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float topFadeSize;    // Normalized height (0.0 to 0.5) of top fade zone
    float bottomFadeSize; // Normalized height (0.0 to 0.5) of bottom fade zone
    float topAlpha;       // 0.0 (fully faded when scrolled) to 1.0 (no fade when at top)
    float bottomAlpha;    // 0.0 (fully faded when more content) to 1.0 (no fade when at end)
} ubuf;

void main() {
    vec4 tex = texture(source, qt_TexCoord0);
    float factor = 1.0;

    // Top edge fade
    if (ubuf.topFadeSize > 0.0001 && qt_TexCoord0.y < ubuf.topFadeSize) {
        float edge = smoothstep(0.0, ubuf.topFadeSize, qt_TexCoord0.y);
        factor *= mix(ubuf.topAlpha, 1.0, edge);
    }

    // Bottom edge fade
    if (ubuf.bottomFadeSize > 0.0001 && qt_TexCoord0.y > (1.0 - ubuf.bottomFadeSize)) {
        float edge = smoothstep(1.0, 1.0 - ubuf.bottomFadeSize, qt_TexCoord0.y);
        factor *= mix(ubuf.bottomAlpha, 1.0, edge);
    }

    fragColor = tex * (factor * ubuf.qt_Opacity);
}
