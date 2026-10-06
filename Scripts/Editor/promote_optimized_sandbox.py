"""Install the structurally and visually reviewed mesh while preserving its asset path."""
import json
import runpy
from pathlib import Path

root = Path('S:/UE_WorkSpace/SilverChoir')
review = json.loads((root / 'Saved/BaseSandboxSetup/LODReview/candidate_final_20260930T230948_434760Z/report.json').read_text(encoding='utf8'))
assert review['ok'] and len(review['captures']) == 8
helpers = runpy.run_path(str(root / 'Scripts/Editor/import_sandbox_lods.py'))
helpers['promote_candidate']('ad6dbf661c4c225b126142f129271eaeba97aeb7510970630269b5129b8c0a57')
