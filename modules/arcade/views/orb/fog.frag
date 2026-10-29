// A layer of fog behind the Orb look's panels (Settings → Orb Fog's Picture,
// fogLive.frag its Live): one of the fog picture's noises (fog.png's red),
// its shape bent by the other two, drifting on a slow circle of radius
// (phase, 0 to 1, once round), as thick as strength, and thinning out towards
// the layer's edge. Its light adds to what is behind.
VARYING vec2 vUV;
void MAIN()
{
    vec2 drift = radius * vec2(cos(6.2831853 * phase), sin(6.2831853 * phase));
    vec2 uv = vUV * scale + vec2(seed);
    vec2 bend = texture(fogNoise, uv * 0.5 - drift * 0.5).gb - vec2(0.5);
    float f = texture(fogNoise, uv + drift + bend * 0.5).r;
    float edge = 1.0 - smoothstep(0.3, 1.0, length(vUV - vec2(0.5)) * 2.0);
    float fog = smoothstep(0.30, 0.88, f) * edge;
    FRAGCOLOR = vec4(tint.rgb * fog * strength, 0.0);
}
