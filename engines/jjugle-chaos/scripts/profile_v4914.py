#!/usr/bin/env python3
"""Read-only, standalone UCI search comparison. Never talks to Lichess/PM2.

Example:
python3 scripts/profile_v4914.py --baseline /path/to/v4913 --engine /path/to/v4914 --out results.csv

Each engine is launched as a separate process for each FEN/history fixture.
OwnBook is disabled intentionally to compare search, not opening-repertoire quality.
"""
import argparse
import csv
import hashlib
import re
import subprocess
import sys
import time
from pathlib import Path

# UCI position history from actual rated games; the script stops immediately
# before the move in each label.
FIXTURES = [
    ("RefsTuzm_1h3_black", "h2h3"),
    ("RefsTuzm_4Nc3_black", "h2h3 h7h5 e2e4 d7d5 e4d5 d8d5 b1c3"),
    ("ZsE8zpiS_9Bb5_white", "e2e4 e7e5 g1f3 b8c6 b1c3 g8f6 f3e5 c6e5 d2d4 e5g6 e4e5 f6g8 f2f4 d7d6 f1d3 g8h6"),
    ("ZsE8zpiS_10exd6_white", "e2e4 e7e5 g1f3 b8c6 b1c3 g8f6 f3e5 c6e5 d2d4 e5g6 e4e5 f6g8 f2f4 d7d6 f1d3 g8h6 d3b5 c8d7"),
]

def tokens(line, prefix):
    if not line.startswith(prefix): return {}
    fields=line[len(prefix):].split()
    return dict(zip(fields[0::2],fields[1::2]))

def run_uci(exe, name, moves, mode, ms):
    go = (f"go movetime {ms}" if mode=="movetime" else
          "go wtime 180000 btime 168000 winc 2000 binc 2000")
    cmd=("uci\nsetoption name OwnBook value false\n"
         "setoption name EvalMode value CHAOS\n"
         "setoption name ThinkTimePercent value 75\n"
         f"position startpos moves {moves}\n{go}\nquit\n")
    begin=time.monotonic()
    r=subprocess.run([str(exe)],input=cmd,text=True,stdout=subprocess.PIPE,
                     stderr=subprocess.PIPE,timeout=40)
    wall=time.monotonic()-begin
    if r.returncode!=0:
        raise RuntimeError(f"{name}: {exe} exit={r.returncode}: {r.stderr[-1000:]}")
    lines=r.stdout.splitlines()
    if any('error invalid position' in x for x in lines):
        raise RuntimeError(f"{name}: illegal UCI fixture sequence")
    identity=next((l[len('id name '):] for l in lines if l.startswith('id name ')),'')
    chosen=next((l.split()[1] for l in reversed(lines) if l.startswith('bestmove ')),None)
    if not identity or not chosen:
        raise RuntimeError(f"{name}: expected UCI ID and bestmove, got: {r.stdout[-1500:]}")
    stats={}
    for line in lines:
        for prefix, tag in [("info string tm ","tm"),
                            ("info string qprofile ","qprofile"),
                            ("info string v4914_decision ","decision"),
                            ("info string v495_choice ","legacy_choice")]:
            if line.startswith(prefix):
                stats[tag]=tokens(line,prefix)
    tm=stats.get('tm',{})
    q=stats.get('qprofile',{})
    decision=stats.get('decision',{})
    old=stats.get('legacy_choice',{})
    if not tm:
        raise RuntimeError(f"{name}: time telemetry missing: {r.stdout[-1500:]}")
    return dict(label=name,engine=identity,mode=mode,chosen=chosen,
                objective=decision.get('objective','-'),
                objective_cp=decision.get('objective_cp',old.get('objective_cp','-')),
                chosen_bound=decision.get('chosen_bound',old.get('chosen_bound','-')),
                final_rejected=decision.get('final_check_rejected','-'),
                shallow_risk=decision.get('shallow_risk','-'),
                elapsed_ms=tm.get('elapsed','-'),wall_seconds=f'{wall:.3f}',
                depth=tm.get('depth','-'),attempted_depth=tm.get('attempted_depth','-'),
                nodes=tm.get('nodes','-'),nps=tm.get('nps','-'),
                qnode_percent=tm.get('qnode_percent','-'),
                q_see_calls=q.get('see_calls','-'),
                soft_ms=tm.get('soft','-'),hard_ms=tm.get('hard','-'),
                objective_ms=tm.get('objective_ms','-'),
                style_verify_ms=tm.get('style_verification_ms','-'),
                stop=tm.get('stop','-'))


def sha256(path):
    digest=hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda:f.read(2**20),b''): digest.update(block)
    return digest.hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--engine',type=Path,required=True)
    p.add_argument('--baseline',type=Path)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--movetime-ms',type=int,default=1600)
    p.add_argument('--clock-only',action='store_true')
    args=p.parse_args()
    if args.movetime_ms<200 or args.movetime_ms>10000:
        p.error('--movetime-ms must be 200..10000')
    versions=[('candidate',args.engine)]
    if args.baseline: versions.insert(0,('baseline',args.baseline))
    for role,exe in versions:
        if not exe.is_file(): p.error(f'{role} missing: {exe}')
        print(role,'SHA256',sha256(exe),flush=True)
    rows=[]
    modes=['clock'] if args.clock_only else ['movetime','clock']
    for name,moves in FIXTURES:
        for mode in modes:
            for role,exe in versions:
                row=run_uci(exe,name,moves,mode,args.movetime_ms)
                row['role']=role
                rows.append(row)
                print(f"{name:25} {mode:8} {role:9} {row['chosen']:6} "
                      f"D{row['depth']:>2} {row['elapsed_ms']:>6}ms "
                      f"NPS={row['nps']} obj={row['objective']} "
                      f"rejected={row['final_rejected']}",flush=True)
    args.out.parent.mkdir(parents=True,exist_ok=True)
    with args.out.open('w',newline='',encoding='utf8') as f:
        writer=csv.DictWriter(f,fieldnames=list(rows[0]))
        writer.writeheader();writer.writerows(rows)
    print('PROFILE_OUT='+str(args.out))
    print('PROFILE_RECORDS='+str(len(rows)))

if __name__=='__main__':
    try: main()
    except (subprocess.TimeoutExpired, RuntimeError, OSError) as exc:
        print('PROFILE_FAILED='+str(exc),file=sys.stderr)
        sys.exit(1)
