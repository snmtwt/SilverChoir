"""Read the saved mesh in a fresh UE process without modifying any assets."""
import json
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir())
out = root / 'Saved/SandboxModelOptimization'
mesh = unreal.load_asset('/Game/Meshs/Map/SM_SandboxMap')
assert isinstance(mesh, unreal.StaticMesh)
assert mesh.get_num_lods() == 3
counts = [mesh.get_num_triangles(i) for i in range(3)]
assert counts == [499990, 149999, 54172], counts
materials = [s.get_editor_property('material_interface').get_path_name()
             for s in mesh.get_editor_property('static_materials')]
assert materials == ['/Game/Meshs/Map/Materials/MI_SandboxTerrain.MI_SandboxTerrain',
                     '/Game/Meshs/Map/Materials/MI_SandboxWater.MI_SandboxWater']
import_file = mesh.get_editor_property('asset_import_data').get_first_filename()
assert Path(import_file).resolve() == (root / 'SourceAssets/SandboxMap/Optimized/lod0.fbx').resolve()
report = {'ok': True, 'asset': mesh.get_path_name(), 'triangles': counts,
          'materials': materials, 'import_file': import_file,
          'asset_bytes': (root / 'Content/Meshs/Map/SM_SandboxMap.uasset').stat().st_size,
          'assets_modified': False}
(out / 'saved_asset_validation.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
unreal.log('Saved sandbox LODs verified: ' + str(counts))
