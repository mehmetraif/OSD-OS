#version 440
// Settings → Look → Effects → Background Effect's Matrix: rain of glyphs down the window, each
// column at its own speed, the newest glyph bright, those behind it fading.
// Drawn in art pixels (Main.qml draws it at that size and scales it up), on
// the screen's grid: every ground (OsdGround) under a dialog draws the same
// rain as the window's, the one carrying on in the other.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 size;    // the area, in art pixels
    vec2 origin;  // its top left on the screen, in art pixels
    float time;   // seconds
    vec4 ink;     // the colour scheme's colour
    vec4 paper;   // its background
};

float grain(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main()
{
    // A glyph's cell: 5 by 7 art pixels and one apart.
    const vec2 cellSize = vec2(6.0, 8.0);
    vec2 p = floor(qt_TexCoord0 * size) + origin;
    vec2 cell = floor(p / cellSize);
    vec2 inCell = p - cell * cellSize;
    fragColor = vec4(0.0);
    if (inCell.x >= 5.0 || inCell.y >= 7.0)
        return;

    float column = cell.x;
    float rows = ceil((origin.y + size.y) / cellSize.y) + 2.0;
    float speed = 5.0 + 9.0 * grain(vec2(column, 1.7));
    float trail = 7.0 + 15.0 * grain(vec2(column, 9.3));
    float span = rows + trail;
    float head = mod(time * speed + grain(vec2(column, 4.1)) * span, span);
    float behind = head - cell.y;
    if (behind < 0.0 || behind > trail)
        return;

    // Each cell's glyph changes now and then, some faster than others; a
    // glyph is mirrored left to right, as many a character is.
    float rate = 0.4 + 3.0 * grain(cell + 0.5);
    float glyph = floor(grain(cell * 1.37) * 64.0 + time * rate);
    vec2 bit = vec2(min(inCell.x, 4.0 - inCell.x), inCell.y);
    if (grain(vec2(glyph * 7.0 + bit.x, glyph * 3.0 + bit.y * 5.0)) < 0.5)
        return;

    // Behind the menus' own lines, so dimmer than they are: the newest glyph
    // brightest, then fading.
    float level = 1.0 - behind / trail;
    vec3 color = behind < 1.0 ? mix(ink.rgb, vec3(1.0), 0.5) : ink.rgb;
    float alpha = behind < 1.0 ? 0.7 : 0.08 + 0.42 * level * level;
    fragColor = vec4(color * alpha, alpha) * qt_Opacity;
}
