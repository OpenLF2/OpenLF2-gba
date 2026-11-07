// SPDX-License-Identifier: MIT
// Runs the browser's conversion (web/worker.js) under node, for tools/make_web_site.py.
//
//   node web_convert.mjs INSTALLER OUTDIR MODULESDIR
//
// MODULESDIR is a node_modules parent holding pyodide@0.27.7 and @ffmpeg/core@0.12.10, the versions web/config.json
// loads. Writes the converter's output files (data.bin, ...) to OUTDIR.
import fs from "fs";
import path from "path";
import { createRequire } from "module";
import { fileURLToPath } from "url";

const [installer, outdir, modules] = process.argv.slice(2);
const require = createRequire(path.resolve(modules) + "/");
const { loadPyodide } = require("pyodide");
const toolsDir = path.dirname(fileURLToPath(import.meta.url));

globalThis.self = globalThis;
globalThis.location = { href: "file://" + toolsDir + "/" };
const createCore = require("@ffmpeg/core");

const py = await loadPyodide();
await py.loadPackage(["numpy", "pillow"]);
py.FS.mkdir("/tools");
for (const f of fs.readdirSync(toolsDir)) {
  if (f.endsWith(".py") && f !== "make_web_site.py") py.FS.writeFile("/tools/" + f, fs.readFileSync(path.join(toolsDir, f)));
}
py.FS.writeFile("/LF2_v2.0a.exe", fs.readFileSync(installer));
py.runPython(`
import sys
sys.path.insert(0, "/tools")
import convert_assets, music, web_fast
web_fast.install()
src = convert_assets.Source("/LF2_v2.0a.exe")
`);

const core = await createCore({ wasmBinary: fs.readFileSync(require.resolve("@ffmpeg/core/wasm")) });
core.setLogger(() => {});
const args = py.runPython("music.ARGS").toJs();
py.FS.mkdir("/wav");
for (const n of py.runPython("music.TRACKS").toJs()) {
  core.FS.writeFile(`${n}.wma`, py.runPython(`src.raw("bgm/${n}.wma")`).toJs());
  const rc = core.exec("-v", "error", "-i", `${n}.wma`, ...args, `${n}.wav`);
  if (rc !== 0) throw new Error(`ffmpeg failed on ${n}`);
  py.FS.writeFile(`/wav/${n}.wav`, core.FS.readFile(`${n}.wav`));
}
py.runPython(`
wavs = {n: open(f"/wav/{n}.wav", "rb").read() for n in music.TRACKS}
convert_assets.convert(src, "/out", wavs)
`);
fs.mkdirSync(outdir, { recursive: true });
for (const f of py.FS.readdir("/out")) {
  if (f[0] !== ".") fs.writeFileSync(path.join(outdir, f), py.FS.readFile("/out/" + f));
}
