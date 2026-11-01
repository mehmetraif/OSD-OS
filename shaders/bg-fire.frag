#version 440
// Settings → Look → Effects → Background Effect's Fire: pixel flames burning up from the
// window's foot, inside its frame, in a fire's colours and dithered where one
// shade meets the next. In art pixels, on the screen's grid, as the
// Matrix rain is (bg-matrix.frag).

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 size;    // the area, in art pixels: the window inside its frame
    vec2 origin;  // its top left on the screen, in art pixels
    float time;   // seconds
    vec4 ink;
    vec4 paper;
};

float grain(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float noise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(grain(i), grain(i + vec2(1.0, 0.0)), u.x),
               mix(grain(i + vec2(0.0, 1.0)), grain(i + vec2(1.0, 1.0)), u.x), u.y);
}

float turbulence(vec2 p)
{
    float sum = 0.0;
    float amount = 0.5;
    for (int i = 0; i < 4; ++i) {
        sum += amount * noise(p);
        p = p * 2.03 + vec2(1.7, 9.2);
        amount *= 0.5;
    }
    return sum;
}

void main()
{
    vec2 p = floor(qt_TexCoord0 * size) + origin;
    // Art pixels up from the area's foot; the flames reach about half way.
    float up = origin.y + size.y - p.y;
    float reach = max(size.y * 0.36, 12.0);
    float heat = turbulence(vec2(p.x * 0.09, up * 0.075 - time * 1.9)) * 0.9
               + turbulence(vec2(p.x * 0.23 + 5.0, up * 0.16 - time * 3.3)) * 0.35
               + 0.08 - up / reach;
    // A checkerboard nudge, so that each shade gives way to the next dithered.
    heat += mod(p.x + p.y, 2.0) < 1.0 ? 0.025 : -0.025;
    fragColor = vec4(0.0);
    if (heat < 0.0)
        return;
    vec3 color;
    float alpha = 1.0;
    if (heat < 0.1)       { color = vec3(0.45, 0.04, 0.0); alpha = 0.8; }
    else if (heat < 0.22) { color = vec3(0.78, 0.12, 0.0); }
    else if (heat < 0.36) { color = vec3(1.0, 0.42, 0.0); }
    else if (heat < 0.5)  { color = vec3(1.0, 0.68, 0.06); }
    else if (heat < 0.64) { color = vec3(1.0, 0.9, 0.3); }
    else                  { color = vec3(1.0, 1.0, 0.8); }
    fragColor = vec4(color * alpha, alpha) * qt_Opacity;
}
