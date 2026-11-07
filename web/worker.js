// SPDX-License-Identifier: MIT
// Builds openlf2.gba in the browser: the installer is converted by the same Python code the Makefile runs
// (tools/*.py in Pyodide, ffmpeg.wasm for the music) and the result is written into the prebuilt ROM.
//
// Messages in:  {type: "start", file?: File}
// Messages out: {type: "status", text, progress?}, {type: "need-file", reason}, {type: "done", rom}, {type: "error", text}

const post = (msg, transfer) => self.postMessage(msg, transfer || []);
const status = (text, progress) => post({ type: "status", text, progress });

let config, manifest;

async function getJson(url) {
  const r = await fetch(url);
  if (!r.ok) throw new Error(`${url}: HTTP ${r.status}`);
  return r.json();
}

async function getBytes(url, label, expected) {
  const r = await fetch(url);
  if (!r.ok) throw new Error(`${label}: HTTP ${r.status}`);
  const total = Number(r.headers.get("content-length")) || expected || 0;
  const chunks = [];
  let got = 0;
  const reader = r.body.getReader();
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    chunks.push(value);
    got += value.length;
    if (total && got <= total) status(`${label}: ${(got / 1048576).toFixed(1)} of ${(total / 1048576).toFixed(1)} MiB`, got / total);
  }
  const out = new Uint8Array(got);
  let at = 0;
  for (const c of chunks) { out.set(c, at); at += c.length; }
  return out;
}

async function sha256(bytes) {
  const d = new Uint8Array(await crypto.subtle.digest("SHA-256", bytes));
  return Array.from(d, (b) => b.toString(16).padStart(2, "0")).join("");
}

async function gunzip(bytes) {
  const stream = new Blob([bytes]).stream().pipeThrough(new DecompressionStream("gzip"));
  return new Uint8Array(await new Response(stream).arrayBuffer());
}

async function fetchInstaller() {
  let last = "";
  for (const url of config.installerUrls) {
    try {
      return await getBytes(url, "Downloading the installer", config.installerSize);
    } catch (e) {
      last = `${url}: ${e.message}`;
    }
  }
  throw Object.assign(new Error(last), { needFile: true });
}

async function loadInstaller(file) {
  if (file) return new Uint8Array(await file.arrayBuffer());
  return fetchInstaller();
}

// Starts the task workers, each with its own Python, and runs the conversion tasks on them as they become free.
async function convertInParallel(installer) {
  const count = Math.max(1, Math.min(4, (navigator.hardwareConcurrency || 2) - 1));
  const python = {};
  await Promise.all(manifest.python.map(async (name) => {
    python[name] = new Uint8Array(await (await fetch("py/" + name)).arrayBuffer());
  }));
  const workers = [];
  const stop = () => workers.forEach((w) => w.terminate());
  try {
    status(`Starting ${count} worker${count > 1 ? "s" : ""}…`, 0);
    const names = manifest.tasks;
    const queue = names.slice();
    const results = {};
    await new Promise((resolve, reject) => {
      const next = (worker) => {
        const name = queue.shift();
        if (name) worker.postMessage({ type: "task", name });
        else if (Object.keys(results).length === names.length) resolve();
      };
      for (let i = 0; i < count; i++) {
        const worker = new Worker(new URL("task-worker.js", import.meta.url), { type: "module" });
        workers.push(worker);
        worker.onerror = (e) => reject(new Error(e.message || "worker failed"));
        worker.onmessage = (e) => {
          const m = e.data;
          if (m.type === "error") reject(new Error(m.text));
          else if (m.type === "ready") next(worker);
          else if (m.type === "result") {
            results[m.name] = m.bytes;
            const done = Object.keys(results).length;
            status(`Converting the game data (${done} of ${names.length} parts done)…`, done / names.length);
            next(worker);
          }
        };
        worker.postMessage({ type: "init", config, python, installer });
      }
    });
    status("Assembling the ROM…");
    const files = await new Promise((resolve, reject) => {
      const worker = workers[0];
      worker.onerror = (e) => reject(new Error(e.message || "worker failed"));
      worker.onmessage = (e) => {
        if (e.data.type === "files") resolve(e.data.files);
        else if (e.data.type === "error") reject(new Error(e.data.text));
      };
      const wanted = manifest.blobs.map((b) => b.name).concat(manifest.headers.map((h) => h.name));
      worker.postMessage({ type: "assemble", results, files: wanted });
    });
    return files;
  } finally {
    stop();
  }
}

function applyFixups(name, blob) {
  const fix = manifest.fixups && manifest.fixups[name];
  if (!fix) return blob;
  for (let i = 0; i < fix.length; i += 2) blob[fix[i]] = fix[i + 1];
  return blob;
}

async function build(file) {
  config = await getJson("config.json");
  manifest = await getJson("manifest.json");

  const installerPromise = loadInstaller(file);
  const templatePromise = getBytes("template.bin.gz", "Loading the ROM template", 0).then(gunzip);
  const installer = await installerPromise;
  status("Checking the installer…");
  if (installer.length !== config.installerSize || (await sha256(installer)) !== config.installerSha256) {
    throw Object.assign(new Error("This is not the 29,586,263-byte LF2_v2.0a.exe installer."), { needFile: !!file });
  }
  const rom = await templatePromise;

  // The boot logo is not part of the template; it is read from devkitPro's gbafix.c.
  const source = await (await fetch(config.logoUrl)).text();
  const body = source.match(/\/\/ logo\s*\n\s*\{([^}]*)\}/)[1];
  const logo = Uint8Array.from(body.match(/0x[0-9A-Fa-f]{2}/g), (v) => parseInt(v, 16));
  if (logo.length !== 156 || (await sha256(logo)) !== manifest.logo.sha256) throw new Error("Could not get the boot logo.");
  rom.set(logo, manifest.logo.offset);

  const files = await convertInParallel(installer);
  for (const blob of manifest.blobs) {
    const data = applyFixups(blob.name, files[blob.name]);
    if (data.length !== blob.size || (await sha256(data)) !== blob.sha256) {
      throw new Error(`${blob.name} does not match the expected build output.`);
    }
    rom.set(data, blob.offset);
  }
  for (const hdr of manifest.headers) {
    if ((await sha256(files[hdr.name])) !== hdr.sha256) throw new Error(`${hdr.name} does not match the prebuilt code.`);
  }
  return rom;
}

self.onmessage = async (e) => {
  if (e.data.type !== "start") return;
  try {
    const rom = await build(e.data.file);
    post({ type: "done", rom }, [rom.buffer]);
  } catch (err) {
    if (err.needFile) post({ type: "need-file", reason: err.message });
    else post({ type: "error", text: String(err && err.message || err) });
  }
};
