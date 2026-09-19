"""Run an isolated benchmark, sample process-tree RSS, and retain raw output."""
from pathlib import Path
import json
import os
import subprocess
import sys
import time
import psutil

binary, backend, corpus, probe, worker, seconds, report, qt = sys.argv[1:]
report = Path(report).resolve()
report.parent.mkdir(parents=True, exist_ok=True)
env = os.environ.copy()
env['PATH'] = str(Path(qt).resolve() / 'bin') + os.pathsep + env.get('PATH', '')
env['QT_PLUGIN_PATH'] = str(Path(qt).resolve() / 'plugins')
env['QT_QPA_PLATFORM'] = 'offscreen'
peak = 0
started = time.monotonic()
with report.with_suffix('.log').open('w', encoding='utf-8') as log:
    process = subprocess.Popen([str(Path(binary).resolve()), backend, str(Path(corpus).resolve()),
                                str(Path(probe).resolve()), str(Path(worker).resolve()), seconds, str(report)],
                               env=env, stdout=log, stderr=subprocess.STDOUT)
    observed = psutil.Process(process.pid)
    while process.poll() is None:
        try:
            family = [observed, *observed.children(recursive=True)]
            rss = 0
            for child in family:
                try:
                    rss += child.memory_info().rss
                except psutil.NoSuchProcess:
                    pass
            peak = max(peak, rss)
        except psutil.NoSuchProcess:
            pass
        if time.monotonic() - started > int(seconds) + 180:
            for child in observed.children(recursive=True):
                child.kill()
            process.kill()
            raise RuntimeError('Prototype watchdog expired')
        time.sleep(.025)
    result = process.wait()
if result != 0:
    print(report.with_suffix('.log').read_text(encoding='utf-8'))
    raise SystemExit(result)
data = json.loads(report.read_text())
data.update(peak_process_tree_rss_bytes=peak, elapsed_wall_seconds=time.monotonic() - started,
            rss_sampling_interval_ms=25)
if backend == 'native':
    data['passed_prototype_targets'] = data['passed_in_process_targets'] and peak <= 1024 ** 3
report.write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')
print(json.dumps(data, indent=2))
