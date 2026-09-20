"""Exact Features source comparison. Numeric differences are reported, not waived."""
import argparse
import json
from pathlib import Path

import numpy as np
from run_agfx_copy import require, sha256
from run_s2h_features import NAMES, ROOT, artifact, inputs


def differences(actual, expected, image):
    require(len(actual) == len(expected) and len(actual) > 0, 'incomplete comparison')
    a = np.frombuffer(actual, np.uint8 if image else '<u4')
    b = np.frombuffer(expected, np.uint8 if image else '<u4')
    indices = np.flatnonzero(a != b)
    first = None if not len(indices) else dict(index=int(indices[0]), actual=int(a[indices[0]]), expected=int(b[indices[0]]))
    record = dict(equal=first is None, first_difference=first, differing_values=len(indices),
                  actual_sha256=sha256(actual), expected_sha256=sha256(expected))
    if image:
        require(len(actual) % 4 == 0, 'incomplete RGBA texel')
        delta = np.abs(a.astype(np.int16) - b.astype(np.int16)).reshape(-1, 4)
        record.update(differing_pixels=int(np.count_nonzero(np.any(delta, axis=1))),
                      max_channel_error=int(delta.max()), mean_channel_error=float(delta.mean()),
                      alpha_differences=int(np.count_nonzero(delta[:, 3])))
    return record


def complete(record, level):
    require(record.get('status') == 'pass' and record.get('required') == record.get('executed') == 54,
            'incomplete native source or candidate')
    require(record.get('optimization_level') == level and len(record.get('cases', [])) == 54,
            'wrong optimization level or case count')
    require([r.get('case_id') for r in record['cases']] == [f's2h.features.{n}.opt{level}' for n in NAMES],
            'missing or reordered Features cases')
    require(all(r.get('status') == 'pass' for r in record['cases']), 'failed native case')
    require([r.get('step') for r in record['cases']] == inputs(), 'changed deterministic source inputs')


def validation(record):
    require(record.get('inserted') is True and record.get('diagnostics') == [], 'missing clean native validation')


def compare(candidate, reference):
    candidate_path, reference_path = candidate/'result.json', reference/'result.json'
    actual = json.loads(candidate_path.read_text(encoding='utf-8'))
    control = json.loads(reference_path.read_text(encoding='utf-8'))
    require(actual.get('status') == control.get('status') == 'pass', 'failed execution record')
    require(actual.get('required') == actual.get('executed') == 108, 'incomplete candidate execution')
    require([r.get('level') for r in actual.get('runs', [])] == [0, 3], 'missing candidate optimization levels')
    original = control['native']; complete(original, 3); validation(control['validation'])
    shader = original['shader']
    manifest = json.loads((ROOT/'tests/parity/features-sources.json').read_text(encoding='utf-8'))
    require(shader.get('language') == 'HLSL' and shader.get('source_pin') == manifest['commit'], 'wrong original source')
    frozen = {r['path']: r['sha256'] for r in manifest['sources']}
    require(shader.get('sources') and shader.get('library_sha256'), 'missing source identities')
    for row in shader['sources']:
        require(frozen.get(row['path']) == row['original_sha256'], 'changed source shader')
    for name, digest in shader['library_sha256'].items():
        require(frozen.get('include/'+name) == digest, 'changed source library')
    review_path = reference.parent/'pre-gpu.json'
    require(sha256(review_path.read_bytes()) == control['review_sha256'], 'changed source review')
    review = json.loads(review_path.read_text(encoding='utf-8'))
    require(review.get('status') == 'reviewed-before-dispatch'
            and review.get('executable_sha256') == control['executable_sha256']
            and review.get('host_sources') == original['host_source_sha256']
            and review.get('payload_sha256') == shader['payload_sha256'], 'inconsistent source review')
    rows = []
    for run in actual['runs']:
        level = run['level']; complete(run['native'], level); validation(run['validation'])
        require(run['native']['shader']['language'] == 'Rust', 'candidate is not Rust')
        folder = candidate/f'opt{level}'
        for a, b in zip(run['native']['cases'], original['cases']):
            require(a['previous_mouse'] == b['previous_mouse'], 'different source mouse history')
            row = dict(case_id=a['case_id'], comparisons={})
            program = a['step']['program']
            intermediate = program in ('gather', 'scatter', 'quad', 'font')
            require((a.get('intermediate') is not None) == (b.get('intermediate') is not None) == intermediate,
                    'missing or unexpected dependent pass')
            for key, suffix, size in [('input', '.input', 384), ('post', '.post', 384),
                                      ('image', '.rgba8', 800*600*4),
                                      ('intermediate', '.atlas.rgba8' if program == 'font' else '.before.rgba8', (768*8 if program == 'font' else 800*600)*4)]:
                if key == 'intermediate' and not intermediate: continue
                aa = artifact(a[key], a['case_id']+suffix, size, lambda n: (folder/n).read_bytes())
                bb = artifact(b[key], b['case_id']+suffix, size, lambda n: (reference/n).read_bytes())
                row['comparisons'][key] = differences(aa, bb, key in ('image', 'intermediate'))
            row['status'] = 'pass' if all(r['equal'] for r in row['comparisons'].values()) else 'fail'
            rows.append(row)
    passed = sum(row['status'] == 'pass' for row in rows)
    return dict(schema_version=1, case_id='s2h.features.source-agreement', status='pass' if passed == 108 else 'fail',
                required=108, executed=len(rows), passed=passed,
                exact_images=sum(r['comparisons']['image']['equal'] for r in rows),
                exact_states=sum(r['comparisons']['post']['equal'] for r in rows), comparisons=rows,
                scope='exact original HLSL comparison, no numeric tolerance or rebaseline',
                candidate_record_sha256=sha256(candidate_path.read_bytes()),
                reference_record_sha256=sha256(reference_path.read_bytes()), reference_payload_sha256=shader['payload_sha256'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('candidate', 'reference', 'output'): parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args(); report = dict(status='fail', required=108, executed=None)
    try: report = compare(args.candidate.resolve(strict=True), args.reference.resolve(strict=True))
    except (OSError, ValueError, KeyError, TypeError) as error: report['error'] = str(error)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({k: report.get(k) for k in ('status', 'required', 'executed', 'passed', 'exact_images', 'exact_states', 'error')}))
    return report['status'] != 'pass'


if __name__ == '__main__':
    raise SystemExit(main())
