// OSD/OS's landing page: the theme previews (a still in the HTML, the
// recording swapped in once it is loaded, unless the visitor asked for less
// motion), and the hardware tabs. The themes come from themes.json, which
// scripts/build-site.py writes from the app's own themes: each with its
// name, its two colours and its pictures.

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
// is left where it is.
function showRecording(img, recording, alt) {
    img.dataset.wanted = recording;
    whenNear(img, () => {
        if (img.dataset.wanted !== recording)
            return;
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

function updateMotionButton() {
    motion.replaceChildren(document.createTextNode(paused ? "Play animation " : "Pause animation "),
                           pixelIcon(paused ? "play" : "pause"));
}

function updateTheme() {
    if (!current)
        return;
    const stillAlt = `The ${current.name} theme: ${current.about}`;
    const recordingAlt = `The ${current.name} theme, recorded as the menus are browsed: ${current.about}`;
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
motion.addEventListener("click", () => {
    paused = !paused;
    updateMotionButton();
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

// --- The hardware tabs: one panel shown, the arrows and Home/End moving
// between the tabs as a tablist takes them.
const tabs = [...document.querySelectorAll("[role=tab]")];
function selectTab(tab) {
    for (const t of tabs) {
        const on = t === tab;
        t.setAttribute("aria-selected", String(on));
        t.tabIndex = on ? 0 : -1;
        document.getElementById(t.getAttribute("aria-controls")).hidden = !on;
    }
}
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
