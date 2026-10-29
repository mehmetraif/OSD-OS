// The Orb look's glass (glass.frag): each point's normal and the way to the
// eye, for how square on it faces the camera.
VARYING vec3 vNormal;
VARYING vec3 vToEye;
void MAIN()
{
    vec4 world = MODEL_MATRIX * vec4(VERTEX, 1.0);
    vNormal = normalize(NORMAL_MATRIX * NORMAL);
    vToEye = CAMERA_POSITION - world.xyz;
    POSITION = MODELVIEWPROJECTION_MATRIX * vec4(VERTEX, 1.0);
}
