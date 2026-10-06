"""Read back saved terrain assets and verify project-local dependency closure."""
from pathlib import Path
import unreal,json
out=Path('S:/UE_WorkSpace/SilverChoir/Saved/SandboxStaticMeshRepair')
registry=unreal.AssetRegistryHelpers.get_asset_registry()
assets=unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
mesh=unreal.load_asset('/Game/Meshs/Map/SM_SandboxMap')
water=unreal.load_asset('/Game/Meshs/Map/Materials/MI_SandboxWater')
assert mesh and water
assert mesh.get_editor_property('allow_cpu_access')
assert unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(water,'WaveTimeOverride')==-1.
pending=['/Game/Meshs/Map/SM_SandboxMap','/Game/Meshs/Map/Preview/L_SandboxMapPreview']
seen=set();missing=[]
options=unreal.AssetRegistryDependencyOptions(include_soft_package_references=True,include_hard_package_references=True,include_searchable_names=False,include_soft_management_references=False,include_hard_management_references=False)
while pending:
    path=pending.pop()
    if path in seen:continue
    seen.add(path)
    if not assets.does_asset_exist(path):missing.append(path)
    for dep in registry.get_dependencies(path,options):
        dep=str(dep)
        if dep.startswith('/Game/') and dep not in seen:pending.append(dep)
assert not missing,missing
report={'ok':True,'mesh':mesh.get_path_name(),'triangles':mesh.get_num_triangles(0),
    'sections':mesh.get_num_sections(0),'allow_cpu_access':True,'animated_water':True,
    'project_dependencies':sorted(seen),'missing_dependencies':missing}
(out/'final_validation.json').write_text(json.dumps(report,indent=2),encoding='utf8')
