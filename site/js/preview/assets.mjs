// The pictures the preview draws with, made as the app makes them: a skin's
// picture in the scheme's two colours (OsdSkinProvider: light the scheme's
// colour, dark its background, clear left clear) and the logo in one colour,
// trimmed to its drawing at the height asked for (OsdIconProvider). Each is
// read once and kept; until it is in, null, and `changed` is called once it
// is.
import { bytesOf } from "./look.mjs";

export class Assets {
    constructor(base, changed) {
        this.base = base;
        this.changed = changed;
        this.images = new Map();
        this.made = new Map();
        this.svg = undefined;
        this.svgSizes = new Map();
    }

    image(source) {
        let entry = this.images.get(source);
        if (!entry) {
            entry = { image: new Image(), ready: false };
            entry.image.onload = () => { entry.ready = true; this.changed(); };
            entry.image.src = this.base + source;
            this.images.set(source, entry);
        }
        return entry.ready ? entry.image : null;
    }

    skin(source, ink, paper) {
        const key = `skin|${source}|${ink}|${paper}`;
        if (this.made.has(key))
            return this.made.get(key);
        const image = this.image(source);
        if (!image)
            return null;
        const canvas = document.createElement("canvas");
        canvas.width = image.naturalWidth;
        canvas.height = image.naturalHeight;
        const ctx = canvas.getContext("2d");
        ctx.drawImage(image, 0, 0);
        const data = ctx.getImageData(0, 0, canvas.width, canvas.height);
        const p = data.data;
        const inkRgb = bytesOf(ink), paperRgb = bytesOf(paper);
        for (let i = 0; i < p.length; i += 4) {
            if (p[i + 3] < 128) {
                p[i] = p[i + 1] = p[i + 2] = p[i + 3] = 0;
                continue;
            }
            // qGray(): (r·11 + g·16 + b·5) / 32.
            const color = ((p[i] * 11 + p[i + 1] * 16 + p[i + 2] * 5) >> 5) >= 128 ? inkRgb : paperRgb;
            p[i] = color[0];
            p[i + 1] = color[1];
            p[i + 2] = color[2];
            p[i + 3] = 255;
        }
        ctx.putImageData(data, 0, 0);
        this.made.set(key, canvas);
        return canvas;
    }

    // The logo (assets/images/logo.svg) drawn at a size, stretched to it as
    // Qt's SVG reader draws one: a picture once it is in.
    svgAt(width, height) {
        const key = `${width}x${height}`;
        let entry = this.svgSizes.get(key);
        if (!entry && this.svg) {
            entry = { image: new Image(), ready: false };
            entry.image.onload = () => { entry.ready = true; this.changed(); };
            const sized = this.svg.text.replace(/<svg\b([^>]*)>/, (_, attrs) =>
                `<svg${attrs.replace(/\s(width|height|preserveAspectRatio)="[^"]*"/g, "")} width="${width}" height="${height}" preserveAspectRatio="none">`);
            entry.image.src = "data:image/svg+xml;charset=utf-8," + encodeURIComponent(sized);
            this.svgSizes.set(key, entry);
        }
        return entry && entry.ready ? entry.image : null;
    }

    logo(color, height) {
        const key = `logo|${color}|${height}`;
        if (this.made.has(key))
            return this.made.get(key);
        if (this.svg === undefined) {
            this.svg = null;
            fetch(this.base + "img/logo.svg").then(r => r.text()).then(text => {
                const box = (text.match(/viewBox="([^"]+)"/) || [])[1];
                const [, , w, h] = box ? box.split(/[\s,]+/).map(Number) : [0, 0, 44, 24];
                this.svg = { text, width: w, height: h };
                this.changed();
            }).catch(() => {});
        }
        if (!this.svg)
            return null;
        // Where the drawing is, found on a large rendering of it.
        const probeHeight = Math.max(height * 8, 64);
        const probeWidth = Math.max(1, Math.round(this.svg.width * probeHeight / this.svg.height));
        const probe = this.svgAt(probeWidth, probeHeight);
        if (!probe)
            return null;
        const canvas = document.createElement("canvas");
        canvas.width = probeWidth;
        canvas.height = probeHeight;
        const ctx = canvas.getContext("2d");
        ctx.drawImage(probe, 0, 0);
        const p = ctx.getImageData(0, 0, probeWidth, probeHeight).data;
        let left = probeWidth, right = -1, top = probeHeight, bottom = -1;
        for (let y = 0; y < probeHeight; ++y) {
            for (let x = 0; x < probeWidth; ++x) {
                if (p[(y * probeWidth + x) * 4 + 3] === 0)
                    continue;
                left = Math.min(left, x);
                right = Math.max(right, x + 1);
                top = Math.min(top, y);
                bottom = Math.max(bottom, y + 1);
            }
        }
        if (right < 0)
            return null;
        // Drawn again at the scale that makes the drawing the height asked
        // for, then cut to it, in the colour with its own edges.
        const scale = height / (bottom - top);
        const full = this.svgAt(Math.max(1, Math.round(probeWidth * scale)), Math.max(1, Math.round(probeHeight * scale)));
        if (!full)
            return null;
        const x0 = Math.floor(left * scale), y0 = Math.floor(top * scale);
        const out = document.createElement("canvas");
        out.width = Math.max(1, Math.ceil(right * scale) - x0);
        out.height = Math.max(1, Math.ceil(bottom * scale) - y0);
        const octx = out.getContext("2d");
        octx.drawImage(full, -x0, -y0);
        octx.globalCompositeOperation = "source-in";
        octx.fillStyle = color;
        octx.fillRect(0, 0, out.width, out.height);
        this.made.set(key, out);
        return out;
    }
}
