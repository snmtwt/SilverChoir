import unreal,json
from pathlib import Path
aa=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
b=next(a for a in aa.get_all_level_actors() if isinstance(a,unreal.BaseSandboxMap))
c=b.get_map_terrain_mesh_component()
report={'map_props':{k:str(b.get_editor_property(k)) for k in ['use_map_bounds','update_tile_material_bounds_mask','map_bounds','current_map_scale']},'slots':[]}
for i in range(c.get_num_materials()):
    m=c.get_material(i)
    report['slots'].append({'path':m.get_path_name(),'parent':str(m.get_editor_property('parent')),
        'enabled':m.get_scalar_parameter_value('GSM_MapBoundsMaskEnabled'),
        'center':str(m.get_vector_parameter_value('GSM_MapBoundsCenter')),
        'axes':[str(m.get_vector_parameter_value('GSM_MapBoundsAxisX')),str(m.get_vector_parameter_value('GSM_MapBoundsAxisY'))],
        'half':str(m.get_vector_parameter_value('GSM_MapBoundsHalfSize'))})
t=b.get_tile_by_id('L7')
d=t.get_editor_property('edge_decal_component')
report['tile']={k:str(t.get_editor_property(k)) for k in ['edge_decal_material','edge_decal_size','use_edge_decal','runtime_map_scale','runtime_square_tile_size']}
report['decal']={'material':str(d.get_decal_material()),'size':str(d.get_editor_property('decal_size')),'transform':str(d.get_relative_transform())}
for n in ['M_BaseSandboxTerrain','M_BaseSandboxWater','M_BaseSandboxGrid']:
    m=unreal.load_asset('/Game/System/Map/BaseMap/Sandbox/Materials/'+n)
    report[n]={'domain':str(m.get_editor_property('material_domain')),'blend':str(m.get_editor_property('blend_mode')),
        'opacity':str(unreal.MaterialEditingLibrary.get_material_property_input_node(m,unreal.MaterialProperty.MP_OPACITY)),
        'mask':str(unreal.MaterialEditingLibrary.get_material_property_input_node(m,unreal.MaterialProperty.MP_OPACITY_MASK))}
Path('S:/UE_WorkSpace/SilverChoir/Saved/BaseSandboxSetup/visual_inspection.json').write_text(json.dumps(report,indent=2),encoding='utf8')
