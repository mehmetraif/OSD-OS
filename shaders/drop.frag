#version 440
// Settings → Look → Effects → Transition's Drop: a drop falls in a corner and its ring spreads
// out across the screen, slowing as it goes, the new window inside it and the
// old one outside. Ripples run on behind the ring and settle; its rim and
// their crests catch the light in the scheme's colour. On whole art pixels,
// as the menus are drawn.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float progress;   // 0 the old window, 1 the new
    vec2 resolution;  // the screen, in its pixels
    float px;         // screen pixels to an art pixel
    vec2 corner;      // where it falls: (0, 0) the top left corner, (1, 1) the bottom right
    vec4 ink;         // the colour scheme's colour
};

layout(binding = 1) uniform sampler2D from;
layout(binding = 2) uniform sampler2D to;

void main()
{
    // In art pixels: the screen, this pixel's middle, and the way out from
    // the corner to it.
    vec2 art = resolution / px;
    vec2 cell = floor(qt_TexCoord0 * art) + 0.5;
    vec2 rel = cell - corner * art;
    float d = length(rel);
    vec2 dir = d > 0.0 ? rel / d : vec2(0.0);

    // The ring, out past the far corner by the end (Main.qml eases it out);
    // this point is `behind` art pixels inside it.
    float ring = progress * (length(art) + 4.0);
    float behind = ring - d;
    float settle = 1.0 - progress;

    // Ripples behind the ring, a dozen art pixels apart, weaker the further
    // in (the older); the picture moves along them by their height, up to
    // four art pixels, in whole ones.
    float wave = 0.0;
    if (behind > 0.0)
        wave = sin(behind * 0.5) * exp(-behind / 48.0);
    float shift = floor(wave * settle * 4.0 + 0.5);
    vec2 at = (cell - dir * shift) / art;
    vec4 color = behind > 0.0 ? texture(to, at) : texture(from, at);

    // The rim, an art pixel or two of light with a fainter ring just inside
    // it, and the crests of the ripples.
    float rim = max(1.0 - smoothstep(0.0, 1.5, abs(behind)),
                    0.5 * (1.0 - smoothstep(0.0, 1.0, abs(behind - 5.0))));
    float crest = smoothstep(0.6, 0.9, wave) * 0.6;
    color.rgb = mix(color.rgb, ink.rgb, max(rim, crest) * settle);
    fragColor = color * qt_Opacity;
}
