"""Close only the task-owned verification editor; saved scene is already verified."""
import json
import os
from pathlib import Path
import unreal

out = Path('S:/UE_WorkSpace/SilverChoir/Saved/BaseSandboxSetup')
ready = json.loads((out / 'ready.json').read_text(encoding='utf8'))
assert os.getpid() == ready['pid'] == 41820, 'Refuse to close a different editor'
assert json.loads((out / 'review_result.json').read_text(encoding='utf8'))['ok']
assert json.loads((out / 'disk_validation.json').read_text(encoding='utf8'))['ok']
(out / 'ready.json').write_text(json.dumps({'pid': os.getpid(), 'ready': False, 'finished': True}), encoding='utf8')
unreal.EditorPythonScripting.set_keep_python_script_alive(False)
unreal.SystemLibrary.quit_editor()
