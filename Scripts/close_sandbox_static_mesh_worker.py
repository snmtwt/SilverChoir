import os,json
from pathlib import Path
import unreal
out=Path('S:/UE_WorkSpace/SilverChoir/Saved/SandboxStaticMeshRepair')
assert os.getpid()==34608,'Only close the task-owned worker'
assert json.loads((out/'final_validation.json').read_text(encoding='utf8'))['ok']
assert json.loads((out/'preview_result.json').read_text(encoding='utf8'))['ok']
unreal.SystemLibrary.quit_editor()
