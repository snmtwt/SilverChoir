import json
import runpy
from pathlib import Path

helpers = runpy.run_path('S:/UE_WorkSpace/SilverChoir/Scripts/Editor/import_sandbox_lods.py')
mesh = helpers['_load'](helpers['SOURCE_PATH'])
snapshot = helpers['_snapshot'](mesh)
Path('S:/UE_WorkSpace/SilverChoir/Saved/SandboxModelOptimization/import_source_snapshot.json').write_text(
    json.dumps(snapshot, indent=2), encoding='utf8')
