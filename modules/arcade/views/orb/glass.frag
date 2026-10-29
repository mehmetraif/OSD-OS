// X-ray glass: a part's edges, where it turns away from the eye, glow, its
// faces hardly at all, and its light adds to what is behind it (alpha 0, so
// the look's picture adds it to the screen's too), so that parts crossing
// each other light up where they cross. core is the light of a face square
// on, strength and power how bright its edges are and how sharp, fade how
// much of it shows (0 to 1).
VARYING vec3 vNormal;
VARYING vec3 vToEye;
void MAIN()
{
    float facing = abs(dot(normalize(vNormal), normalize(vToEye)));
    float glow = core + strength * pow(1.0 - facing, power);
    FRAGCOLOR = vec4(tint.rgb * glow * fade, 0.0);
}
