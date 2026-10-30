// The app's own shaders (shaders/*.frag, which scripts/build-site.py copies
// into the page) as WebGL 2 takes them. Qt Quick's are GLSL 4.40 for qsb:
// their uniforms in one block, beginning with qt_Matrix and qt_Opacity, their
// samplers bound by number, their input at a location. Made GLSL ES 3.00 as
// they load: each of the block's members a uniform of its own, the bindings
// and the input's location gone, a precision said. Nothing else of a shader
// is touched, so the preview runs the app's code as it is.
export function fromQt(source) {
    let body = String(source).replace(/^[ \t]*#version[^\n]*\n?/m, "");
    const head = /layout\s*\(\s*std140[^)]*\)\s*uniform\s+\w+\s*\{/.exec(body);
    if (head) {
        // The block's members, up to its closing brace, past any in a comment.
        const comment = /\/\/[^\n]*|\/\*[\s\S]*?(?:\*\/|$)/y;
        let at = head.index + head[0].length;
        let members = "";
        while (at < body.length && body[at] !== "}") {
            comment.lastIndex = at;
            if (comment.test(body)) {
                at = comment.lastIndex;
                continue;
            }
            members += body[at++];
        }
        const end = /^\}\s*;?/.exec(body.slice(at));
        body = body.slice(0, head.index)
            + members.split(";").map(member => member.trim()).filter(Boolean).map(member => `uniform ${member};`).join("\n")
            + body.slice(at + (end ? end[0].length : 0));
    }
    body = body.replace(/layout\s*\(\s*binding\s*=\s*\d+\s*\)\s*uniform\b/g, "uniform");
    body = body.replace(/layout\s*\(\s*location\s*=\s*\d+\s*\)\s*in\b/g, "in");
    return "#version 300 es\nprecision highp float;\nprecision highp int;\n" + body;
}

// The uniforms a shader declares, by name: what its ShaderEffect in Main.qml
// sets by the same names.
export function uniformsOf(source) {
    return [...fromQt(source).matchAll(/^\s*uniform\s+\w+\s+(\w+)\s*;/gm)].map(m => m[1]);
}

// A Qt Quick 3D custom material's shader (the Orb look's, in
// modules/arcade/views/orb/) as WebGL 2 takes it: its MAIN, VARYING and
// FRAGCOLOR, and the built-ins it reads (VERTEX, NORMAL and UV0 at
// attributes 0, 1 and 2; the matrices and the camera's position as uniforms),
// said in GLSL ES 3.00 ahead of it, and in the fragment shader each of the
// material's properties a uniform (`properties`: name → GLSL type; a colour's
// a vec4, a texture's a sampler2D). The shader itself is left as it is.
export const ATTRIBUTES = ["VERTEX", "NORMAL", "UV0"];
export function fromQt3D(source, stage, properties = {}) {
    const head = ["#version 300 es", "precision highp float;", "precision highp int;", "#define MAIN main"];
    if (stage === "vertex") {
        head.push("#define VARYING out", "#define POSITION gl_Position",
                  "in vec3 VERTEX;", "in vec3 NORMAL;", "in vec2 UV0;",
                  "uniform mat4 MODEL_MATRIX;", "uniform mat3 NORMAL_MATRIX;",
                  "uniform mat4 MODELVIEWPROJECTION_MATRIX;", "uniform vec3 CAMERA_POSITION;");
    } else {
        head.push("#define VARYING in", "out vec4 FRAGCOLOR;");
        for (const [name, type] of Object.entries(properties))
            head.push(`uniform ${type} ${name};`);
    }
    return head.join("\n") + "\n" + String(source);
}
