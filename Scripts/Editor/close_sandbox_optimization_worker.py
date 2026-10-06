"""Close the dedicated task editor without saving temporary scene comparisons."""
import json
import os
from pathlib import Path
import unreal

out = Path('S:/UE_WorkSpace/SilverChoir/Saved/SandboxModelOptimization')
ready = json.loads((out / 'ready.json').read_text(encoding='utf8'))
assert ready['ready'] and ready['pid'] == os.getpid(), 'Not the task-owned editor'
(out / 'ready.json').write_text(json.dumps({'pid': os.getpid(), 'ready': False}), encoding='utf8')
unreal.EditorPythonScripting.set_keep_python_script_alive(False)
unreal.SystemLibrary.quit_editor()
