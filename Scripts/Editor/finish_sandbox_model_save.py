"""Finish the reviewed in-memory promotion after another editor releases its file."""
import json
import os
import runpy
from pathlib import Path
import unreal

ROOT = Path('S:/UE_WorkSpace/SilverChoir')
OUT = ROOT / 'Saved/SandboxModelOptimization'
ready = json.loads((OUT / 'ready.json').read_text(encoding='utf-8'))
assert ready['ready'] and ready['pid'] == os.getpid(), 'Only the optimization worker may resume this save.'
h = runpy.run_path(str(ROOT / 'Scripts/Editor/import_sandbox_lods.py'))
expected_hash = 'ad6dbf661c4c225b126142f129271eaeba97aeb7510970630269b5129b8c0a57'
assert h['_sha'](h['REPORT_PATH']) == expected_hash
report = json.loads(h['REPORT_PATH'].read_text(encoding='utf-8'))
source = h['_load'](h['SOURCE_PATH'])
backup_path = '/Game/Meshs/Map/Optimization/Backups/SM_SandboxMap_Before_20260930T231311Z'
backup = h['_load'](backup_path)
assert h['_snapshot'](backup)['lods'] == report['source']['lods']
assert h['_sha'](ROOT / 'Content/Meshs/Map/SM_SandboxMap.uasset') == h['_sha'](OUT / 'Backup/SM_SandboxMap.uasset'), 'Original disk file changed; review before saving.'
for fbx in report['fbx']:
    assert h['_sha'](fbx['path']) == fbx['sha256']
promoted = h['_validate'](source, report['source'])
expected = dict(report['candidate'], path=report['source']['path'])
assert promoted == expected, 'In-memory asset no longer matches reviewed candidate.'
assert unreal.EditorAssetLibrary.save_loaded_asset(source, False), 'Original asset is still locked.'
h['_write'](OUT / 'promotion_result.json', {
    'promoted': True, 'backup': backup_path, 'asset': promoted,
    'reviewed_report_sha256': expected_hash,
})
unreal.log('Reviewed sandbox model saved successfully to original asset path.')
