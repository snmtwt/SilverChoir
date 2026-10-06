"""Task-owned editor for reversible sandbox mesh optimization and render QA."""
import json
import os
import time
import traceback
from pathlib import Path
import unreal

ROOT = Path('S:/UE_WorkSpace/SilverChoir')
OUT = ROOT / 'Saved/SandboxModelOptimization'
OUT.mkdir(parents=True, exist_ok=True)
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
last_id = None
busy = False

def tick(delta):
    global last_id, busy
    if busy or not (OUT / 'job.json').exists():
        return
    try:
        job = json.loads((OUT / 'job.json').read_text(encoding='utf-8-sig'))
    except (OSError, ValueError):
        return
    if job['id'] == last_id:
        return
    last_id = job['id']
    busy = True
    result = {'id': last_id, 'pid': os.getpid(), 'ok': False, 'started': time.time()}
    try:
        script = Path(job['script']).resolve()
        assert script.is_relative_to((ROOT / 'Scripts').resolve())
        exec(compile(script.read_text(encoding='utf-8-sig'), str(script), 'exec'),
             {'__name__': '__main__', '__file__': str(script)})
        result['ok'] = True
    except Exception:
        result['error'] = traceback.format_exc()
        unreal.log_error(result['error'])
    finally:
        result['finished'] = time.time()
        (OUT / 'job_result.json').write_text(json.dumps(result, indent=2), encoding='utf8')
        busy = False

handle = unreal.register_slate_post_tick_callback(tick)
(OUT / 'ready.json').write_text(json.dumps({'pid': os.getpid(), 'ready': True}), encoding='utf8')
