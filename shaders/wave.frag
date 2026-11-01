#version 440
// Settings → Look → Effects → Transition's Wave: a wave runs out from a corner across the
// screen, the old window swelling up and settling into the new one as it
// passes, smaller the further it goes, until it dies away at the far side.
// On whole art pixels, as the menus are drawn.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float progress;   // 0 the old window, 1 the new
    vec2 resolution;  // the screen, in its pixels
    float px;         // screen pixels to an art pixel
    vec2 corner;      // where it starts: (0, 0) the top left corner, (1, 1) the bottom right
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

    // The wave's middle runs from the corner out past the far one; this
    // point is `behind` art pixels behind it (ahead of it while below 0).
    float reach = length(art);
    float width = 0.2 * reach;
    float behind = progress * (reach + 2.0 * width) - width - d;

    // Its height here: a swell and a trough or two round the middle, lower
    // the further it has run. The picture moves along the wave by that much,
    // up to eight art pixels, in whole ones.
    float envelope = exp(-3.0 * behind * behind / (width * width));
    float phase = behind / width * 9.4247780;
    float strength = envelope * (1.0 - progress);
    float shift = floor(sin(phase) * strength * 8.0 + 0.5);
    vec2 at = (cell - dir * shift) / art;

    // The old window ahead of the middle, the new one behind it; light on
    // the swell's near slope, shade on its far one.
    float t = smoothstep(-0.3 * width, 0.3 * width, behind);
    vec4 color = mix(texture(from, at), texture(to, at), t);
    color.rgb *= 1.0 + 0.3 * cos(phase) * strength;
    fragColor = clamp(color, 0.0, 1.0) * qt_Opacity;
}
