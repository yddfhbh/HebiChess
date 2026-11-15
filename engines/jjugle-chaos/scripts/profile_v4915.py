#!/usr/bin/env python3
"""Read-only UCI A/B profiling for JJUGLE Chaos v4.9.14 vs v4.9.15.

Four positions are reconstructed from public PGN, no Lichess API requests.
OwnBook=false: this measures engine search, not repertoire selection.
"""
import argparse
import csv
import hashlib
import subprocess
import time
from pathlib import Path

FIXTURES = [
    ("DE9Q4KMj_12_black", "r1b2rk1/pp2ppbp/2np2p1/3N3q/2P3nP/6P1/PPN1PPB1/1RBQ1RK1 b - - 2 12"),
    ("6CGQ7z5B_35_white", "r4bk1/5pp1/1Bp4p/p2rP2q/P4Q2/1R2K3/2P3PP/1R6 w - - 7 35"),
    ("ZHbRP3f8_31_black", "r3k3/P3p3/2pp3b/4nqp1/4N2r/1P2BPQ1/2P3K1/R3R3 b - - 1 31"),
    ("ZHbRP3f8_33_black", "r3k3/P7/2pp3b/5qp1/7r/1P2BQ2/2P3K1/R3R3 b - - 0 33"),
]

def fields(line, prefix):
    if not line.startswith(prefix): return {}
    a=line[len(prefix):].split()
    return dict(zip(a[::2], a[1::2]))

def sha256(path):
    h=hashlib.sha256()
    with path.open('rb') as file:
        for data in iter(lambda:file.read(1024*1024),b''):
            h.update(data)
    return h.hexdigest()

def query(exe, label, fen, ms):
    cmd=("uci\nsetoption name EvalMode value CHAOS\n"
         "setoption name OwnBook value false\n"
         f"position fen {fen}\ngo movetime {ms}\nquit\n")
    start=time.monotonic()
    result=subprocess.run([str(exe)],input=cmd,text=True,
                          capture_output=True,timeout=max(30,ms/1000+15))
    wall_ms=int((time.monotonic()-start)*1000)
    if result.returncode:
        raise RuntimeError(f"{exe} exited={result.returncode}: {result.stderr[-1000:]}")
    lines=result.stdout.splitlines()
    identity=next((x[8:] for x in lines if x.startswith('id name ')),None)
    chosen=next((x.split()[1] for x in reversed(lines) if x.startswith('bestmove ')),None)
    if not identity or not chosen:
        raise RuntimeError('UCI did not provide identity/bestmove')
    tm,dec,extra={},{},{}
    for line in lines:
        if line.startswith('info string tm '): tm=fields(line,'info string tm ')
        if line.startswith('info string v4914_decision '): dec=fields(line,'info string v4914_decision ')
        if line.startswith('info string v4915_attack_formation '):
            extra=fields(line,'info string v4915_attack_formation ')
    if not tm: raise RuntimeError('Missing search telemetry')
    return dict(label=label, engine=identity, chosen=chosen,objective=dec.get('objective','-'),
                objective_cp=dec.get('objective_cp','-'), wall_ms=wall_ms,
                elapsed_ms=tm.get('elapsed','-'),depth=tm.get('depth','-'),
                nodes=tm.get('nodes','-'),nps=tm.get('nps','-'),
                qnode_percent=tm.get('qnode_percent','-'),
                style_verification_ms=tm.get('style_verification_ms','-'),
                queen_veto=extra.get('queen_veto','-'),
                queen_tempo=extra.get('queen_tempo_selected','-'),
                attack_formation_bonus=extra.get('chosen_formation_bonus','-'))

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--engine',required=True,type=Path)
    p.add_argument('--baseline',required=True,type=Path)
    p.add_argument('--out',required=True,type=Path)
    p.add_argument('--movetime-ms',type=int,default=2400)
    args=p.parse_args()
    if not 500 <= args.movetime_ms <= 10000: p.error('movetime must be 500..10000')
    rows=[]
    for role,exe in [('baseline',args.baseline),('candidate',args.engine)]:
        if not exe.is_file(): p.error(f'{role} not found: {exe}')
        print(f"{role} sha256={sha256(exe)}",flush=True)
    for label,fen in FIXTURES:
        for role,exe in [('baseline',args.baseline),('candidate',args.engine)]:
            row=query(exe,label,fen,args.movetime_ms)
            row['role']=role
            rows.append(row)
            print(f"{label:25} {role:9} best={row['chosen']} depth={row['depth']} "
                  f"elapsed={row['elapsed_ms']}ms nps={row['nps']} ",flush=True)
    args.out.parent.mkdir(parents=True,exist_ok=True)
    with args.out.open('w',newline='',encoding='utf-8') as f:
        writer=csv.DictWriter(f,fieldnames=list(rows[0]))
        writer.writeheader();writer.writerows(rows)
    print(f"PROFILE_CSV={args.out}",flush=True)

if __name__=='__main__':main()
