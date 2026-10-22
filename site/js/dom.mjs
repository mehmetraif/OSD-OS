// An element as the page and its preview make theirs: its attributes
// (`class` and `text` as they read, an `on…` as a listener, a true as the bare
// attribute, a false, a null or an undefined as none) and its children,
// flattened, a null or an undefined among them left out.
export function el(tag, attrs = {}, ...children) {
    const node = document.createElement(tag);
    for (const [key, value] of Object.entries(attrs)) {
        if (key === "class") node.className = value;
        else if (key === "text") node.textContent = value;
        else if (key.startsWith("on")) node.addEventListener(key.slice(2), value);
        else if (value === true) node.setAttribute(key, "");
        else if (value !== false && value !== null && value !== undefined) node.setAttribute(key, value);
    }
    for (const child of children.flat()) {
        if (child !== null && child !== undefined)
            node.append(child);
    }
    return node;
}
