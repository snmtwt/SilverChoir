import json, runpy, traceback
from pathlib import Path
root=Path('S:/UE_WorkSpace/SilverChoir')
try:
    r=runpy.run_path(str(root/'Scripts/Editor/base_sandbox_materials.py'))['ensure_materials']()
    report=r['report']
except Exception:
    report={'ok':False,'error':traceback.format_exc()}
(root/'Saved/BaseSandboxSetup/materials_result.json').write_text(json.dumps(report,indent=2),encoding='utf8')
