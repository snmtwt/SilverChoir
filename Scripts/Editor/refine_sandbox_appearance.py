"""Install scene-local exposure compensation and selectable grid-border material.

Run with UnrealEditor-Cmd -run=pythonscript. Backups precede any asset saves.
Original mesh/material assets, room lighting and global exposure are untouched.
"""
import datetime
import json
from pathlib import Path
import runpy
import shutil
import traceback
import unreal

ROOT = Path('S:/UE_WorkSpace/SilverChoir')
OUT = ROOT / 'Saved/SandboxAppearance'
BASE = '/Game/System/Map/BaseMap/Sandbox'
MAT = BASE + '/Materials/'
ML = unreal.MaterialEditingLibrary
ASSETS = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
helper = runpy.run_path(str(ROOT / 'Scripts/Editor/base_sandbox_materials.py'))
report = {'ok': False, 'saved': []}

def tone(material):
    label = 'BaseSandbox: Indoor diffuse response'
    existing = [n for n in ML.get_material_expressions(material) if str(n.get_editor_property('desc')) == label]
    if existing:
        return
    source = ML.get_material_property_input_node(material, unreal.MaterialProperty.MP_BASE_COLOR)
    outlet = ML.get_material_property_input_node_output_name(material, unreal.MaterialProperty.MP_BASE_COLOR)
    assert source, 'Missing surface base color'
    gain = helper['_scalar'](material, 'SandboxAlbedoGain', 1)
    multiply = helper['_node'](material, unreal.MaterialExpressionMultiply, 'Indoor diffuse response')
    helper['_wire'](source, multiply, 'A', outlet)
    helper['_wire'](gain, multiply, 'B')
    helper['_property'](multiply, unreal.MaterialProperty.MP_BASE_COLOR)
    # Miniature surfaces need a matte response under the room's fixed exposure.
    specular = helper['_scalar'](material, 'SandboxSpecular', .08)
    helper['_property'](specular, unreal.MaterialProperty.MP_SPECULAR)
    roughness = ML.get_material_property_input_node(material, unreal.MaterialProperty.MP_ROUGHNESS)
    rough_out = ML.get_material_property_input_node_output_name(material, unreal.MaterialProperty.MP_ROUGHNESS)
    floor = helper['_scalar'](material, 'SandboxRoughnessFloor', .75)
    maximum = helper['_node'](material, unreal.MaterialExpressionMax, 'Matte sandbox surface')
    if roughness:
        helper['_wire'](roughness, maximum, 'A', rough_out)
    helper['_wire'](floor, maximum, 'B')
    helper['_property'](maximum, unreal.MaterialProperty.MP_ROUGHNESS)

try:
    backup = OUT / ('Backup_' + datetime.datetime.now().strftime('%Y%m%d_%H%M%S'))
    paths = [MAT + n for n in ['M_BaseSandboxTerrain', 'MI_BaseSandboxTerrain', 'M_BaseSandboxWater',
                                'MI_BaseSandboxWater', 'M_BaseSandboxGrid', 'MI_BaseSandboxGrid']]
    paths += [BASE + '/BP_BaseSandboxTile']
    for path in paths:
        source = ROOT / 'Content' / (path.removeprefix('/Game/') + '.uasset')
        if source.exists():
            dest = backup / source.relative_to(ROOT)
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, dest)
    report['backup'] = str(backup)
    terrain = unreal.load_asset(MAT + 'M_BaseSandboxTerrain')
    water = unreal.load_asset(MAT + 'M_BaseSandboxWater')
    grid = unreal.load_asset(MAT + 'M_BaseSandboxGrid')
    for key, asset in [('terrain_master', terrain), ('water_master', water), ('grid', grid)]:
        helper['_validate_owned'](asset, key)
    tone(terrain)
    tone(water)
    helper['_build_grid'](grid)
    for material in [terrain, water, grid]:
        errors = ML.recompile_material(material)
        assert not errors, str(errors)
    terrain_mi = unreal.load_asset(MAT + 'MI_BaseSandboxTerrain')
    water_mi = unreal.load_asset(MAT + 'MI_BaseSandboxWater')
    for instance, gain, rough in [(terrain_mi, .07, .8), (water_mi, .12, .5)]:
        for parameter, value in [('SandboxAlbedoGain', gain), ('SandboxSpecular', .08), ('SandboxRoughnessFloor', rough)]:
            # UE 5.8's setter always returns false; verify the stored value.
            ML.set_material_instance_scalar_parameter_value(instance, parameter, value)
            assert abs(ML.get_material_instance_scalar_parameter_value(instance, parameter) - value) < .0001, parameter
        ML.update_material_instance(instance)
    grid_mi = unreal.load_asset(MAT + 'MI_BaseSandboxGrid') if ASSETS.does_asset_exist(MAT + 'MI_BaseSandboxGrid') else None
    if not grid_mi:
        grid_mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset('MI_BaseSandboxGrid', MAT.rstrip('/'), unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    ML.set_material_instance_parent(grid_mi, grid)
    for parameter, value in [('BorderWidth', .0035), ('BorderOpacity', .35), ('BorderEmissiveStrength', .015),
                             ('SelectedBorderWidth', .015), ('SelectedBorderOpacity', 1), ('SelectedBorderEmissiveStrength', 4)]:
        ML.set_material_instance_scalar_parameter_value(grid_mi, parameter, value)
        assert abs(ML.get_material_instance_scalar_parameter_value(grid_mi, parameter) - value) < .0001, parameter
    ML.update_material_instance(grid_mi)
    bp = unreal.load_asset(BASE + '/BP_BaseSandboxTile')
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    cdo = unreal.get_default_object(bp.generated_class())
    cdo.modify()
    cdo.set_editor_property('edge_decal_material', grid_mi)
    cdo.set_editor_property('default_edge_decal_color', unreal.LinearColor(.30, .60, .65, 1))
    cdo.set_editor_property('selected_edge_decal_color', unreal.LinearColor(1, .38, .035, 1))
    cdo.set_editor_property('selected_edge_decal_sort_order_offset', 10)
    for asset in [terrain, water, grid, terrain_mi, water_mi, grid_mi, bp]:
        assert ASSETS.save_loaded_asset(asset, only_if_is_dirty=False), asset.get_path_name()
        report['saved'].append(asset.get_path_name())
    report['ok'] = True
except Exception:
    report['error'] = traceback.format_exc()
    unreal.log_error(report['error'])
finally:
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'refinement.json').write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
    unreal.log('SANDBOX_APPEARANCE_REFINED ' + str(report['ok']))
