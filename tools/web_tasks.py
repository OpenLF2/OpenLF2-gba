# SPDX-License-Identifier: MIT
"""The conversion split into independent tasks, so the browser can run them in parallel workers.

Every task is a pure function of the installer, so the assembled files are the same however the tasks are spread
over workers (and the same as convert_assets.convert, which runs them one after the other)."""
import pickle

import convert_assets
import stages


def task_names():
    return ["fighters"] + [f"stage:{i}" for i in range(len(stages.STAGES))] + ["misc"]


def run(src, name, wavs=None):
    """Runs one task and returns its result pickled."""
    if name == "fighters":
        result = convert_assets.convert_fighters(src)
    elif name == "misc":
        result = convert_assets.convert_misc(src, wavs)
    else:
        result = stages.build_one(src, int(name.split(":")[1]))
    return pickle.dumps(result)


def assemble(out, results):
    """results: {task name: bytes from run()}. Writes the converter's output files to out."""
    got = {name: pickle.loads(data) for name, data in results.items()}
    st = stages.assemble([got[f"stage:{i}"] for i in range(len(stages.STAGES))])
    convert_assets.write_output(out, got["fighters"], st, got["misc"])
