#version 440
// Settings → Look → Effects → Transition's Ripple: one window gives way to the next in waves,
// the old one rippling out as the new one ripples in, the waves strongest half
// way and on whole art pixels.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float progress;   // 0 the old window, 1 the new
    vec2 resolution;  // the screen, in its pixels
    float px;         // screen pixels to an art pixel
};

layout(binding = 1) uniform sampler2D from;
layout(binding = 2) uniform sampler2D to;

void main()
{
    vec2 uv = qt_TexCoord0;
    float strength = sin(progress * 3.14159265);
    float row = floor(uv.y * resolution.y / px);
    float wave = (sin(row * 0.33 + progress * 19.0) * 0.045 + sin(row * 0.09 - progress * 8.0) * 0.025) * strength;
    wave = floor(wave * resolution.x / px + 0.5) * px / resolution.x;
    vec4 before = texture(from, uv + vec2(wave, 0.0));
    vec4 after = texture(to, uv - vec2(wave, 0.0));
    fragColor = mix(before, after, smoothstep(0.3, 0.7, progress)) * qt_Opacity;
}
