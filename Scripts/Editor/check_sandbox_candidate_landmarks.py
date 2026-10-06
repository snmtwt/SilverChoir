import json
import runpy
from pathlib import Path

helpers = runpy.run_path('S:/UE_WorkSpace/SilverChoir/Scripts/Editor/import_sandbox_lods.py')
source, candidate = helpers['_load'](helpers['SOURCE_PATH']), helpers['_load'](helpers['CANDIDATE_PATH'])
report = {'source': helpers['_snapshot'](source), 'candidate': helpers['_snapshot'](candidate)}
out = Path('S:/UE_WorkSpace/SilverChoir/Saved/SandboxModelOptimization/candidate_landmarks.json')
out.write_text(json.dumps(report, indent=2), encoding='utf8')
report['landmarks'] = helpers['_compare_surface_landmarks'](source, candidate)
out.write_text(json.dumps(report, indent=2), encoding='utf8')
