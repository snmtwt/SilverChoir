"""Local task worker for scene generation and GPU verification in a separate editor."""
import json, os, time, traceback
from pathlib import Path
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)

ROOT = Path('S:/UE_WorkSpace/SilverChoir')
OUT = ROOT / 'Saved/BaseSandboxSetup'
OUT.mkdir(parents=True, exist_ok=True)
last_id = None
busy = False
def tick(delta):
    global last_id, busy
    if busy or not (OUT/'job.json').is_file(): return
    try: job = json.loads((OUT/'job.json').read_text(encoding='utf-8-sig'))
    except (OSError, ValueError): return
    if job['id'] == last_id: return
    last_id = job['id']; busy = True
    result = {'id':last_id, 'ok':False, 'pid':os.getpid(), 'started':time.time()}
    try:
        path = Path(job['script']).resolve()
        assert path.is_relative_to((ROOT/'Scripts').resolve())
        exec(compile(path.read_text(encoding='utf-8-sig'), str(path), 'exec'), {'__name__':'__main__','__file__':str(path)})
        result['ok'] = True
    except Exception:
        result['error'] = traceback.format_exc(); unreal.log_error(result['error'])
    finally:
        result['finished'] = time.time()
        (OUT/'job_result.json').write_text(json.dumps(result,indent=2),encoding='utf8')
        busy = False
handle = unreal.register_slate_post_tick_callback(tick)
(OUT/'ready.json').write_text(json.dumps({'pid':os.getpid(),'ready':True}),encoding='utf8')
