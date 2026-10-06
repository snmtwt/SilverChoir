"""Install new portable static sandbox assets without replacing the user's import."""
from pathlib import Path
import json,runpy,traceback
import unreal
ROOT=Path('S:/UE_WorkSpace/SilverChoir')
OUT=ROOT/'Saved/SandboxStaticMeshRepair'
OWNER='SilverSandboxStaticRepairV1'
DEST='/Game/Meshs/Map'
ASSETS=unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
ML=unreal.MaterialEditingLibrary
REPORT={'ok':False,'owner':OWNER,'original_import_modified':False}

def duplicate(source,dest):
    if ASSETS.does_asset_exist(dest):
        obj=unreal.load_asset(dest)
        assert ASSETS.get_metadata_tag(obj,'SilverSandboxRepairOwner')==OWNER,'Refusing existing destination: '+dest
        assert isinstance(obj,unreal.Material),'Only pristine task-created masters may be reused'
        assert not any(str(n.get_editor_property('desc')).startswith('SilverSandboxPortable: ') for n in ML.get_material_expressions(obj))
        return obj
    obj=ASSETS.duplicate_asset(source,dest);assert obj,(source,dest)
    ASSETS.set_metadata_tag(obj,'SilverSandboxRepairOwner',OWNER)
    return obj

try:
    registry=unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous(['/Game/SandboxMap','/Game/Meshs/Map'],True)
    originals={}
    for name in ('SandboxNaturalV5_TerrainAndWater_Import','MI_Terrain_NaturalV5_24km','MI_Water_HarborV4'):
        obj=unreal.load_asset(DEST+'/'+name)
        originals[name]={'class':obj.get_class().get_name(),'path':obj.get_path_name()} if obj else None
    REPORT['original_import']=originals
    terrain=duplicate('/Game/SandboxMap/TerrainNaturalV5/M_Terrain_NaturalV5_Master',DEST+'/Materials/M_SandboxTerrain')
    water=duplicate('/Game/SandboxMap/TerrainHarborV4/Water/M_Water_HarborV4_Master',DEST+'/Materials/M_SandboxWater')
    helper=runpy.run_path(str(ROOT/'Scripts/silver_sandbox_material_portable.py'),run_name='portable_helper')
    REPORT['materials']=helper['patch_materials'](terrain,water,recompile=True)
    instances={}
    for source,name,master in [('/Game/SandboxMap/TerrainNaturalV5/MI_Terrain_NaturalV5_24km','MI_SandboxTerrain',terrain),
        ('/Game/SandboxMap/TerrainHarborV4/Water/MI_Water_HarborV4','MI_SandboxWater',water)]:
        instance=duplicate(source,DEST+'/Materials/'+name)
        ML.set_material_instance_parent(instance,master);ML.update_material_instance(instance)
        instances[name]=instance
        assert ASSETS.save_loaded_asset(master) and ASSETS.save_loaded_asset(instance)
    mesh=duplicate('/Game/SandboxMap/Exports/NaturalV5/SM_SandboxNaturalV5_Combined',DEST+'/SM_SandboxMap')
    assert mesh.get_num_triangles(0)==3066634 and mesh.get_num_sections(0)==2
    slots=list(mesh.get_editor_property('static_materials'));assert len(slots)==2
    names={'MI_Terrain_NaturalV5_24km':'MI_SandboxTerrain','MI_Water_HarborV4':'MI_SandboxWater'}
    bound={}
    for i,slot in enumerate(slots):
        name=str(slot.get_editor_property('imported_material_slot_name'))
        assert name in names,name
        material=instances[names[name]];mesh.set_material(i,material);bound[i]=material.get_path_name()
    mesh.set_editor_property('allow_cpu_access',True)
    editor=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    assert not editor.get_nanite_settings(mesh).get_editor_property('enabled')
    sections=[]
    for index in range(2):
        slot=editor.get_lod_material_slot(mesh,0,index)
        is_water='MI_SandboxWater' in bound[slot]
        if is_water:
            if editor.is_section_collision_enabled(mesh,0,index):editor.enable_section_collision(mesh,False,0,index)
            if editor.is_section_cast_shadow_enabled(mesh,0,index):editor.enable_section_cast_shadow(mesh,False,0,index)
        sections.append({'section':index,'material':bound[slot],
            'cast_shadow':editor.is_section_cast_shadow_enabled(mesh,0,index),
            'collision':editor.is_section_collision_enabled(mesh,0,index)})
    assert ASSETS.save_loaded_asset(mesh)
    REPORT.update(ok=True,mesh=mesh.get_path_name(),triangles=mesh.get_num_triangles(0),
        render_vertices=editor.get_number_verts(mesh,0),sections=sections,
        allow_cpu_access=mesh.get_editor_property('allow_cpu_access'),nanite_enabled=False,
        placement='Translation, rotation, positive nonuniform scale supported; original authored vertices preserved')
except Exception:
    REPORT['error']=traceback.format_exc();unreal.log_error(REPORT['error']);raise
finally:
    (OUT/'install_result.json').write_text(json.dumps(REPORT,indent=2),encoding='utf8')
