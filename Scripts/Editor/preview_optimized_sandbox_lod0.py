"""Import only the first Blender result for early visual and coordinate review."""
import json
import runpy
from pathlib import Path
import unreal

helpers = runpy.run_path('S:/UE_WorkSpace/SilverChoir/Scripts/Editor/import_sandbox_lods.py')
source = helpers['_load'](helpers['SOURCE_PATH'])
snapshot = helpers['_snapshot'](source)
filename = Path('S:/UE_WorkSpace/SilverChoir/SourceAssets/SandboxMap/Optimized/lod0.fbx')
assert filename.is_file()
with helpers['_legacy_fbx']():
    candidate = helpers['_import_lod0'](filename)
helpers['_restore_settings'].__globals__['SCREEN_SIZES'] = [1.0]
helpers['_restore_settings'](candidate, source, snapshot)
landmarks = helpers['_compare_surface_landmarks'](source, candidate)
assert unreal.EditorAssetLibrary.save_loaded_asset(candidate, False)
Path('S:/UE_WorkSpace/SilverChoir/Saved/SandboxModelOptimization/lod0_preview.json').write_text(
    json.dumps({'candidate': helpers['_snapshot'](candidate), 'landmarks': landmarks}, indent=2), encoding='utf8')
