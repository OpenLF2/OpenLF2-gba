# SPDX-License-Identifier: MIT
"""Speeds up the converter in the browser without changing a result.

Pyodide's numpy has no BLAS and multiplies the (mostly zero) resampling weights with every source row. Each output
row of sprites.downscale only has a few nonzero weights, so only those are added, in the same ascending order and
float32 arithmetic as the plain loop (zeros add nothing), which gives the same numbers.
"""
import numpy as np

import sprites


def _banded(a, x):
    """a (m, k) mostly zero, x (k, n): a @ x, summing each row's nonzero terms in ascending order."""
    m = a.shape[0]
    rows, cols = np.nonzero(a)
    count = np.bincount(rows, minlength=m)
    width = int(count.max()) if len(rows) else 0
    start = np.cumsum(count) - count
    slot = np.arange(len(rows)) - start[rows]
    idx = np.zeros((m, width), np.intp)
    val = np.zeros((m, width), np.float32)
    idx[rows, slot] = cols
    val[rows, slot] = a[rows, cols]
    acc = np.zeros((m, x.shape[1]), np.float32)
    for j in range(width):
        acc += val[:, j, None] * x[idx[:, j]]
    return acc


def _matmul(a, b):
    if a.dtype == np.float32 and b.dtype == np.float32 and a.ndim == b.ndim == 2:
        if np.count_nonzero(a) * 4 <= a.size:
            return _banded(a, b)
        if np.count_nonzero(b) * 4 <= b.size:
            return _banded(b.T, a.T).T
    return a @ b


def install():
    sprites.matmul = _matmul
