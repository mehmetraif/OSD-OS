#version 440
// Settings → Look → Effects → Background Effect's Stars: a starfield drifting past the window,
// the nearer stars faster and brighter, twinkling. In art pixels, on the
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
        // Far, middle and near: each a star or none in every square of it.
        float square = 11.0 - 3.0 * layer;
        vec2 q = p + vec2(floor(time * (1.5 + 3.5 * layer)), 0.0);
        vec2 cell = floor(q / square);
        if (grain(cell + layer * 31.0) > 0.45 - 0.1 * layer)
            continue;
        vec2 star = floor(vec2(grain(cell * 1.3 + 7.0), grain(cell * 0.7 + 3.0)) * square);
        if (all(equal(floor(q - cell * square), star))) {
            float twinkle = 0.5 + 0.5 * sin(time * (2.0 + 3.0 * grain(cell)) + grain(cell + 5.0) * 6.28);
            light = max(light, 0.35 + 0.2 * layer + 0.3 * twinkle);
        }
    }
    vec3 color = mix(ink.rgb, vec3(1.0), 0.3);
    fragColor = vec4(color * light, light) * qt_Opacity;
}
