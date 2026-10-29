// OSD/OS's landing page: the theme previews (a still in the HTML, the
// recording swapped in once it is loaded, unless the visitor asked for less
// motion), the tabs, and the terminal commands' Copy buttons. The
// themes come from themes.json, which scripts/build-site.py writes from the
// app's own themes: each with its name, its two colours and its pictures,
// the recording a GIF or, for a 3D look a GIF would blur, an MP4.

function pixelIcon(name) {
    const ns = "http://www.w3.org/2000/svg";
    const svg = document.createElementNS(ns, "svg");
    const use = document.createElementNS(ns, "use");
    svg.setAttribute("class", "pixel-icon");
    svg.setAttribute("aria-hidden", "true");
    use.setAttribute("href", "#icon-" + name);
    svg.append(use);
    return svg;
}

// --- The theme previews.
const image = document.querySelector("#theme-image");
const video = document.querySelector("#theme-video");
const nameLabel = document.querySelector("#theme-name");
const motion = document.querySelector("#motion");
const buttons = document.querySelector("#theme-buttons");
const tour = document.querySelector("#tour-image");
let themes = [];
let current = null;
let paused = matchMedia("(prefers-reduced-motion: reduce)").matches;

// A picture's turn comes as it nears the screen: the recordings are below
// the fold, and up to a megabyte each.
function whenNear(img, then) {
    if (!("IntersectionObserver" in window))
        return then();
    const watcher = new IntersectionObserver(entries => {
        if (entries.some(e => e.isIntersecting)) {
            watcher.disconnect();
            then();
        }
    }, { rootMargin: "200px" });
    watcher.observe(img);
}

// A recording shown once it is in, the still until then: a blank frame
// while it loads would be a flicker. Asked for another meanwhile, this one
// is left where it is. An MP4 plays in the theme preview's video instead.
function showRecording(img, recording, alt) {
    img.dataset.wanted = recording;
    whenNear(img, () => {
        if (img.dataset.wanted !== recording)
            return;
        if (recording.endsWith(".mp4"))
            return playVideo(img, recording, alt);
        const loader = new Image();
        loader.onload = () => {
            if (img.dataset.wanted !== recording)
                return;
            img.src = recording;
            img.alt = alt;
        };
        loader.src = recording;
    });
}

// The video in the picture's place, the still its poster until it plays,
// and for good where it can't: a browser without H.264, or one holding back
// a muted video's playing (iOS's Low Power Mode). Played, not preloaded, it
// is downloaded only to be played.
function playVideo(img, recording, alt) {
    video.poster = img.getAttribute("src");
    video.setAttribute("aria-label", alt);
    video.src = recording;
    img.hidden = true;
    video.hidden = false;
    video.play().catch(() => {});
}

// The video stopped and put away, the picture back in its place.
function stopVideo() {
    if (!video.hasAttribute("src"))
        return;
    video.pause();
    video.removeAttribute("src");
    video.load();
    video.hidden = true;
    image.hidden = false;
}

function updateMotionButton() {
    motion.replaceChildren(document.createTextNode(paused ? "Play animation " : "Pause animation "),
                           pixelIcon(paused ? "play" : "pause"));
}

// The page's own blinking (the headline's cursor, PRESS START) rests along
// with the recordings.
function updateStill() {
    document.documentElement.classList.toggle("still", paused);
}

function updateTheme() {
    if (!current)
        return;
    const stillAlt = `The ${current.name} theme: ${current.about}`;
    const recordingAlt = `The ${current.name} theme, recorded ${current.recorded || "as the menus are browsed"}: ${current.about}`;
    stopVideo();
    image.dataset.wanted = "";
    image.src = current.still;
    image.alt = stillAlt;
    if (!paused)
        showRecording(image, current.recording, recordingAlt);
    nameLabel.textContent = current.name;
}

function selectTheme(theme) {
    current = theme;
    for (const b of buttons.querySelectorAll("button"))
        b.setAttribute("aria-pressed", String(b.dataset.theme === theme.id));
    updateTheme();
}

function updateTour() {
    if (!tour)
        return;
    if (paused) {
        tour.dataset.wanted = "";
        tour.src = tour.dataset.still;
        tour.alt = tour.dataset.altStill;
    } else {
        showRecording(tour, tour.dataset.recording, tour.dataset.altRecording);
    }
}

if (tour) {
    tour.dataset.still = tour.getAttribute("src");
    tour.dataset.altStill = tour.getAttribute("alt");
}

updateMotionButton();
updateStill();
motion.addEventListener("click", () => {
    paused = !paused;
    updateMotionButton();
    updateStill();
    updateTheme();
    updateTour();
});

fetch("themes.json")
    .then(reply => reply.json())
    .then(list => {
        themes = list;
        for (const theme of themes) {
            const b = document.createElement("button");
            b.type = "button";
            b.dataset.theme = theme.id;
            b.append(document.createTextNode(theme.name));
            // The theme's own two colours, as a swatch.
            b.style.setProperty("--swatch-ink", theme.primary);
            b.style.setProperty("--swatch-paper", theme.surface);
            b.setAttribute("aria-pressed", "false");
            b.addEventListener("click", () => selectTheme(theme));
            buttons.append(b);
        }
        selectTheme(themes.find(t => t.id === "trinitron") || themes[0]);
        updateTour();
    })
    .catch(() => {
        // Without the list the still in the HTML stands, as it is.
        updateTour();
    });

// --- The tabs, the terminal commands' at the top and the hardware's: in
// each list one panel shown, the arrows and Home/End moving between its
// tabs as a tablist takes them.
for (const list of document.querySelectorAll("[role=tablist]")) {
    const tabs = [...list.querySelectorAll("[role=tab]")];
    const selectTab = tab => {
        for (const t of tabs) {
            const on = t === tab;
            t.setAttribute("aria-selected", String(on));
            t.tabIndex = on ? 0 : -1;
            document.getElementById(t.getAttribute("aria-controls")).hidden = !on;
        }
    };
    tabs.forEach((tab, i) => {
        tab.addEventListener("click", () => selectTab(tab));
        tab.addEventListener("keydown", event => {
            let n;
            if (event.key === "ArrowRight") n = (i + 1) % tabs.length;
            else if (event.key === "ArrowLeft") n = (i + tabs.length - 1) % tabs.length;
            else if (event.key === "Home") n = 0;
            else if (event.key === "End") n = tabs.length - 1;
            if (n === undefined)
                return;
            event.preventDefault();
            selectTab(tabs[n]);
            tabs[n].focus();
        });
    });
}

// --- The terminal commands: each word kept whole, so that a command goes on
// to a second line only between its words. Copy puts one on the clipboard
// or, where the browser won't let the page, selects it to be copied by hand.
for (const code of document.querySelectorAll(".command code")) {
    code.replaceChildren(...code.textContent.split(" ").flatMap((word, i) => {
        const whole = document.createElement("span");
        whole.textContent = word;
        return i ? [" ", whole] : [whole];
    }));
}
for (const button of document.querySelectorAll(".command .copy")) {
    const command = button.parentElement.querySelector("code");
    let timer;
    button.hidden = false;
    button.addEventListener("click", async () => {
        try {
            await navigator.clipboard.writeText(command.textContent);
            button.textContent = "Copied";
        } catch {
            getSelection().selectAllChildren(command);
            button.textContent = "Selected";
        }
        clearTimeout(timer);
        timer = setTimeout(() => { button.textContent = "Copy"; }, 2000);
    });
}
