#!/usr/bin/env python3
"""Windows/native acceptance runner for QSearch vector versus fixed buffers."""
import argparse
import json
import pathlib
import statistics
import subprocess
import tempfile


def run(binary, network, fixture, destination):
    command = [str(binary), '--network', str(network), '--fixture', str(fixture),
               '--output', str(destination), '--depth', '5', '--time-ms', '1000',
               '--modes', 'nnue', '--eval-warmup', '1', '--eval-iters', '1', '--eval-samples', '1']
    subprocess.run(command, check=True)
    return json.loads(destination.read_text(encoding='utf-8'))


def records(report, kind):
    return {row['name']: row for row in report['search']
            if row['benchmark'] == kind and row['eval_mode'] == 'NNUE'}


def compare(baseline, candidate):
    fixed_depth_mismatches, fixed_time_mismatches = [], []
    output = {}
    for kind, fields, mismatch_list in (
        ('fixed_depth', ('bestmove', 'score_cp', 'completed_depth', 'nodes', 'qnodes'), fixed_depth_mismatches),
        ('fixed_time', ('bestmove', 'score_cp'), fixed_time_mismatches),
    ):
        left, right = records(baseline, kind), records(candidate, kind)
        rows = []
        for name in left:
            if name not in right or any(left[name][field] != right[name][field] for field in fields):
                mismatch_list.append(name)
            rows.append({'name': name, 'baseline': left[name], 'candidate': right.get(name)})
        base_nodes = sum(row['baseline']['nodes'] for row in rows)
        candidate_nodes = sum(row['candidate']['nodes'] for row in rows if row['candidate'])
        base_ms = sum(row['baseline']['elapsed_ms'] for row in rows)
        candidate_ms = sum(row['candidate']['elapsed_ms'] for row in rows if row['candidate'])
        relative = [row['candidate']['nps'] / row['baseline']['nps'] for row in rows
                    if row['candidate'] and row['baseline']['nps'] > 0]
        output[kind] = {'records': rows, 'aggregate': {
            'baseline_nps': base_nodes * 1000 / base_ms,
            'candidate_nps': candidate_nodes * 1000 / candidate_ms,
            'median_relative_nps': statistics.median(relative),
            'baseline_wall_ms': base_ms, 'candidate_wall_ms': candidate_ms}}
    return output, fixed_depth_mismatches, fixed_time_mismatches


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--baseline', type=pathlib.Path, required=True)
    parser.add_argument('--candidate', type=pathlib.Path, required=True)
    parser.add_argument('--network', type=pathlib.Path, required=True)
    parser.add_argument('--fixture', type=pathlib.Path, default=pathlib.Path('tests/data/phase6-search-baseline.fen'))
    parser.add_argument('--repetitions', type=int, default=2)
    parser.add_argument('--output', type=pathlib.Path, default=pathlib.Path('runs/native-qsearch-move-buffer-ab.json'))
    options = parser.parse_args()
    if options.repetitions < 2:
        parser.error('--repetitions must be at least 2 to meet acceptance requirements')
    runs = []
    with tempfile.TemporaryDirectory(prefix='hebichess-move-buffer-') as temporary:
        temporary = pathlib.Path(temporary)
        for repetition in range(options.repetitions):
            for order in ('baseline-first', 'candidate-first'):
                baseline_file = temporary / f'{repetition}-{order}-baseline.json'
                candidate_file = temporary / f'{repetition}-{order}-candidate.json'
                if order == 'baseline-first':
                    baseline = run(options.baseline, options.network, options.fixture, baseline_file)
                    candidate = run(options.candidate, options.network, options.fixture, candidate_file)
                else:
                    candidate = run(options.candidate, options.network, options.fixture, candidate_file)
                    baseline = run(options.baseline, options.network, options.fixture, baseline_file)
                search, depth_mismatches, time_mismatches = compare(baseline, candidate)
                runs.append({'repetition': repetition + 1, 'order': order, 'layout': {
                    'baseline': baseline['qsearch_move_buffer_layout'], 'candidate': candidate['qsearch_move_buffer_layout']},
                    'fixed_depth_mismatches': depth_mismatches, 'fixed_time_mismatches': time_mismatches, 'search': search})
    options.output.parent.mkdir(parents=True, exist_ok=True)
    report = {'schema': 'hebichess-native-qsearch-move-buffer-ab-v1',
              'status': 'PASS' if all(not run['fixed_depth_mismatches'] for run in runs) else 'FAIL',
              'configuration': {'baseline': str(options.baseline), 'candidate': str(options.candidate),
                                'network': str(options.network), 'fixture': str(options.fixture), 'repetitions': options.repetitions},
              'runs': runs}
    options.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'status': report['status'], 'output': str(options.output),
                      'fixed_depth_mismatches': [run['fixed_depth_mismatches'] for run in runs],
                      'fixed_time_mismatches': [run['fixed_time_mismatches'] for run in runs]}, indent=2))
    if report['status'] != 'PASS': raise SystemExit(1)


if __name__ == '__main__':
    main()
