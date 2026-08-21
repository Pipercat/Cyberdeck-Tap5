/**
 * CyberDeck Remote Web UI - vanilla JS, no build step, no framework
 * (Nutzeranforderung 23: "keine unnoetigen Animationen oder riesige
 * Frameworks"). Talks directly to the device's REST/WebSocket API, see
 * docs/remote_protocol.md in the repository root.
 *
 * NOT tested against a real device in the session that wrote this (no
 * network path to hardware) - see the repository's final implementation
 * report. Review before relying on it.
 */
"use strict";

const CHUNK_SIZE = 4096; // muss <= FLASH_CHUNK_MAX_LEN auf dem Geraet sein

const state = {
    host: "",
    token: null,
    manifest: null,     // { chip, files: [{name,address,size}] }
    fileBlobs: [],       // parallel zu manifest.files - Blob je Datei
    ws: null,
    serialWs: null,
    flashing: false,
};

const el = (id) => document.getElementById(id);

function tokenKey(host) { return `cyberdeck:${host}:token`; }

function apiUrl(path) { return `http://${state.host}${path}`; }

async function api(method, path, { json, body, timeout = 10000 } = {}) {
    const headers = {};
    if (state.token) headers["Authorization"] = `Bearer ${state.token}`;
    let payload = body;
    if (json !== undefined) {
        headers["Content-Type"] = "application/json";
        payload = JSON.stringify(json);
    }
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), timeout);
    try {
        const resp = await fetch(apiUrl(path), { method, headers, body: payload, signal: controller.signal });
        const text = await resp.text();
        const data = text ? JSON.parse(text) : {};
        if (!resp.ok) {
            throw new Error(data.message || data.error || `HTTP ${resp.status}`);
        }
        return data;
    } finally {
        clearTimeout(timer);
    }
}

// --- Connect / Pairing -------------------------------------------------

el("connectBtn").addEventListener("click", async () => {
    state.host = el("hostInput").value.trim();
    if (!state.host) return;
    state.token = localStorage.getItem(tokenKey(state.host));

    try {
        const system = await api("GET", "/api/v1/system");
        el("statusLine").textContent = `${system.device} · firmware ${system.firmware}`;
    } catch (e) {
        el("statusLine").textContent = `Connection failed: ${e.message}`;
        return;
    }

    if (!state.token) {
        el("pairRow").classList.remove("hidden");
        return;
    }
    // Token vorhanden - Gueltigkeit ueber einen authentifizierten Endpunkt pruefen
    try {
        await api("GET", "/api/v1/devices/current");
        onAuthenticated();
    } catch (e) {
        el("pairRow").classList.remove("hidden");
    }
});

el("pairBtn").addEventListener("click", async () => {
    const code = el("codeInput").value.trim();
    if (!code) return;
    try {
        const resp = await api("POST", "/api/v1/pair/confirm", { json: { code, client_name: navigator.platform || "web-client" } });
        state.token = resp.client_token;
        localStorage.setItem(tokenKey(state.host), state.token);
        el("pairRow").classList.add("hidden");
        onAuthenticated();
    } catch (e) {
        alert(`Pairing failed: ${e.message}`);
    }
});

function onAuthenticated() {
    el("targetCard").classList.remove("hidden");
    el("flashCard").classList.remove("hidden");
    el("onlineChip").textContent = "ONLINE";
    el("onlineChip").className = "chip success";
    startPolling();
    connectEventsSocket();
}

// --- Polling + WebSocket events -----------------------------------------

async function refreshOnce() {
    try {
        const [network, device, flash] = await Promise.all([
            api("GET", "/api/v1/network"),
            api("GET", "/api/v1/devices/current"),
            api("GET", "/api/v1/flash/status"),
        ]);
        renderNetwork(network);
        renderDevice(device);
        renderFlashStatus(flash);
    } catch (e) {
        // Verbindung ggf. kurz weg - naechster Tick versucht es erneut
    }
}

function startPolling() {
    refreshOnce();
    setInterval(refreshOnce, 2000);
}

function connectEventsSocket() {
    if (state.ws) return;
    const proto = location.protocol === "https:" ? "wss" : "ws";
    const ws = new WebSocket(`${proto}://${state.host}/api/v1/ws?token=${encodeURIComponent(state.token)}`);
    ws.onmessage = (msg) => {
        try {
            const evt = JSON.parse(msg.data);
            handleEvent(evt);
        } catch (e) { /* ignore malformed frames */ }
    };
    ws.onclose = () => { state.ws = null; setTimeout(connectEventsSocket, 3000); };
    state.ws = ws;
}

function handleEvent(evt) {
    if (evt.type === "flash.progress") {
        setProgress(evt.progress, evt.state, evt.file, evt.written, evt.total);
    } else if (evt.type === "flash.completed") {
        setFlashDone(evt.success, evt.error);
    } else if (evt.type === "device.connected" || evt.type === "device.disconnected") {
        refreshOnce();
    }
}

// --- Rendering ------------------------------------------------------------

function renderNetwork(network) {
    const wifi = network.wifi || {};
    const server = network.remote_server || {};
    el("statusLine").innerHTML = `
        <span>${state.host}</span>
        <span>Wi-Fi ${wifi.connected ? wifi.rssi_dbm + " dBm" : "disconnected"}</span>
        <span>Server ${server.running ? "running" : "stopped"} (${server.clients || 0} client(s))</span>
    `;
}

function renderDevice(device) {
    if (!device.connected) {
        el("targetName").textContent = "No target";
        el("targetChip").textContent = "IDLE";
        el("targetChip").className = "chip neutral";
        el("targetDetailRow").textContent = "";
        return;
    }
    el("targetName").textContent = device.product || device.manufacturer || "Unknown";
    el("targetChip").textContent = device.flash_supported ? "READY" : "UNSUPPORTED";
    el("targetChip").className = device.flash_supported ? "chip success" : "chip warning";
    el("targetDetailRow").textContent = `${device.bridge}  ·  ${device.vid}:${device.pid}`;
}

function renderFlashStatus(status) {
    if (state.flashing) return; // waehrend eines aktiven lokalen Uploads treibt der Client selbst den Fortschritt
    el("progressLabel").textContent = status.state;
    el("progressPct").textContent = status.state === "FLASHING" || status.state === "ERASING"
        ? `${status.progress_percent}%` : "";
    el("progressFill").style.width = `${status.progress_percent || 0}%`;
}

// --- Build-dir parsing (flasher_args.json) --------------------------------

el("buildDirInput").addEventListener("change", async (e) => {
    const files = Array.from(e.target.files);
    const argsFile = files.find((f) => f.webkitRelativePath.endsWith("flasher_args.json") ||
                                        f.name === "flasher_args.json");
    if (!argsFile) {
        alert("flasher_args.json not found in selected directory - run 'idf.py build' first.");
        return;
    }
    const argsText = await argsFile.text();
    const args = JSON.parse(argsText);
    const flashFiles = args.flash_files || {};
    const chip = (args.extra_esptool_args || {}).chip || "";

    const manifestFiles = [];
    const blobs = [];
    let ok = true;
    for (const [addr, relPath] of Object.entries(flashFiles)) {
        const match = files.find((f) => f.webkitRelativePath.endsWith("/" + relPath) || f.name === relPath);
        if (!match) {
            ok = false;
            alert(`Referenced binary not found in selection: ${relPath}`);
            break;
        }
        manifestFiles.push({ name: match.name, address: addr, size: match.size });
        blobs.push(match);
    }
    if (!ok) return;

    manifestFiles.sort((a, b) => parseInt(a.address, 16) - parseInt(b.address, 16));
    state.manifest = { chip, files: manifestFiles };
    state.fileBlobs = blobs;

    const total = manifestFiles.reduce((sum, f) => sum + f.size, 0);
    el("fileList").innerHTML = manifestFiles.map((f) =>
        `<div><span>${f.name} @ ${f.address}</span><span>${(f.size / 1024).toFixed(1)} kB</span></div>`
    ).join("") + `<div><strong>Total</strong><strong>${(total / 1024 / 1024).toFixed(2)} MB</strong></div>`;
    el("flashBtn").disabled = false;
});

// --- Flashing --------------------------------------------------------------

function setProgress(pct, stateLabel, file, written, total) {
    el("progressFill").style.width = `${pct}%`;
    el("progressLabel").textContent = `${stateLabel}${file ? " · " + file : ""}`;
    el("progressPct").textContent = `${pct}%  (${(written / 1024).toFixed(0)} / ${(total / 1024 / 1024).toFixed(2)} MB)`;
}

function setFlashDone(success, error) {
    state.flashing = false;
    el("cancelBtn").classList.add("hidden");
    el("flashBtn").disabled = false;
    el("progressLabel").textContent = success ? "SUCCESS" : `ERROR: ${error}`;
    el("progressFill").style.background = success ? "var(--success)" : "var(--danger)";
}

el("flashBtn").addEventListener("click", async () => {
    if (!state.manifest || state.flashing) return;
    state.flashing = true;
    el("flashBtn").disabled = true;
    el("cancelBtn").classList.remove("hidden");
    el("progressFill").style.background = "var(--accent)";

    try {
        await api("POST", "/api/v1/flash/start", { json: state.manifest, timeout: 30000 });

        const total = state.manifest.files.reduce((s, f) => s + f.size, 0);
        let writtenTotal = 0;
        const startTime = Date.now();

        for (let i = 0; i < state.manifest.files.length; i++) {
            const file = state.manifest.files[i];
            const blob = state.fileBlobs[i];
            let offset = 0;
            while (offset < blob.size) {
                const end = Math.min(offset + CHUNK_SIZE, blob.size);
                const chunk = await blob.slice(offset, end).arrayBuffer();
                await api("POST", `/api/v1/flash/chunk?file=${i}&offset=${offset}`, { body: chunk, timeout: 15000 });
                offset = end;
                writtenTotal += chunk.byteLength;

                const pct = Math.round((100 * writtenTotal) / total);
                const elapsedS = (Date.now() - startTime) / 1000;
                const speedKb = elapsedS > 0.1 ? (writtenTotal / 1024) / elapsedS : 0;
                el("progressFill").style.width = `${pct}%`;
                el("progressLabel").textContent = `Writing ${file.name}`;
                el("progressPct").textContent = `${pct}%  ${speedKb.toFixed(0)} kB/s`;
            }
        }

        await api("POST", "/api/v1/flash/finish", { json: {}, timeout: 30000 });
        setFlashDone(true);
    } catch (e) {
        setFlashDone(false, e.message);
    }
});

el("cancelBtn").addEventListener("click", async () => {
    try { await api("POST", "/api/v1/flash/cancel", { json: {} }); } catch (e) { /* ignore */ }
    setFlashDone(false, "Cancelled");
});

// --- Device control ---------------------------------------------------------

el("resetBtn").addEventListener("click", async () => {
    try { await api("POST", "/api/v1/device/reset", { json: {} }); } catch (e) { alert(e.message); }
});

el("bootloaderBtn").addEventListener("click", async () => {
    try {
        const resp = await api("POST", "/api/v1/device/bootloader", { json: {} });
        if (!resp.ok) alert(resp.message || "Manual bootloader entry required.");
    } catch (e) { alert(e.message); }
});

// --- Serial terminal ---------------------------------------------------------

el("serialBtn").addEventListener("click", () => {
    el("serialCard").classList.remove("hidden");
    if (state.serialWs) return;
    const proto = location.protocol === "https:" ? "wss" : "ws";
    const ws = new WebSocket(`${proto}://${state.host}/api/v1/serial?token=${encodeURIComponent(state.token)}&baud=115200`);
    ws.binaryType = "arraybuffer";
    ws.onmessage = (msg) => {
        const text = typeof msg.data === "string" ? msg.data : new TextDecoder().decode(msg.data);
        const term = el("terminal");
        term.textContent += text;
        term.scrollTop = term.scrollHeight;
    };
    ws.onclose = () => { state.serialWs = null; };
    state.serialWs = ws;
});

el("serialSendBtn").addEventListener("click", () => {
    const input = el("serialInput");
    if (state.serialWs && input.value) {
        state.serialWs.send(input.value + "\n");
        input.value = "";
    }
});
el("serialInput").addEventListener("keydown", (e) => {
    if (e.key === "Enter") el("serialSendBtn").click();
});
el("serialClearBtn").addEventListener("click", () => { el("terminal").textContent = ""; });
el("serialCloseBtn").addEventListener("click", () => {
    if (state.serialWs) state.serialWs.close();
    el("serialCard").classList.add("hidden");
});
