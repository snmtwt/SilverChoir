from pathlib import Path
import unreal,json
out=Path('S:/UE_WorkSpace/SilverChoir/Saved/SandboxStaticMeshRepair')
mesh=unreal.load_asset('/Game/Meshs/Map/SM_SandboxMap')
water=unreal.load_asset('/Game/Meshs/Map/Materials/MI_SandboxWater')
terrain=unreal.load_asset('/Game/Meshs/Map/Materials/MI_SandboxTerrain')
assert mesh and water and terrain
assert mesh.get_num_triangles(0)==3066634
assert mesh.get_editor_property('allow_cpu_access')
assert unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(water,'WaveTimeOverride')==-1.
slots=list(mesh.get_editor_property('static_materials'))
assert [s.get_editor_property('material_interface') for s in slots]==[terrain,water]
assert terrain.get_editor_property('parent').get_path_name()=='/Game/Meshs/Map/Materials/M_SandboxTerrain.M_SandboxTerrain'
assert water.get_editor_property('parent').get_path_name()=='/Game/Meshs/Map/Materials/M_SandboxWater.M_SandboxWater'
(out/'disk_validation.json').write_text(json.dumps({'ok':True,'fresh_process':True,'animated_water':True,'material_slots':2,'triangles':3066634,'allow_cpu_access':True},indent=2),encoding='utf8')
