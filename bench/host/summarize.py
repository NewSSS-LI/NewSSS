#!/usr/bin/env python3
"""Collects the results of fb_bench.sh, rdb_bench.sh and paper_fio.sh runs into one
CSV, so the three boards' results can be put side by side.

Usage: summarize.py <run dir> [<run dir> ...] > results.csv
A run dir is one folder made by a script (…_fb, …_rdb or …_fio). Output columns:
  ftl, board_id, run, benchmark, workload, metric, value, unit, run_dir
Units: MB = 10^6 bytes (as in the LearnedFTL paper). db_bench prints "MB/s" in
units of 2^20 bytes; the CSV converts it.
"""
import csv, json, os, re, sys

def meta(d):
    m = {}
    try:
        for line in open(os.path.join(d, 'meta.txt'), encoding='utf-8'):
            k, _, v = line.rstrip('\n').partition('=')
            m[k] = v
    except FileNotFoundError:
        pass
    return m

def fio_rows(d):
    for f in sorted(os.listdir(d)):
        if not f.endswith('.txt') or f.startswith('mkfs'):
            continue
        text = open(os.path.join(d, f), encoding='utf-8', errors='replace').read()
        i = text.find('\n{')
        if i < 0:
            continue
        try:
            # fio writes the JSON block and the normal report into one file.
            job = json.JSONDecoder().raw_decode(text[i + 1:])[0]['jobs'][0]
        except (ValueError, KeyError, IndexError):
            continue
        name = f[:-4]
        for op in ('read', 'write'):
            o = job.get(op, {})
            if not o or o.get('io_bytes', 0) == 0:
                continue
            yield name, f'{op}_bw', o['bw_bytes'] / 1e6, 'MB/s'
            yield name, f'{op}_iops', o['iops'], 'IOPS'
            pct = o.get('clat_ns', {}).get('percentile', {})
            for p, key in (('p50', '50.000000'), ('p99', '99.000000'), ('p99.9', '99.900000')):
                if key in pct:
                    yield name, f'{op}_clat_{p}', pct[key] / 1e6, 'ms'

FB = re.compile(r'IO Summary:\s+(\d+) ops ([\d.]+) ops/s (\d+)/(\d+) rd/wr\s+([\d.]+)mb/s\s+([\d.]+)ms/op')

def fb_rows(d):
    for w in ('fileserver', 'webserver', 'varmail'):
        p = os.path.join(d, f'filebench_{w}.txt')
        if not os.path.exists(p):
            continue
        m = FB.search(open(p, encoding='utf-8', errors='replace').read())
        if not m:
            continue
        yield w, 'ops_per_s', float(m.group(2)), 'ops/s'
        yield w, 'throughput', float(m.group(5)), 'filebench mb/s'
        yield w, 'latency', float(m.group(6)), 'ms/op'

DB = re.compile(r'^(\w+)\s+:\s+([\d.]+) micros/op (\d+) ops/sec ([\d.]+) seconds (\d+) operations;(?:\s+([\d.]+) MB/s)?', re.M)

def rdb_rows(d):
    for b in ('fillseq', 'overwrite', 'readrandom', 'readseq'):
        p = os.path.join(d, f'dbbench_{b}.txt')
        if not os.path.exists(p):
            continue
        for m in DB.finditer(open(p, encoding='utf-8', errors='replace').read()):
            if m.group(1) != b:
                continue
            yield b, 'ops_per_s', float(m.group(3)), 'ops/s'
            yield b, 'micros_per_op', float(m.group(2)), 'us/op'
            yield b, 'operations', int(m.group(5)), 'ops'
            if m.group(6):
                yield b, 'throughput', float(m.group(6)) * 1048576 / 1e6, 'MB/s'
            break

def main(dirs):
    w = csv.writer(sys.stdout)
    w.writerow(['ftl', 'board_id', 'run', 'benchmark', 'workload', 'metric', 'value', 'unit', 'run_dir'])
    for d in dirs:
        m = meta(d)
        run = m.get('run') or d.rstrip('/').rsplit('_', 1)[-1]
        rows = {'fio': fio_rows, 'fb': fb_rows, 'rdb': rdb_rows}.get(run)
        if rows is None:
            print(f'skipped {d}: unknown run type {run}', file=sys.stderr)
            continue
        bench = {'fio': 'fio', 'fb': 'filebench', 'rdb': 'rocksdb'}[run]
        for workload, metric, value, unit in rows(d):
            w.writerow([m.get('ftl', ''), m.get('board_id', ''), run, bench, workload, metric,
                        f'{value:.4g}' if isinstance(value, float) else value, unit, os.path.basename(d.rstrip('/'))])

if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    main(sys.argv[1:])
