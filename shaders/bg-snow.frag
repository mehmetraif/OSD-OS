#version 440
// Settings → Look → Effects → Background Effect's Snow: flakes falling down the window, swaying
// as they fall, the nearer ones bigger and faster. In art pixels, on the
// screen's grid, as the Matrix rain is (bg-matrix.frag).

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 size;
    vec2 origin;
    float time;
    vec4 ink;
    vec4 paper;
};

float grain(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main()
{
    vec2 p = floor(qt_TexCoord0 * size) + origin;
    float light = 0.0;
    for (int i = 0; i < 3; ++i) {
        float layer = float(i);
        float square = 13.0 - 3.0 * layer;
        float column = floor(p.x / square);
        // Each column of squares falls at its own pace, and sways.
        float fall = time * (4.0 + 4.0 * layer + 3.0 * grain(vec2(column, layer)));
        float sway = sin(time * (0.9 + 0.5 * layer) + grain(vec2(column, 3.0)) * 6.28) * (1.0 + layer);
        vec2 q = p - vec2(floor(sway), floor(fall));
        vec2 cell = floor(q / square);
        if (grain(cell + layer * 17.0) > 0.22 + 0.06 * layer)
            continue;
        vec2 flake = floor(vec2(grain(cell * 1.9 + 2.0), grain(cell * 0.3 + 8.0)) * (square - 2.0));
        vec2 d = abs(floor(q - cell * square) - flake);
        // Far ones a dot, near ones a little cross.
        if ((layer < 2.0 && d.x + d.y < 0.5) || (layer >= 2.0 && d.x + d.y < 1.5 && min(d.x, d.y) < 0.5))
            light = max(light, 0.45 + 0.25 * layer);
    }
    vec3 color = mix(ink.rgb, vec3(1.0), 0.5);
    fragColor = vec4(color * light, light) * qt_Opacity;
}
