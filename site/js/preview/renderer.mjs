// The preview's picture on the GPU (WebGL 2), put together as Main.qml puts
// the app's together, with the app's own shaders (glsl.mjs makes them WebGL's):
//   the window, its face: the ground (OsdGround), the background effect over
//     it (BackgroundFx: shaders/bg-*.frag drawn in art pixels and scaled up),
//     the rainbow selector behind the view, the view, the other selectors;
//   a window change between the face caught a moment before and the new
//     one: Fade, the Cube's turn (its matrices), or Ripple, Wave and Drop
//     (shaders/ripple.frag, wave.frag, drop.frag);
//   the screen's and the text's effects over all of it (shaders/effects.frag).
// Each pass sets its shader's uniforms by the names Main.qml sets them by.
import { fromQt } from "./glsl.mjs";

const VERTEX = `#version 300 es
in vec2 corner;
uniform vec4 rect;       // where it goes on its target, in the target's pixels
uniform vec2 target;     // the target's size
uniform float flipY;     // 1 onto the page, -1 into a texture (its rows top first)
uniform mat4 transform;  // the item's matrix (the cube's), on the target's pixels
out vec2 qt_TexCoord0;
void main() {
    qt_TexCoord0 = corner;
    vec4 p = transform * vec4(rect.xy + corner * rect.zw, 0.0, 1.0);
    gl_Position = vec4(2.0 * p.x / target.x - p.w, flipY * (p.w - 2.0 * p.y / target.y), 0.0, p.w);
}`;

const BLIT = `#version 300 es
precision highp float;
in vec2 qt_TexCoord0;
out vec4 fragColor;
uniform sampler2D source;
uniform float qt_Opacity;
void main() { fragColor = texture(source, qt_TexCoord0) * qt_Opacity; }`;

const IDENTITY = new Float32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]);

export class Renderer {
    // shaders: { name: source } of shaders/*.frag.
    constructor(canvas, width, height, shaders) {
        const gl = canvas.getContext("webgl2", { alpha: false, antialias: false, depth: false, stencil: false,
                                                 premultipliedAlpha: true, preserveDrawingBuffer: false });
        if (!gl)
            throw new Error("No WebGL 2");
        this.gl = gl;
        this.width = width;
        this.height = height;
        this.errors = [];
        this.quad = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, this.quad);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([0, 0, 1, 0, 0, 1, 1, 1]), gl.STATIC_DRAW);
        this.vertex = this.compile(gl.VERTEX_SHADER, VERTEX, "vertex");
        this.programs = new Map();
        this.blit = this.link("blit", BLIT);
        this.sources = shaders;
        this.textures = {};
        this.targets = {};
        for (const name of ["face", "last", "screen"])
            this.targets[name] = this.target(width, height, gl.LINEAR);
    }

    compile(type, source, name) {
        const gl = this.gl;
        const shader = gl.createShader(type);
        gl.shaderSource(shader, source);
        gl.compileShader(shader);
        if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
            this.errors.push(`${name}: ${gl.getShaderInfoLog(shader)}`);
            return null;
        }
        return shader;
    }

    link(name, fragmentSource) {
        const gl = this.gl;
        const fragment = this.compile(gl.FRAGMENT_SHADER, fragmentSource, name);
        if (!fragment || !this.vertex)
            return null;
        const program = gl.createProgram();
        gl.attachShader(program, this.vertex);
        gl.attachShader(program, fragment);
        gl.bindAttribLocation(program, 0, "corner");
        gl.linkProgram(program);
        if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
            this.errors.push(`${name}: ${gl.getProgramInfoLog(program)}`);
            return null;
        }
        const uniforms = new Map();
        for (let i = 0; i < gl.getProgramParameter(program, gl.ACTIVE_UNIFORMS); ++i) {
            const info = gl.getActiveUniform(program, i);
            uniforms.set(info.name, { location: gl.getUniformLocation(program, info.name), type: info.type });
        }
        return { program, uniforms };
    }

    // The app's shader of that name (shaders/<name>.frag), made once; null
    // where it can't be used (the app then goes without, as it does).
    shader(name) {
        if (!this.sources[name])
            return null;
        if (!this.programs.has(name))
            this.programs.set(name, this.link(name, fromQt(this.sources[name])));
        return this.programs.get(name);
    }

    // Every shader the page has, made now: what wouldn't be, said.
    compileAll() {
        for (const name of Object.keys(this.sources))
            this.shader(name);
        return this.errors;
    }

    texture(filter) {
        const gl = this.gl;
        const texture = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, texture);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, filter);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, filter);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
        return texture;
    }

    target(width, height, filter) {
        const gl = this.gl;
        const texture = this.texture(filter);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, width, height, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
        const framebuffer = gl.createFramebuffer();
        gl.bindFramebuffer(gl.FRAMEBUFFER, framebuffer);
        gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, texture, 0);
        return { texture, framebuffer, width, height };
    }

    // A canvas (premultiplied as it is drawn) or a frame of premultiplied
    // pixels into the texture of that name.
    upload(name, source, width, height, filter) {
        const gl = this.gl;
        if (!this.textures[name])
            this.textures[name] = this.texture(filter);
        gl.bindTexture(gl.TEXTURE_2D, this.textures[name]);
        if (source instanceof Uint8Array) {
            gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
            gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, width, height, 0, gl.RGBA, gl.UNSIGNED_BYTE, source);
        } else {
            gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, true);
            gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, gl.RGBA, gl.UNSIGNED_BYTE, source);
        }
        return this.textures[name];
    }

    bindTarget(target) {
        const gl = this.gl;
        gl.bindFramebuffer(gl.FRAMEBUFFER, target ? target.framebuffer : null);
        gl.viewport(0, 0, target ? target.width : this.width, target ? target.height : this.height);
    }

    clear(color) {
        const gl = this.gl;
        gl.clearColor(color[0], color[1], color[2], color[3]);
        gl.clear(gl.COLOR_BUFFER_BIT);
    }

    // One pass: `program` over `rect` of `target` (null: the page), its
    // uniforms by name, its samplers' textures in `textures`, blended over
    // what is there (premultiplied) unless `blend` is false.
    draw(program, target, { rect, transform = IDENTITY, uniforms = {}, textures = {}, blend = true }) {
        if (!program)
            return;
        const gl = this.gl;
        this.bindTarget(target);
        gl.useProgram(program.program);
        gl.bindBuffer(gl.ARRAY_BUFFER, this.quad);
        gl.enableVertexAttribArray(0);
        gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 0, 0);
        if (blend) {
            gl.enable(gl.BLEND);
            gl.blendFunc(gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
        } else {
            gl.disable(gl.BLEND);
        }
        const width = target ? target.width : this.width, height = target ? target.height : this.height;
        const values = { rect: rect || [0, 0, width, height], target: [width, height], flipY: target ? -1 : 1,
                         transform, qt_Opacity: 1, ...uniforms };
        let unit = 0;
        for (const [name, { location, type }] of program.uniforms) {
            if (type === gl.SAMPLER_2D) {
                gl.activeTexture(gl.TEXTURE0 + unit);
                gl.bindTexture(gl.TEXTURE_2D, textures[name] || null);
                gl.uniform1i(location, unit++);
                continue;
            }
            const value = values[name];
            if (value === undefined)
                continue;
            if (type === gl.FLOAT) gl.uniform1f(location, value);
            else if (type === gl.FLOAT_VEC2) gl.uniform2fv(location, value);
            else if (type === gl.FLOAT_VEC3) gl.uniform3fv(location, value);
            else if (type === gl.FLOAT_VEC4) gl.uniform4fv(location, value);
            else if (type === gl.FLOAT_MAT4) gl.uniformMatrix4fv(location, false, value);
            else if (type === gl.INT || type === gl.BOOL) gl.uniform1i(location, value);
        }
        gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
    }

    // A picture drawn as it is, or scaled, over what is there.
    copy(texture, target, rect, { opacity = 1, transform = IDENTITY } = {}) {
        this.draw(this.blit, target, { rect, transform, uniforms: { qt_Opacity: opacity }, textures: { source: texture } });
    }
}
