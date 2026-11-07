// SPDX-License-Identifier: MIT
// One of the parallel workers started by worker.js. It holds its own Python and runs the conversion tasks it is given
// (tools/web_tasks.py); the last message assembles the results into the output files.
//
// Messages in:  {type: "init", config, python: {name: bytes}, installer}, {type: "task", name}, {type: "assemble", results, files}
// Messages out: {type: "ready"}, {type: "result", name, bytes}, {type: "files", files}, {type: "error", text}

let py;
let config;

const post = (msg, transfer) => self.postMessage(msg, transfer || []);

async function start(m) {
  config = m.config;
  const { loadPyodide } = await import(config.pyodide + "pyodide.mjs");
  py = await loadPyodide({ indexURL: config.pyodide });
  await py.loadPackage(["numpy", "pillow"]);
  py.FS.mkdir("/tools");
  for (const [name, bytes] of Object.entries(m.python)) py.FS.writeFile("/tools/" + name, bytes);
  py.FS.writeFile("/LF2_v2.0a.exe", m.installer);
  py.runPython(`
import sys
sys.path.insert(0, "/tools")
import convert_assets, music, web_fast, web_tasks
web_fast.install()
src = convert_assets.Source("/LF2_v2.0a.exe")
wavs = None
`);
}

// The music is converted by ffmpeg.wasm here, in the worker that runs the "misc" task.
async function convertMusic() {
  const names = py.runPython("music.TRACKS").toJs();
  const args = py.runPython("music.ARGS").toJs();
  const wasm = await (await fetch(config.ffmpegCore + "ffmpeg-core.wasm")).arrayBuffer();
  const { default: createCore } = await import(config.ffmpegCore + "ffmpeg-core.js");
  const core = await createCore({ wasmBinary: new Uint8Array(wasm) });
  core.setLogger(() => {});
  py.FS.mkdir("/wav");
  for (const n of names) {
    core.FS.writeFile(`${n}.wma`, py.runPython(`src.raw("bgm/${n}.wma")`).toJs());
    const rc = core.exec("-v", "error", "-i", `${n}.wma`, ...args, `${n}.wav`);
    if (rc !== 0) throw new Error(`ffmpeg failed on ${n} (code ${rc})`);
    py.FS.writeFile(`/wav/${n}.wav`, core.FS.readFile(`${n}.wav`));
    core.FS.unlink(`${n}.wma`);
    core.FS.unlink(`${n}.wav`);
  }
  py.runPython(`wavs = {n: open(f"/wav/{n}.wav", "rb").read() for n in music.TRACKS}`);
}

async function runTask(name) {
  if (name === "misc") await convertMusic();
  py.globals.set("task_name", name);
  const proxy = py.runPython("web_tasks.run(src, task_name, wavs)");
  const bytes = proxy.toJs();
  proxy.destroy();
  return bytes;
}

function assemble(results, files) {
  py.FS.mkdir("/results");
  for (const [name, bytes] of Object.entries(results)) py.FS.writeFile("/results/" + name.replace(":", "_"), bytes);
  py.runPython(`
import os
results = {}
for n in web_tasks.task_names():
    results[n] = open("/results/" + n.replace(":", "_"), "rb").read()
web_tasks.assemble("/out", results)
`);
  const out = {};
  for (const name of files) out[name] = py.FS.readFile("/out/" + name);
  return out;
}

self.onmessage = async (e) => {
  const m = e.data;
  try {
    if (m.type === "init") {
      await start(m);
      post({ type: "ready" });
    } else if (m.type === "task") {
      const bytes = await runTask(m.name);
      post({ type: "result", name: m.name, bytes }, [bytes.buffer]);
    } else if (m.type === "assemble") {
      const files = assemble(m.results, m.files);
      post({ type: "files", files }, Object.values(files).map((f) => f.buffer));
    }
  } catch (err) {
    post({ type: "error", text: String((err && err.message) || err) });
  }
};
