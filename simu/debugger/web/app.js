const controls = {
    form: document.querySelector("#status-form"),
    wifiSignal: document.querySelector("#wifi-signal"),
    level: document.querySelector("#level"),
    voltage: document.querySelector("#voltage"),
    powerSource: document.querySelector("#power-source"),
    inputVoltage: document.querySelector("#input-voltage"),
    charging: document.querySelector("#charging"),
    chargingEnabled: document.querySelector("#charging-enabled"),
};

const ui = {
    connection: document.querySelector("#connection"),
    connectionLabel: document.querySelector("#connection-label"),
    levelOutput: document.querySelector("#level-output"),
    voltageOutput: document.querySelector("#voltage-output"),
};

const imu = {
    scene: document.querySelector("#imu-scene"),
    model: document.querySelector("#imu-model"),
    orientation: document.querySelector("#imu-orientation"),
    values: document.querySelector("#imu-values"),
    vertical: document.querySelector("#imu-vertical"),
    verticalOutput: document.querySelector("#imu-vertical-output"),
};

const chain = {
    control: document.querySelector(".chain-control"),
    leftList: document.querySelector("#chain-left-list"),
    leftCount: document.querySelector("#chain-left-count"),
    rightList: document.querySelector("#chain-right-list"),
    rightCount: document.querySelector("#chain-right-count"),
};

const STATUS_APPLY_DELAY_MS = 180;
const BUTTON_PULSE_MS = 90;
const IMU_SEND_INTERVAL_MS = 32;
const IMU_DRAG_DEGREES_PER_PIXEL = 0.46;
const IMU_SNAP_COSINE = Math.cos((18 * Math.PI) / 180);
const IMU_VERTICAL_Z_LIMIT = Math.sin((18 * Math.PI) / 180);
const IMU_SPIN_SNAP_DEGREES = 12;
const MAX_CHAIN_DEVICES_PER_SIDE = 12;
const debuggerButtons = document.querySelectorAll("[data-debugger-button]");
const debuggerButtonsByKey = new Map(Array.from(debuggerButtons, (button) => [button.dataset.keyboardKey, button]));
const buttonQueues = new Map();
const heldKeyboardKeys = new Set();
const heldImuKeys = new Set();
const imuState = {
    pitch: 0,
    roll: 0,
    spin: 0,
    gyroX: 0,
    gyroY: 0,
    gyroZ: 0,
    label: "Flat · +Z",
};
const imuSnapTargets = [
    { pitch: 0, roll: 0, label: "Flat · +Z" },
    { pitch: 180, roll: 0, label: "Flat · −Z" },
    { pitch: 90, roll: 0, label: "Vertical · −Y" },
    { pitch: -90, roll: 0, label: "Vertical · +Y" },
    { pitch: 0, roll: 90, label: "Vertical · −X" },
    { pitch: 0, roll: -90, label: "Vertical · +X" },
];
const chainState = {
    left: [{ id: 1, kind: "key" }],
    right: [{ id: 2, kind: "angle" }],
};
let statusApplyTimer;
let statusRequestInFlight = false;
let statusApplyQueued = false;
let imuDrag;
let imuSendTimer;
let imuRequestInFlight = false;
let imuSendQueued = false;
let imuSpinLastTime;
let imuSpinLastValue = 0;
let chainRequestInFlight = false;
let chainSendQueued = false;
let nextChainDeviceId = 3;
let simulatorWasOnline = false;

function setConnection(state) {
    ui.connection.dataset.state = state;
    ui.connectionLabel.textContent = {
        online: "Simulator connected",
        offline: "Simulator offline",
        checking: "Checking simulator",
    }[state];
}

function setRangeProgress(input) {
    const progress = ((input.value - input.min) / (input.max - input.min)) * 100;
    input.style.setProperty("--range-progress", `${progress}%`);
}

function refreshValues() {
    setRangeProgress(controls.level);
    setRangeProgress(controls.voltage);
    ui.levelOutput.value = `${controls.level.value}%`;
    ui.voltageOutput.value = `${(Number(controls.voltage.value) / 1000).toFixed(3)} V`;
}

function statusMessage() {
    return {
        type: "status",
        data: {
            wifi_signal: controls.wifiSignal.value,
            level: Number(controls.level.value),
            voltage_mv: Number(controls.voltage.value),
            input_voltage_mv: Number(controls.inputVoltage.value),
            power_source: controls.powerSource.value,
            charging: controls.charging.checked,
            charging_enabled: controls.chargingEnabled.checked,
        },
    };
}

function normalizeAngle(value) {
    return ((value + 180) % 360 + 360) % 360 - 180;
}

function clamp(value, minimum, maximum) {
    return Math.min(maximum, Math.max(minimum, value));
}

function accelerometerForOrientation(pitch, roll, spin = 0) {
    const pitchRadians = (pitch * Math.PI) / 180;
    const rollRadians = (roll * Math.PI) / 180;
    const spinRadians = (spin * Math.PI) / 180;
    const cosPitch = Math.cos(pitchRadians);
    const baseX = -cosPitch * Math.sin(rollRadians);
    const baseY = Math.sin(pitchRadians);
    const cosSpin = Math.cos(spinRadians);
    const sinSpin = Math.sin(spinRadians);
    return {
        x: baseX * cosSpin + baseY * sinSpin,
        y: -baseX * sinSpin + baseY * cosSpin,
        z: cosPitch * Math.cos(rollRadians),
    };
}

function formatSigned(value) {
    const normalized = Math.abs(value) < 0.005 ? 0 : value;
    return `${normalized >= 0 ? "+" : "−"}${Math.abs(normalized).toFixed(2)}`;
}

function refreshImuVisual() {
    const acceleration = accelerometerForOrientation(imuState.pitch, imuState.roll, imuState.spin);
    imu.model.style.setProperty("--imu-pitch", `${imuState.pitch}deg`);
    imu.model.style.setProperty("--imu-roll", `${imuState.roll}deg`);
    imu.model.style.setProperty("--imu-spin", `${imuState.spin}deg`);
    imu.orientation.textContent = imuState.label;
    imu.values.value = `X ${formatSigned(acceleration.x)} · Y ${formatSigned(acceleration.y)} · Z ${formatSigned(acceleration.z)} g`;
    imu.vertical.disabled = Math.abs(acceleration.z) > IMU_VERTICAL_Z_LIMIT;
}

function imuMessage() {
    const acceleration = accelerometerForOrientation(imuState.pitch, imuState.roll, imuState.spin);
    return {
        type: "imu",
        data: {
            accel_x: Number(acceleration.x.toFixed(5)),
            accel_y: Number(acceleration.y.toFixed(5)),
            accel_z: Number(acceleration.z.toFixed(5)),
            gyro_x: Number(imuState.gyroX.toFixed(3)),
            gyro_y: Number(imuState.gyroY.toFixed(3)),
            gyro_z: Number(imuState.gyroZ.toFixed(3)),
        },
    };
}

function chainMessage() {
    const snapshot = (devices) => devices.map(({ id, kind, x, y, pressed, value, delta }) => {
        if (kind === "joystick") return { id, kind, x, y, pressed };
        if (kind === "encoder") return { id, kind, value, delta, pressed };
        return { id, kind };
    });
    return {
        type: "chain",
        data: {
            left: snapshot(chainState.left),
            right: snapshot(chainState.right),
        },
    };
}

function renderJoystickControls(device) {
    const controls = document.createElement("div");
    controls.className = "chain-joystick-controls";

    for (const axis of ["x", "y"]) {
        const row = document.createElement("div");
        row.className = "chain-joystick-axis";
        const label = document.createElement("label");
        label.textContent = axis.toUpperCase();
        const range = document.createElement("input");
        range.type = "range";
        range.id = `chain-joystick-${device.id}-${axis}`;
        range.min = "-127";
        range.max = "127";
        range.step = "1";
        range.value = String(device[axis]);
        range.setAttribute("aria-label", `Joystick ${device.id} ${axis.toUpperCase()}`);
        label.htmlFor = range.id;

        const number = document.createElement("input");
        number.type = "number";
        number.min = range.min;
        number.max = range.max;
        number.step = range.step;
        number.value = range.value;
        number.setAttribute("aria-label", `Joystick ${device.id} ${axis.toUpperCase()} value`);

        const updateAxis = (value) => {
            device[axis] = clamp(Math.round(value), -127, 127);
            range.value = String(device[axis]);
            number.value = range.value;
            setRangeProgress(range);
            flushChainState();
        };
        range.addEventListener("input", () => updateAxis(range.valueAsNumber));
        number.addEventListener("input", () => {
            if (Number.isFinite(number.valueAsNumber)) updateAxis(number.valueAsNumber);
        });
        number.addEventListener("change", () => {
            if (!Number.isFinite(number.valueAsNumber)) number.value = String(device[axis]);
        });
        setRangeProgress(range);
        row.append(label, range, number);
        controls.append(row);
    }

    controls.append(renderChainPressedControl(device, "Joystick"));
    return controls;
}

function renderChainPressedControl(device, name) {
    const pressed = document.createElement("label");
    pressed.className = "switch-row chain-joystick-pressed";
    const label = document.createElement("span");
    label.textContent = "Pressed";
    const input = document.createElement("input");
    input.type = "checkbox";
    input.checked = device.pressed;
    input.setAttribute("aria-label", `${name} ${device.id} pressed`);
    input.addEventListener("change", () => {
        device.pressed = input.checked;
        flushChainState();
    });
    const indicator = document.createElement("i");
    indicator.setAttribute("aria-hidden", "true");
    pressed.append(label, input, indicator);
    return pressed;
}

function renderEncoderControls(device) {
    const controls = document.createElement("div");
    controls.className = "chain-joystick-controls";

    for (const field of ["value", "delta"]) {
        const label = document.createElement("label");
        label.className = "field";
        const name = document.createElement("span");
        name.textContent = field === "value" ? "Value" : "Delta";
        const wrapper = document.createElement("span");
        wrapper.className = "number-field";
        const number = document.createElement("input");
        number.type = "number";
        number.min = "-32768";
        number.max = "32767";
        number.step = "1";
        number.value = String(device[field]);
        number.setAttribute("aria-label", `Encoder ${device.id} ${field}`);
        number.addEventListener("input", () => {
            if (!Number.isFinite(number.valueAsNumber)) return;
            device[field] = clamp(Math.round(number.valueAsNumber), -32768, 32767);
            number.value = String(device[field]);
            flushChainState();
        });
        number.addEventListener("change", () => {
            if (!Number.isFinite(number.valueAsNumber)) number.value = String(device[field]);
        });
        wrapper.append(number);
        label.append(name, wrapper);
        controls.append(label);
    }

    controls.append(renderChainPressedControl(device, "Encoder"));
    return controls;
}

function renderChainSide(side) {
    const devices = chainState[side];
    const list = chain[`${side}List`];
    const count = chain[`${side}Count`];
    list.replaceChildren();

    devices.forEach((device, index) => {
        const row = document.createElement("div");
        row.className = "chain-device";

        const heading = document.createElement("div");
        heading.className = "chain-device-heading";

        const label = document.createElement("div");
        const name = document.createElement("strong");
        name.textContent = {
            key: "Key",
            angle: "Angle",
            joystick: "Joystick",
            buzzer: "Buzzer",
            encoder: "Encoder",
            mono: "Mono",
            rgb: "RGB",
            pir: "PIR",
            tof: "ToF",
        }[device.kind];
        const id = document.createElement("small");
        id.textContent = `#${String(device.id).padStart(2, "0")}`;
        label.append(name, id);

        const remove = document.createElement("button");
        remove.type = "button";
        remove.className = "chain-remove";
        remove.dataset.chainRemove = String(index);
        remove.setAttribute("aria-label", `Disconnect ${name.textContent} ${device.id}`);
        remove.title = "Disconnect this node and nodes beyond it";
        remove.textContent = "×";

        heading.append(label, remove);
        row.append(heading);
        if (device.kind === "joystick") row.append(renderJoystickControls(device));
        if (device.kind === "encoder") row.append(renderEncoderControls(device));
        list.append(row);
    });

    count.value = `${devices.length} ${devices.length === 1 ? "node" : "nodes"}`;
    const port = list.closest("[data-chain-side]");
    for (const button of port.querySelectorAll("[data-chain-add]")) {
        button.disabled = devices.length >= MAX_CHAIN_DEVICES_PER_SIDE;
    }
}

function renderChainTopology() {
    renderChainSide("left");
    renderChainSide("right");
}

async function forwardMessage(message) {
    const response = await fetch("/api/message", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(message),
    });
    if (!response.ok) throw new Error("simulator_offline");

    const reply = await response.json();
    if (!reply.ok) throw new Error(reply.error || "rejected");
    return reply;
}

function queueButtonState(button, pressed) {
    const previous = buttonQueues.get(button) ?? Promise.resolve();
    const next = previous
        .catch(() => {})
        .then(() =>
            forwardMessage({
                type: "button",
                data: { button: button.dataset.debuggerButton, pressed },
            }),
        );
    buttonQueues.set(button, next);
    return next;
}

async function flushImuState() {
    if (imuRequestInFlight) {
        imuSendQueued = true;
        return;
    }

    imuRequestInFlight = true;
    try {
        await forwardMessage(imuMessage());
        setConnection("online");
    } catch (error) {
        setConnection("offline");
    } finally {
        imuRequestInFlight = false;
        if (imuSendQueued) {
            imuSendQueued = false;
            flushImuState();
        }
    }
}

function scheduleImuState(immediate = false) {
    if (immediate) {
        window.clearTimeout(imuSendTimer);
        imuSendTimer = undefined;
        flushImuState();
        return;
    }
    if (imuSendTimer !== undefined) return;
    imuSendTimer = window.setTimeout(() => {
        imuSendTimer = undefined;
        flushImuState();
    }, IMU_SEND_INTERVAL_MS);
}

async function flushChainState() {
    if (chainRequestInFlight) {
        chainSendQueued = true;
        return;
    }

    chainRequestInFlight = true;
    try {
        await forwardMessage(chainMessage());
        setConnection("online");
    } catch (error) {
        setConnection("offline");
    } finally {
        chainRequestInFlight = false;
        if (chainSendQueued) {
            chainSendQueued = false;
            flushChainState();
        }
    }
}

function nearestImuSnapTarget() {
    const acceleration = accelerometerForOrientation(imuState.pitch, imuState.roll, imuState.spin);
    let nearest;
    let nearestDot = -1;
    for (const target of imuSnapTargets) {
        const targetAcceleration = accelerometerForOrientation(target.pitch, target.roll);
        const dot =
            acceleration.x * targetAcceleration.x +
            acceleration.y * targetAcceleration.y +
            acceleration.z * targetAcceleration.z;
        if (dot > nearestDot) {
            nearest = target;
            nearestDot = dot;
        }
    }
    if (nearestDot < IMU_SNAP_COSINE) return undefined;

    const targetAcceleration = accelerometerForOrientation(nearest.pitch, nearest.roll);
    const spinRadians = (imuState.spin * Math.PI) / 180;
    const baseX =
        targetAcceleration.x * Math.cos(spinRadians) - targetAcceleration.y * Math.sin(spinRadians);
    const baseY =
        targetAcceleration.x * Math.sin(spinRadians) + targetAcceleration.y * Math.cos(spinRadians);
    return {
        pitch: (Math.asin(clamp(baseY, -1, 1)) * 180) / Math.PI,
        roll: (Math.atan2(-baseX, targetAcceleration.z) * 180) / Math.PI,
        label: nearest.label,
    };
}

function finishImuInteraction() {
    imuState.gyroX = 0;
    imuState.gyroY = 0;
    imuState.gyroZ = 0;

    const target = nearestImuSnapTarget();
    if (target) {
        imu.model.classList.add("is-snapping");
        imuState.pitch = target.pitch;
        imuState.roll = target.roll;
        imuState.label = target.label;
        window.setTimeout(() => imu.model.classList.remove("is-snapping"), 240);
    } else {
        imuState.label = "Free tilt";
    }

    refreshImuVisual();
    scheduleImuState(true);
}

function updateImuFromPointer(event) {
    if (!imuDrag || event.pointerId !== imuDrag.pointerId) return;

    const nextPitch = normalizeAngle(
        imuDrag.startPitch - (event.clientY - imuDrag.startY) * IMU_DRAG_DEGREES_PER_PIXEL,
    );
    const nextRoll = normalizeAngle(
        imuDrag.startRoll + (event.clientX - imuDrag.startX) * IMU_DRAG_DEGREES_PER_PIXEL,
    );
    const elapsed = Math.max(8, event.timeStamp - imuDrag.lastTime);
    imuState.gyroX = clamp((normalizeAngle(nextPitch - imuState.pitch) * 1000) / elapsed, -1000, 1000);
    imuState.gyroY = clamp((normalizeAngle(nextRoll - imuState.roll) * 1000) / elapsed, -1000, 1000);
    imuState.gyroZ = 0;
    imuState.pitch = nextPitch;
    imuState.roll = nextRoll;
    imuState.label = "Free tilt";
    imuDrag.lastTime = event.timeStamp;

    refreshImuVisual();
    scheduleImuState();
}

async function applyStatus() {
    if (statusRequestInFlight) {
        statusApplyQueued = true;
        return;
    }

    statusRequestInFlight = true;

    try {
        await forwardMessage(statusMessage());
        setConnection("online");
    } catch (error) {
        setConnection("offline");
    } finally {
        statusRequestInFlight = false;
        if (statusApplyQueued) {
            statusApplyQueued = false;
            applyStatus();
        }
    }
}

async function pulseButton(button) {
    if (button.classList.contains("is-pending") || heldKeyboardKeys.has(button.dataset.keyboardKey)) return;

    button.classList.add("is-pending", "is-active");

    try {
        await queueButtonState(button, true);
        setConnection("online");
        await new Promise((resolve) => window.setTimeout(resolve, BUTTON_PULSE_MS));
    } catch (error) {
        setConnection("offline");
    } finally {
        try {
            await queueButtonState(button, false);
            setConnection("online");
        } catch (error) {
            setConnection("offline");
        }
        button.classList.remove("is-pending", "is-active");
    }
}

async function setKeyboardButtonState(button, pressed) {
    button.classList.toggle("is-active", pressed);
    try {
        await queueButtonState(button, pressed);
        setConnection("online");
    } catch (error) {
        setConnection("offline");
    }
}

function releaseKeyboardKey(key) {
    if (!heldKeyboardKeys.delete(key)) return;
    const button = debuggerButtonsByKey.get(key);
    if (button) setKeyboardButtonState(button, false);
}

function scheduleStatusApply(immediate = false) {
    window.clearTimeout(statusApplyTimer);
    if (immediate) {
        applyStatus();
        return;
    }
    statusApplyTimer = window.setTimeout(applyStatus, STATUS_APPLY_DELAY_MS);
}

async function checkConnection() {
    try {
        const response = await fetch("/api/status", { cache: "no-store" });
        const online = response.ok;
        setConnection(online ? "online" : "offline");
        if (online && !simulatorWasOnline) {
            simulatorWasOnline = true;
            applyStatus();
            scheduleImuState(true);
            flushChainState();
        } else if (!online) {
            simulatorWasOnline = false;
        }
    } catch (error) {
        simulatorWasOnline = false;
        setConnection("offline");
    }
}

controls.form.addEventListener("submit", (event) => {
    event.preventDefault();
    scheduleStatusApply(true);
});

controls.form.addEventListener("input", () => {
    refreshValues();
    scheduleStatusApply();
});

controls.form.addEventListener("change", () => scheduleStatusApply());

controls.powerSource.addEventListener("change", () => {
    if (controls.powerSource.value === "battery") {
        controls.inputVoltage.value = 0;
        controls.charging.checked = false;
    } else if (Number(controls.inputVoltage.value) === 0) {
        controls.inputVoltage.value = 5000;
    }
    refreshValues();
});

chain.control.addEventListener("click", (event) => {
    const button = event.target.closest("button");
    if (!button) return;

    const port = button.closest("[data-chain-side]");
    if (!port) return;
    const side = port.dataset.chainSide;
    const devices = chainState[side];

    if (button.dataset.chainAdd && devices.length < MAX_CHAIN_DEVICES_PER_SIDE) {
        const device = { id: nextChainDeviceId, kind: button.dataset.chainAdd };
        if (device.kind === "joystick") Object.assign(device, { x: 0, y: 0, pressed: false });
        if (device.kind === "encoder") Object.assign(device, { value: 42, delta: 1, pressed: false });
        devices.push(device);
        nextChainDeviceId += 1;
    } else if (button.dataset.chainRemove !== undefined) {
        const index = Number(button.dataset.chainRemove);
        if (!Number.isInteger(index) || index < 0 || index >= devices.length) return;
        devices.splice(index);
    } else {
        return;
    }

    renderChainTopology();
    flushChainState();
});

for (const button of debuggerButtons) {
    button.addEventListener("click", () => pulseButton(button));
}

imu.scene.addEventListener("pointerdown", (event) => {
    if (!event.isPrimary || event.button !== 0) return;
    event.preventDefault();
    imu.model.classList.remove("is-snapping");
    imu.scene.classList.add("is-dragging");
    imu.scene.setPointerCapture(event.pointerId);
    imuDrag = {
        pointerId: event.pointerId,
        startX: event.clientX,
        startY: event.clientY,
        startPitch: imuState.pitch,
        startRoll: imuState.roll,
        lastTime: event.timeStamp,
    };
});

imu.scene.addEventListener("pointermove", updateImuFromPointer);

function endImuPointerInteraction(event) {
    if (!imuDrag || event.pointerId !== imuDrag.pointerId) return;
    if (imu.scene.hasPointerCapture(event.pointerId)) imu.scene.releasePointerCapture(event.pointerId);
    imuDrag = undefined;
    imu.scene.classList.remove("is-dragging");
    finishImuInteraction();
}

imu.scene.addEventListener("pointerup", endImuPointerInteraction);
imu.scene.addEventListener("pointercancel", endImuPointerInteraction);

imu.scene.addEventListener("keydown", (event) => {
    const amount = event.shiftKey ? 15 : 5;
    let handled = true;
    switch (event.key) {
        case "ArrowUp":
            imuState.pitch = normalizeAngle(imuState.pitch + amount);
            break;
        case "ArrowDown":
            imuState.pitch = normalizeAngle(imuState.pitch - amount);
            break;
        case "ArrowLeft":
            imuState.roll = normalizeAngle(imuState.roll - amount);
            break;
        case "ArrowRight":
            imuState.roll = normalizeAngle(imuState.roll + amount);
            break;
        default:
            handled = false;
            break;
    }
    if (!handled) return;

    event.preventDefault();
    heldImuKeys.add(event.key);
    imu.model.classList.remove("is-snapping");
    imuState.gyroX = 0;
    imuState.gyroY = 0;
    imuState.gyroZ = 0;
    imuState.label = "Free tilt";
    refreshImuVisual();
    scheduleImuState();
});

imu.scene.addEventListener("keyup", (event) => {
    if (!heldImuKeys.delete(event.key)) return;
    event.preventDefault();
    if (heldImuKeys.size === 0) finishImuInteraction();
});

imu.vertical.addEventListener("pointerdown", (event) => {
    imuSpinLastTime = event.timeStamp;
    imuSpinLastValue = imuState.spin;
    imu.model.classList.remove("is-snapping");
});

imu.vertical.addEventListener("input", (event) => {
    const spin = Number(imu.vertical.value);
    const elapsed = Math.max(8, event.timeStamp - (imuSpinLastTime ?? event.timeStamp));
    imuState.gyroX = 0;
    imuState.gyroY = 0;
    imuState.gyroZ = clamp(((spin - imuSpinLastValue) * 1000) / elapsed, -1000, 1000);
    imuState.spin = spin;
    imuState.label = `Vertical tilt · ${spin > 0 ? "+" : ""}${spin}°`;
    imu.verticalOutput.value = `${spin > 0 ? "+" : ""}${spin}°`;
    imuSpinLastTime = event.timeStamp;
    imuSpinLastValue = spin;
    setRangeProgress(imu.vertical);
    refreshImuVisual();
    scheduleImuState();
});

imu.vertical.addEventListener("change", () => {
    const spin = Number(imu.vertical.value);
    const snap = clamp(Math.round(spin / 90) * 90, -180, 180);
    if (Math.abs(spin - snap) <= IMU_SPIN_SNAP_DEGREES) {
        imu.model.classList.add("is-snapping");
        imuState.spin = snap;
        imu.vertical.value = snap;
        imu.verticalOutput.value = `${snap > 0 ? "+" : ""}${snap}°`;
        imuState.label = `Vertical tilt · ${snap > 0 ? "+" : ""}${snap}°`;
        window.setTimeout(() => imu.model.classList.remove("is-snapping"), 240);
    }
    imuState.gyroZ = 0;
    imuSpinLastTime = undefined;
    imuSpinLastValue = imuState.spin;
    setRangeProgress(imu.vertical);
    refreshImuVisual();
    scheduleImuState(true);
});

window.addEventListener("keydown", (event) => {
    const key = event.key.toLowerCase();
    const button = debuggerButtonsByKey.get(key);
    if (
        !button ||
        event.repeat ||
        heldKeyboardKeys.has(key) ||
        event.metaKey ||
        event.ctrlKey ||
        event.altKey ||
        button.classList.contains("is-pending")
    ) {
        return;
    }

    event.preventDefault();
    heldKeyboardKeys.add(key);
    setKeyboardButtonState(button, true);
});

window.addEventListener("keyup", (event) => {
    const key = event.key.toLowerCase();
    if (!heldKeyboardKeys.has(key)) return;
    event.preventDefault();
    releaseKeyboardKey(key);
});

window.addEventListener("blur", () => {
    for (const key of Array.from(heldKeyboardKeys)) releaseKeyboardKey(key);
    if (imuDrag) {
        imuDrag = undefined;
        imu.scene.classList.remove("is-dragging");
        finishImuInteraction();
    }
    if (heldImuKeys.size > 0) {
        heldImuKeys.clear();
        finishImuInteraction();
    }
});

refreshValues();
setRangeProgress(imu.vertical);
refreshImuVisual();
renderChainTopology();
checkConnection();
window.setInterval(checkConnection, 3000);
