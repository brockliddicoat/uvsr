"""AGFX's unchanged CPU FLIP oracle, with explicit executable/header identity."""
import json
import math
from pathlib import Path
import struct
import subprocess
from build_flip_reference import HEADER_SHA256
from run_agfx_copy import require, sha256


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


SOURCE_THRESHOLD = f32(0.05)


def passed(mean):
    return f32(mean) <= SOURCE_THRESHOLD


def identity(executable):
    executable = Path(executable).resolve(strict=True)
    record = json.loads(executable.with_name('flip_reference.identity.json').read_text())
    require(record['schema_version'] == 1 and record['header_sha256'] == HEADER_SHA256 and
            record['source_commit'] == 'f91b108a111d2ca3ca4b6586b6cb5dd750064fd7' and
            record['source_sha256'] == sha256(Path(__file__).with_name('flip_reference.cpp').read_bytes()) and
            record['executable_sha256'] == sha256(executable.read_bytes()), 'stale or unpinned FLIP reference')
    return record


def evaluate(executable, output, case, expected, actual, width=64, height=64):
    require(len(expected) == len(actual) == width * height * 4, 'FLIP input size mismatch')
    expected_file, actual_file, map_file = [output / (case + suffix) for suffix in ('.expected.rgba8', '.actual.rgba8', '.flip.f32')]
    expected_file.write_bytes(expected)
    actual_file.write_bytes(actual)
    map_file.unlink(missing_ok=True)
    command = [str(Path(executable).resolve()), str(width), str(height), str(expected_file.resolve()),
               str(actual_file.resolve()), str(map_file.resolve())]
    result = subprocess.run(command, capture_output=True, check=True, timeout=30)
    require(not result.stderr, 'FLIP emitted unexpected diagnostics')
    record = json.loads(result.stdout)
    raw = map_file.read_bytes()
    require(record['pixels'] == width * height and len(raw) == width * height * 4, 'incomplete FLIP map')
    values = struct.unpack('<' + str(width * height) + 'f', raw)
    require(all(math.isfinite(v) and 0 <= v <= 1 for v in values), 'invalid FLIP map values')
    require(all(math.isfinite(record[k]) and 0 <= record[k] <= 1 for k in ('mean','maximum')), 'invalid FLIP metrics')
    # The pinned source accumulates and divides in float32, not Python double.
    total = 0.0
    for value in values:
        total = f32(total + value)
    require(abs(f32(total/len(values)) - record['mean']) < 1e-8 and
            abs(max(values) - record['maximum']) < 1e-7, 'FLIP metrics differ from their full map')
    record.update(threshold=0.05, threshold_f32=SOURCE_THRESHOLD, status='pass' if passed(record['mean']) else 'fail',
                  map_sha256=sha256(raw), reference_sha256=sha256(expected), actual_sha256=sha256(actual), command=command)
    return record
