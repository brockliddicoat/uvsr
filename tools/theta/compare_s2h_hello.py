"""Compare all Hello outputs against an independently executed original HLSL control."""
import argparse
import json
from pathlib import Path

import numpy as np
from run_agfx_copy import require, sha256
from run_s2h_hello import NAMES


def compare(candidate, reference):
    actual_run = json.loads((candidate / 'result.json').read_text())
    source_run = json.loads((reference / 'result.json').read_text())
    require(actual_run.get('status') == 'pass' and actual_run.get('required') == actual_run.get('executed') == 12,
            'incomplete candidate execution')
    require(source_run.get('status') == 'pass' and source_run.get('required') == source_run.get('executed') == 6,
            'incomplete original-source execution')
    require(source_run.get('validation_inserted') is True and source_run.get('validation_diagnostics') == [],
            'reference validation missing')
    original = source_run['native']
    require(original['shader']['language'] == 'HLSL'
            and original['shader']['source_pin'] == 'd6f98b7d67da802053cd9c702082fa741dec42e7', 'wrong reference')
    expected_rows = {row['case_id']: row for row in original['cases']}
    require(set(expected_rows) == {f's2h.hello.{name}.opt3' for name in NAMES}, 'missing reference cases')
    require([run['level'] for run in actual_run['runs']] == [0, 3], 'missing optimization level')
    results = []
    for run in actual_run['runs']:
        level = run['level']
        rows = run['native']['cases']
        require([row['case_id'] for row in rows] == [f's2h.hello.{name}.opt{level}' for name in NAMES], 'wrong candidate cases')
        for name, row in zip(NAMES, rows):
            expected = expected_rows[f's2h.hello.{name}.opt3']
            require(row['root_sha256'] == expected['root_sha256'], 'source and candidate inputs differ')
            require(row['output'] == row['case_id'] + '.rgba8' and expected['output'] == expected['case_id'] + '.rgba8', 'unexpected output path')
            a = (candidate / f'opt{level}' / row['output']).read_bytes()
            b = (reference / expected['output']).read_bytes()
            require(len(a) == len(b) == 800 * 600 * 4 and sha256(a) == row['sha256']
                    and sha256(b) == expected['sha256'], 'missing or changed capture')
            av = np.frombuffer(a, dtype=np.uint8).reshape(600, 800, 4)
            bv = np.frombuffer(b, dtype=np.uint8).reshape(600, 800, 4)
            diff = np.abs(av.astype(np.int16) - bv.astype(np.int16))
            mismatches = np.argwhere(diff)
            first = None
            if len(mismatches):
                y, x, channel = map(int, mismatches[0])
                first = dict(x=x, y=y, channel=channel, actual=int(av[y, x, channel]), expected=int(bv[y, x, channel]))
            results.append(dict(case_id=row['case_id'], status='pass' if first is None else 'fail',
                                actual_sha256=sha256(a), expected_sha256=sha256(b),
                                differing_pixels=int(np.any(diff, axis=2).sum()), differing_channels=len(mismatches),
                                max_channel_error=int(diff.max()), first_difference=first))
    passed = sum(row['status'] == 'pass' for row in results)
    return dict(schema_version=1, case_id='s2h.hello.source-agreement', status='pass' if passed == 12 else 'fail',
                required=12, executed=len(results), passed=passed, comparisons=results,
                candidate_record_sha256=sha256((candidate / 'result.json').read_bytes()),
                reference_record_sha256=sha256((reference / 'result.json').read_bytes()),
                reference_payload_sha256=original['shader']['payload_sha256'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('candidate', 'reference', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    report = dict(status='fail', required=12, executed=None)
    try:
        report = compare(args.candidate.resolve(strict=True), args.reference.resolve(strict=True))
    except (OSError, ValueError, KeyError, TypeError) as error:
        report['error'] = str(error)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: report.get(k) for k in ('status', 'required', 'executed', 'passed', 'error')}))
    return report['status'] != 'pass'


if __name__ == '__main__':
    raise SystemExit(main())
