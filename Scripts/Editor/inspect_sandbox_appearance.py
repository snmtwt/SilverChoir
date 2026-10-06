"""Read current sandbox shading and selection assets without saving changes."""
import json
import traceback
from pathlib import Path
import unreal

ROOT = Path('S:/UE_WorkSpace/SilverChoir')
OUT = ROOT / 'Saved/SandboxAppearance'
OUT.mkdir(parents=True, exist_ok=True)
ML = unreal.MaterialEditingLibrary
BASE = '/Game/System/Map/BaseMap/Sandbox'
report = {'materials': {}, 'ok': False}

def props(obj, names):
    result = {}
    for name in names:
        try:
            value = obj.get_editor_property(name)
            result[name] = value if isinstance(value, (bool, float, int, str)) else str(value)
        except Exception as e:
            result[name] = str(e)
    return result

try:
    for name in ['M_BaseSandboxTerrain', 'MI_BaseSandboxTerrain', 'M_BaseSandboxWater', 'MI_BaseSandboxWater', 'M_BaseSandboxGrid']:
        asset = unreal.load_asset(BASE + '/Materials/' + name)
        data = props(asset, ['shading_model', 'material_domain', 'blend_mode', 'parent'])
        data['scalars'] = {str(p): (ML.get_material_instance_scalar_parameter_value(asset, p) if isinstance(asset, unreal.MaterialInstanceConstant)
                                   else ML.get_material_default_scalar_parameter_value(asset, p)) for p in ML.get_scalar_parameter_names(asset)}
        data['vectors'] = {str(p): str(ML.get_material_instance_vector_parameter_value(asset, p) if isinstance(asset, unreal.MaterialInstanceConstant)
                                   else ML.get_material_default_vector_parameter_value(asset, p)) for p in ML.get_vector_parameter_names(asset)}
        if isinstance(asset, unreal.Material):
            data['outputs'] = {str(p): str(ML.get_material_property_input_node(asset, p)) for p in
                [unreal.MaterialProperty.MP_BASE_COLOR, unreal.MaterialProperty.MP_EMISSIVE_COLOR, unreal.MaterialProperty.MP_ROUGHNESS,
                 unreal.MaterialProperty.MP_SPECULAR, unreal.MaterialProperty.MP_OPACITY]}
            data['nodes'] = [dict(type=n.get_class().get_name(), name=n.get_name(), **props(n, ['desc', 'parameter_name', 'default_value', 'code', 'const_a', 'const_b']))
                             for n in ML.get_material_expressions(asset)]
        report['materials'][name] = data
    bp = unreal.load_asset(BASE + '/BP_BaseSandboxTile')
    cdo = unreal.get_default_object(bp.generated_class())
    report['tile_defaults'] = props(cdo, ['edge_decal_material', 'use_edge_decal', 'default_edge_decal_color', 'selected_edge_decal_color',
        'edge_decal_relative_location', 'edge_decal_size', 'edge_decal_border_extent_scale'])
    report['decal_defaults'] = props(cdo.get_editor_property('edge_decal_component'), ['decal_material', 'decal_color', 'sort_order', 'decal_size'])
    cfg = unreal.load_asset(BASE + '/DA_BaseSandboxMap')
    report['map_config'] = props(cfg, ['whole_map_terrain_materials', 'whole_map_terrain_mesh', 'use_custom_stencil_for_map_decals', 'map_decal_receiver_stencil_value'])
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level('/Game/System/Map/BaseMap/L_BaseTemplate')
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    board = next(a for a in actors if isinstance(a, unreal.BaseSandboxMap))
    report['terrain_slots'] = [str(board.get_map_terrain_mesh_component().get_material(i)) for i in range(board.get_map_terrain_mesh_component().get_num_materials())]
    report['lights'] = []
    for actor in actors:
        light = actor.get_component_by_class(unreal.LightComponent)
        if light and actor.get_distance_to(board) < 1200:
            report['lights'].append(dict(label=actor.get_actor_label(), location=str(actor.get_actor_location()),
                **props(light, ['intensity', 'intensity_units', 'light_color', 'attenuation_radius', 'indirect_lighting_intensity'])))
    report['postprocess'] = []
    for actor in actors:
        if isinstance(actor, unreal.PostProcessVolume):
            report['postprocess'].append(dict(label=actor.get_actor_label(), **props(actor, ['enabled', 'unbound', 'priority']),
                settings=props(actor.get_editor_property('settings'), ['auto_exposure_method', 'auto_exposure_bias', 'auto_exposure_min_brightness',
                    'auto_exposure_max_brightness', 'bloom_intensity', 'local_exposure_highlight_contrast_scale'])))
    report['ok'] = True
except Exception:
    report['error'] = traceback.format_exc()
finally:
    (OUT / 'inspection.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.log('SANDBOX_APPEARANCE_INSPECT ' + str(report['ok']))
