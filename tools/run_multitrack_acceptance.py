"""Run the production-worker reference test and sample total resident memory."""
from pathlib import Path
import argparse
import json
import os
import subprocess
import time
import psutil

p=argparse.ArgumentParser()
for name in ('binary','worker','corpus','probe','precision','qt','report'):
    p.add_argument(name,type=Path)
p.add_argument('--seconds',type=int,default=600)
a=p.parse_args()
env=os.environ.copy()
env['PATH']=str(a.qt.resolve()/'bin')+os.pathsep+env.get('PATH','')
env['QT_PLUGIN_PATH']=str(a.qt.resolve()/'plugins')
env['QT_QPA_PLATFORM']='offscreen'
started=time.monotonic();started_wall=time.time();peak=0
with a.report.with_suffix('.log').open('w',encoding='utf-8') as log:
    process=subprocess.Popen([str(a.binary.resolve()),str(a.worker.resolve()),str(a.corpus.resolve()),
        str(a.probe.resolve()),str(a.seconds),str(a.report.resolve()),str(a.precision.resolve())],env=env,stdout=log,stderr=subprocess.STDOUT)
    owner=psutil.Process(process.pid)
    while process.poll() is None:
        try:
            total=0
            for member in [owner,*owner.children(recursive=True)]:
                try: total+=member.memory_info().rss
                except psutil.NoSuchProcess: pass
            peak=max(peak,total)
        except psutil.NoSuchProcess: pass
        if time.monotonic()-started>a.seconds+180:
            for member in owner.children(recursive=True):
                try: member.kill()
                except psutil.NoSuchProcess: pass
            process.kill()
            raise RuntimeError('Acceptance watchdog expired')
        time.sleep(.025)
    code=process.wait()
if code:
    print(a.report.with_suffix('.log').read_text(encoding='utf-8'))
if not a.report.exists() or a.report.stat().st_mtime < started_wall:
    raise SystemExit(code or 'Acceptance did not write a fresh report')
result=json.loads(a.report.read_text(encoding='utf-8'))
result.update(peak_process_tree_rss_bytes=peak,rss_sampling_interval_ms=25,elapsed_wall_seconds=round(time.monotonic()-started,3))
result['passed']=code==0 and result['passed'] and peak<=1024**3
result['exit_code']=code
a.report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print(json.dumps(result,indent=2))
raise SystemExit(0 if result['passed'] else 1)
