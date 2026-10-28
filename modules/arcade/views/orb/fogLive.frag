// Settings → Orb Fog's Live: a layer of fog behind the Orb look's panels
// worked out for every pixel as it drifts, as in the look's first design,
// where fog.frag reads its picture (fog.png). Value noise in four octaves
// (fbm), bent by two more, drifting on a circle of radius (phase, 0 to 1,
// once round), as thick as strength, and thinning out towards the layer's
// edge. Its light adds to what is behind. (Not "noise" for a name: the
// shaders' compiler takes that one.)
VARYING vec2 vUV;

float fogHash(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float fogValue(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 w = f * f * (3.0 - 2.0 * f);
    return mix(mix(fogHash(i), fogHash(i + vec2(1.0, 0.0)), w.x),
               mix(fogHash(i + vec2(0.0, 1.0)), fogHash(i + vec2(1.0, 1.0)), w.x), w.y);
}

float fogFbm(vec2 p)
{
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 4; ++i) {
        v += a * fogValue(p);
        p = p * 2.02 + vec2(17.1, 3.7);
        a *= 0.5;
    }
    return v;
}

void MAIN()
{
    vec2 drift = radius * vec2(cos(6.2831853 * phase), sin(6.2831853 * phase));
    vec2 p = vUV * scale + vec2(seed, seed * 0.37) + drift;
    vec2 bend = vec2(fogFbm(p * 0.6 + drift * 0.5), fogFbm(p * 0.6 + vec2(5.2, 1.3) - drift * 0.5));
    float f = fogFbm(p + 1.8 * bend);
    float edge = smoothstep(1.0, 0.3, length(vUV - vec2(0.5)) * 2.0);
    float fog = smoothstep(0.30, 0.88, f) * edge;
    FRAGCOLOR = vec4(tint.rgb * fog * strength, 0.0);
}
