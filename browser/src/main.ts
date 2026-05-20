import * as ucobs from 'ucobs';
import * as msgpack from "@msgpack/msgpack";
import kitbase64 from "./kitbase64.txt";
import adafruitbase64 from "./adafruitbase64.txt";

const UNITS = [
    "px",
    "cm"
] as const;
type Unit = typeof UNITS[number];

type DSource = Generator<any, void, unknown>;
type DSink = (v: unknown) => void;

function readUnit(source: DSource): Unit {
    const ind: number = source.next().value;
    const unit = UNITS[ind];
    if (!unit) throw "invalid unit";
    return unit;
}

function writeUnit(sink: DSink, v: Unit) {
    switch (v) {
        case "px":
            sink(0);
            break;
        case "cm":
            sink(1);
            break;
        default:
            let _: never = v;
    }
}

type Length = {
    value: number,
    unit: Unit
};

function readLength(source: DSource): Length {
    const value: number = source.next().value;
    const unit = readUnit(source);
    return { value, unit };
}

function writeLength(sink: DSink, v: Length) {
    sink(v.value);
    writeUnit(sink, v.unit);
}

type BoxLength = {
    top: Length,
    bottom: Length,
    left: Length,
    right: Length,
};

function readBoxLength(source: DSource): BoxLength {
    const top = readLength(source);
    const bottom = readLength(source);
    const left = readLength(source);
    const right = readLength(source);
    return { top, bottom, left, right };
}

function writeBoxLength(sink: DSink, v: BoxLength) {
    writeLength(sink, v.top);
    writeLength(sink, v.bottom);
    writeLength(sink, v.left);
    writeLength(sink, v.right);
}

type RGB = {
    r: number,
    g: number,
    b: number,
};

function readRGB(source: DSource): RGB {
    const value: number = source.next().value;
    const r = (value >> 16) & 0xff;
    const g = (value >> 8) & 0xff;
    const b = value & 0xff;
    return { r, g, b };
}

function writeRGB(sink: DSink, v: RGB) {
    const value = (v.r << 16) | (v.g << 8) | v.b;
    sink(value);
}

enum Mode {
    Hover = 0,
    Locked = 1,
}

let currentMode: Mode = Mode.Hover;
let hoveredElement: Element | null = null;
let lockedElement: Element | null = null;
let comms: Comms | undefined;

function targetElement(): Element | null {
    switch (currentMode) {
        case Mode.Hover:
            return hoveredElement;
        case Mode.Locked:
            return lockedElement;
        default:
            return currentMode;
    }
}

function setTargetStyle(property: string, value: string) {
    const element = targetElement();
    if (!element) return;
    (element as HTMLElement).style.setProperty(property, value, "important");
}

function setCurrentMode(mode: Mode) {
    if (mode === currentMode) return;
    currentMode = mode;
    if (currentMode == Mode.Locked) {
        lockedElement = hoveredElement;
    } else {
        lockedElement = null;
    }
    updateStyleInfo();
}

function logMessage(source: DSource) {
    const msg: string = source.next().value;
    console.log(`device log: ${msg}`);
}

function setMode(source: DSource) {
    const which: number = source.next().value;
    if (which >= 0 && which < 2) {
        setCurrentMode(which);
    }
}

function setColor(source: DSource) {
    const which: number = source.next().value;
    const color = readRGB(source);
    setTargetStyle(which === 0 ? "color" : "background-color", `rgb(${color.r},${color.g},${color.b})`);
}

function setFont(source: DSource) {
    const name: string = source.next().value;
    setTargetStyle("font-family", name);
}

function lengthString(l: Length): string {
    return `${l.value}${l.unit}`;
}

function setTextSize(source: DSource) {
    const size = readLength(source);
    setTargetStyle("font-size", lengthString(size));
}

function setLineHeight(source: DSource) {
    const size = readLength(source);
    setTargetStyle("line-height", lengthString(size));
}

function setWidth(source: DSource) {
    const width = readLength(source);
    setTargetStyle("width", lengthString(width));
}

function setHeight(source: DSource) {
    const height = readLength(source);
    setTargetStyle("height", lengthString(height));
}

function setMargin(source: DSource) {
    const margin = readBoxLength(source);
    setTargetStyle("margin-top", lengthString(margin.top));
    setTargetStyle("margin-bottom", lengthString(margin.bottom));
    setTargetStyle("margin-left", lengthString(margin.left));
    setTargetStyle("margin-right", lengthString(margin.right));
}

function setPadding(source: DSource) {
    const padding = readBoxLength(source);
    setTargetStyle("padding-top", lengthString(padding.top));
    setTargetStyle("padding-bottom", lengthString(padding.bottom));
    setTargetStyle("padding-left", lengthString(padding.left));
    setTargetStyle("padding-right", lengthString(padding.right));
}

function setTransform(source: DSource) {
    const rx: number = source.next().value;
    const ry: number = source.next().value;
    const rz: number = source.next().value;
    const angle: number = source.next().value;
    const x: number = source.next().value;
    const y: number = source.next().value;
    const z: number = source.next().value;
    setTargetStyle(
        "transform",
        "perspective(30cm)"
        + ` translateX(${x * 100}cm) translateY(${y * 100}cm) translateZ(${z * 100}cm)`
        + ` rotate3d(${rx}, ${ry}, ${rz}, ${angle}rad)`
    );
}

function doEffect(source: DSource) {
    const which: number = source.next().value;
    if (which === 0) {
        let img = document.createElement("img");
        img.style.position = "fixed";
        img.style.bottom = "0px";
        img.style.left = "10px";
        img.style.zIndex = "1000000";
        // Add v param to cache bust (otherwise the gif won't replay)
        img.src = `data:image/gif;v=${Date.now()};base64,${kitbase64}`;
        document.body.appendChild(img);
        setTimeout(() => document.body.removeChild(img), 4000);
    } else if (which === 1) {
        let img = document.createElement("img");
        img.style.position = "fixed";
        img.style.top = "50%";
        img.style.left = "0";
        img.style.opacity = "0";
        img.style.filter = "drop-shadow(0px 0px 5px #ffffff)";
        img.style.marginTop = "-225px";
        img.style.marginLeft = "-225px";
        img.style.zIndex = "1000000";
        img.src = `data:image/svg+xml;base64,${adafruitbase64}`;
        document.body.appendChild(img);
        const keyframes = [
            { opacity: 0, transform: "rotate(0)", left: 0 },
            { opacity: 1, transform: "rotate(216deg)", left: "50%", offset: 0.3 },
            { opacity: 1, transform: "rotate(216deg)", left: "50%", offset: 0.7 },
            { opacity: 0, transform: "rotate(432deg)", left: "100%" },
        ];
        img.animate(keyframes, { duration: 4000, iterations: 1, easing: "ease-in-out" });
        setTimeout(() => document.body.removeChild(img), 4000);
    }
}

const InboundMessages: ((source: DSource) => void)[] = [
    logMessage,
    setMode,
    setColor,
    setFont,
    setTextSize,
    setLineHeight,
    setWidth,
    setHeight,
    setMargin,
    setPadding,
    setTransform,
    doEffect,
];

type StyleInfo = {
    element: string,
    colors: RGB[],
    font: string,
    fontSize: Length,
    lineSpacing: Length,
    width: Length,
    height: Length,
    margin: BoxLength,
    padding: BoxLength
};

function parseCSSLength(val: string): Length {
    const match = val.match(/([+-]?[.0-9]+)(.*)/);
    if (!match) return { value: 0, unit: "px" };
    const value = parseFloat(match[1]!);
    let unit = match[2] ?? "px";
    if (!(UNITS as readonly string[]).includes(unit)) {
        unit = "px";
    }
    return { value, unit: unit as Unit };
}

function getVisualColor(element: Element, colorProp: string) {
    let currentElement: Element | null = element;
    while (currentElement) {
        const color = getComputedStyle(currentElement).getPropertyValue(colorProp);
        if (color !== "transparent" && color !== "rgba(0, 0, 0, 0)") {
            return color;
        }
        currentElement = currentElement.parentElement;
    }
    return "rgb(255, 255, 255)";
}

async function updateStyleInfo() {
    const element = targetElement();
    if (!comms || !element) return;
    const style = getComputedStyle(element);

    const colorProperties = ["color", "background-color"];
    const colors = colorProperties.map(prop => {
        const m = getVisualColor(element, prop).match(/^rgba?\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*[,)]?/i);
        if (m) {
            return { r: parseInt(m[1]!), g: parseInt(m[2]!), b: parseInt(m[3]!) };
        } else {
            return { r: 0, g: 0, b: 0 };
        }
    });
    const styleInfo: StyleInfo = {
        element: element.tagName.toLowerCase() + (element.id ? `#${element.id}` : ""),
        colors,
        font: style.fontFamily,
        fontSize: parseCSSLength(style.fontSize),
        lineSpacing: parseCSSLength(style.lineHeight),
        width: parseCSSLength(style.width),
        height: parseCSSLength(style.height),
        margin: {
            top: parseCSSLength(style.marginTop),
            bottom: parseCSSLength(style.marginBottom),
            left: parseCSSLength(style.marginLeft),
            right: parseCSSLength(style.marginRight),
        },
        padding: {
            top: parseCSSLength(style.paddingTop),
            bottom: parseCSSLength(style.paddingBottom),
            left: parseCSSLength(style.paddingLeft),
            right: parseCSSLength(style.paddingRight),
        },
    };
    await comms.sendStyleInfo(styleInfo);
}

class Comms {
    port: SerialPort;
    writer: WritableStreamDefaultWriter<Uint8Array> | undefined;

    constructor(port: SerialPort) {
        this.port = port;
        this.writer = this.port.writable?.getWriter();
    }

    static async create() {
        let port = await navigator.serial.requestPort();
        await port.open({ baudRate: 115200 })
        await port.setSignals({ dataTerminalReady: true });
        return new Comms(port);
    }

    async readMessages() {
        if (!this.port.readable) return;

        let payload: Uint8Array | undefined;
        const decodeStream = ucobs.createStreamDecoder((chunk, isEnd) => {
            if (!payload) {
                payload = chunk;
            } else {
                const newPayload = new Uint8Array(payload.length + chunk.length);
                newPayload.set(payload);
                newPayload.set(chunk, payload.length);
                payload = newPayload;
            }

            if (isEnd) {
                const msgId = payload[0];
                if (msgId !== undefined && msgId < InboundMessages.length) {
                    InboundMessages[msgId]!(msgpack.decodeMulti(payload.subarray(1)));
                }
                payload = undefined;
            }
        });

        for await (const chunk of this.port.readable) {
            decodeStream(chunk);
        }
    }

    async sendReset() {
        let curWrite = Promise.resolve();
        const [push, end] = ucobs.createStreamEncoder((chunk, isEnd) => {
            curWrite = curWrite.then(() => this.writer?.write(chunk));
        });
        push(new Uint8Array([0])); // Message id
        end();
        await curWrite;
    }

    async sendStyleInfo(info: StyleInfo) {
        let curWrite = Promise.resolve();
        const [push, end] = ucobs.createStreamEncoder((chunk, isEnd) => {
            curWrite = curWrite.then(() => this.writer?.write(chunk));
        });

        let encoder = new msgpack.Encoder();

        push(new Uint8Array([1])); // Message id

        const pushEncode = (val: unknown) => push(encoder.encode(val));

        const colors: unknown[] = [];
        for (const color of info.colors) {
            writeRGB((v) => colors.push(v), color);
        }
        pushEncode(info.element);
        pushEncode(colors);
        pushEncode(info.font);
        writeLength(pushEncode, info.fontSize);
        writeLength(pushEncode, info.lineSpacing);
        writeLength(pushEncode, info.width);
        writeLength(pushEncode, info.height);
        writeBoxLength(pushEncode, info.margin);
        writeBoxLength(pushEncode, info.padding);
        end();
        await curWrite;
    }
}

async function main() {
    comms = await Comms.create();

    document.addEventListener("mousemove", e => {
        const newHoveredElement = document.elementFromPoint(e.clientX, e.clientY);
        const change = hoveredElement !== newHoveredElement;
        hoveredElement = newHoveredElement;
        if (currentMode == Mode.Hover && change) {
            updateStyleInfo();
        }
    }, { passive: true });

    await comms.sendReset();
    await updateStyleInfo();
    comms.readMessages();
}

function addButton() {
    const button = document.createElement("button");
    button.type = "button";
    button.innerText = "🕹️ Play";
    button.addEventListener("click", main);
    button.style.position = "fixed";
    button.style.bottom = "8px";
    button.style.right = "8px";
    button.style.zIndex = "1000000";
    document.body.appendChild(button);
}

if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", addButton);
} else {
    addButton();
}
