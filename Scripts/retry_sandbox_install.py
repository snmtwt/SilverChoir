import unreal,runpy
from pathlib import Path
root=Path('S:/UE_WorkSpace/SilverChoir/Scripts')
helper=runpy.run_path(str(root/'silver_sandbox_material_portable.py'))
assets=unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
ml=unreal.MaterialEditingLibrary
for path in ('/Game/Meshs/Map/Materials/M_SandboxTerrain','/Game/Meshs/Map/Materials/M_SandboxWater'):
    mat=unreal.load_asset(path)
    assert assets.get_metadata_tag(mat,'SilverSandboxRepairOwner')=='SilverSandboxStaticRepairV1'
    active={n.get_path_name() for n in helper['_nodes'](mat)}
    for node in list(ml.get_material_expressions(mat)):
        if str(node.get_editor_property('desc')).startswith('SilverSandboxPortable: '):
            assert node.get_path_name() not in active,'Refusing to clear a connected graph node'
            ml.delete_material_expression(mat,node)
runpy.run_path(str(root/'install_sandbox_static_mesh.py'),run_name='__main__')
