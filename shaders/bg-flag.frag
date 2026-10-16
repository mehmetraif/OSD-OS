#version 440
// Settings → Background Effect's Checkered Flag: the window a chequered
// racing flag waving in the wind, its waves running along it from the left,
// where a pole would hold it, each fold lit as it lifts and shaded as it
// falls away. Behind the menus, so faint: its light squares in the scheme's
// colour paled, its dark ones the window's ground showing through. In art
// pixels, on the screen's grid, as the Matrix rain is (bg-matrix.frag).

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

void main()
{
    // A square of the flag, in art pixels.
    const float square = 16.0;
    vec2 p = floor(qt_TexCoord0 * size) + origin;

    // The cloth lifted and lowered by two waves running right, a long one
    // and a shorter one slanting across it, a whole art pixel at a time.
    float phase = p.x * 0.05 + p.y * 0.012 - time * 2.6;
    float ripple = p.x * 0.11 - p.y * 0.035 - time * 4.1;
    float lift = 5.0 * sin(phase) + 1.5 * sin(ripple);
    vec2 cloth = vec2(p.x, p.y - floor(lift + 0.5));

    // Lit as it rises, shaded as it falls: the waves' slope, in four shades,
    // one giving way to the next on a checkerboard of art pixels, as the
    // fire's shades do (bg-fire.frag).
    float light = 0.5 + 0.38 * cos(phase) + 0.12 * cos(ripple);
    light += mod(p.x + p.y, 2.0) < 1.0 ? 0.07 : -0.07;
    float shade = clamp(floor(light * 4.0), 0.0, 3.0) / 3.0;

    vec2 cell = floor(cloth / square);
    bool pale = mod(cell.x + cell.y, 2.0) < 1.0;
    float alpha = pale ? 0.07 + 0.2 * shade : 0.05 * shade;
    vec3 color = mix(ink.rgb, vec3(1.0), 0.5);
    fragColor = vec4(color * alpha, alpha) * qt_Opacity;
}
