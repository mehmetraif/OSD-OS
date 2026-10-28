// The game's picture inside the orb, a 4:3 screen as high as the orb's disc
// (OrbLook's screen): round, its edge fading into the glass's tint. As the
// game starts the disc opens out (open, 0 to 1) to the whole screen, its
// growing edge fading into what is behind it; as much of it shows as fade
// says (0 to 1). Premultiplied, as the look's picture is.
VARYING vec2 vUV;
void MAIN()
{
    // Round the screen's middle, in its heights.
    vec2 p = (vUV - vec2(0.5)) * vec2(4.0 / 3.0, 1.0);
    float d = length(p) * 2.0;
    float r = mix(1.0, 1.9, open);
    if (d > r)
        discard;
    // The picture's top at the quad's (UV0's v runs up, a picture's rows down).
    vec3 c = texture(picture, vec2(vUV.x, 1.0 - vUV.y)).rgb;
    float rim = 1.0 - smoothstep(r - 0.2, r, d);
    vec3 shown = mix(tint.rgb * 0.2, c + tint.rgb * 0.08 * (1.0 - open), mix(rim, 1.0, open));
    float alpha = mix(1.0, rim, smoothstep(0.0, 0.15, open)) * fade;
    FRAGCOLOR = vec4(shown * alpha, alpha);
}
