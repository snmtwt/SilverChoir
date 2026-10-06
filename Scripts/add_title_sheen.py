"""Editable native UI font material: intermittent light sweep, original text tint preserved."""
from pathlib import Path
import datetime
import shutil
import unreal

ROOT = "/Game/System/Map/MainMenu/"
project = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
backup = project / "Saved/MainMenuAssetBackups" / datetime.datetime.now().strftime("%Y%m%d-%H%M%S-sheen")
backup.mkdir(parents=True, exist_ok=True)
for relative in ("WBP_MainMenu.uasset", "Materials/M_TitleSheen.uasset", "Materials/MI_TitleSheen_Silver.uasset", "Materials/MI_TitleSheen_Choir.uasset"):
    source = project / "Content/System/Map/MainMenu" / relative
    if source.exists():
        target = backup / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)

tools = unreal.AssetToolsHelpers.get_asset_tools()
edit = unreal.MaterialEditingLibrary
material = unreal.load_asset(ROOT + "Materials/M_TitleSheen") if unreal.EditorAssetLibrary.does_asset_exist(ROOT + "Materials/M_TitleSheen") else None
if not material:
    material = tools.create_asset("M_TitleSheen", ROOT + "Materials", unreal.Material, unreal.MaterialFactoryNew())
assert material
material.set_editor_property("material_domain", unreal.MaterialDomain.MD_UI)
material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
edit.delete_all_material_expressions(material)

def node(cls, x, y):
    return edit.create_material_expression(material, cls, x, y)

uv = node(unreal.MaterialExpressionTextureCoordinate, -650, -200)
clock = node(unreal.MaterialExpressionTime, -650, -60)
parameters = {}
for index, (name, value) in enumerate((("Interval", 5.0), ("Duration", 1.6), ("Gain", 1.5), ("RowOffset", 0.0))):
    parameter = node(unreal.MaterialExpressionScalarParameter, -650, 100 + index * 110)
    parameter.set_editor_property("parameter_name", name)
    parameter.set_editor_property("default_value", value)
    parameters[name] = parameter

sweep = node(unreal.MaterialExpressionCustom, -200, -100)
sweep.set_editor_property("description", "A brief moving highlight; multiply the existing font color only")
sweep.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
inputs = []
for name in ("UV", "Clock", "Interval", "Duration", "Gain", "RowOffset"):
    entry = unreal.CustomInput()
    entry.set_editor_property("input_name", name)
    inputs.append(entry)
sweep.set_editor_property("inputs", inputs)
sweep.set_editor_property("code", """
float cycle = fmod(max(Clock, 0.0), max(Interval, Duration + 0.1));
float center = lerp(-0.25, 1.45, cycle / max(Duration, 0.05));
float distance = abs(UV.x + UV.y * 0.14 + RowOffset - center);
float soft = saturate(1.0 - distance / 0.13);
float core = saturate(1.0 - distance / 0.025);
float light = max(Gain, 0.0) * (0.75 * soft * soft + 0.25 * core);
return float3(1.0 + light, 1.0 + light, 1.0 + light);
""")
assert edit.connect_material_expressions(uv, "", sweep, "UV")
assert edit.connect_material_expressions(clock, "", sweep, "Clock")
for name, expression in parameters.items():
    assert edit.connect_material_expressions(expression, "", sweep, name)
assert edit.connect_material_property(sweep, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
opacity = node(unreal.MaterialExpressionConstant, -200, 300)
opacity.set_editor_property("r", 1.0)
assert edit.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
edit.recompile_material(material)
assert unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)

bp = unreal.load_asset(ROOT + "WBP_MainMenu")
assert bp
for suffix, widget_name, row in (("Silver", "TitleSilver", 0.0), ("Choir", "TitleChoir", .12)):
    path = ROOT + "Materials/MI_TitleSheen_" + suffix
    instance = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if not instance:
        instance = tools.create_asset("MI_TitleSheen_" + suffix, ROOT + "Materials", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    edit.set_material_instance_parent(instance, material)
    edit.set_material_instance_scalar_parameter_value(instance, "RowOffset", row)
    edit.update_material_instance(instance)
    assert unreal.EditorAssetLibrary.save_loaded_asset(instance, only_if_is_dirty=False)
    text = unreal.load_object(None, bp.get_path_name() + ":WidgetTree." + widget_name)
    assert text
    font = text.get_editor_property("font")
    font.set_editor_property("font_material", instance)
    text.set_font(font)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
assert unreal.EditorAssetLibrary.save_loaded_asset(bp, only_if_is_dirty=False)
unreal.log("TITLE_SHEEN_OK: native font materials saved; 1.6s light sweep every 5s, no color/layout changes")
