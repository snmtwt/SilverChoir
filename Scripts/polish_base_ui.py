"""Update sampling and font materials while preserving the user-authored UI tree."""
from pathlib import Path
from datetime import datetime
import shutil
import unreal

root = Path(unreal.Paths.project_dir())
backup = root / "Saved/BaseUIBackups" / datetime.now().strftime("%Y%m%d-%H%M%S-polish")
for relative in ("System/Map/BaseMap/UI/BP_BaseMapWidget.uasset", "System/UIBasic/Textures/T_ValCurrency.uasset"):
    source = root / "Content" / relative
    target = backup / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)

texture = unreal.load_asset("/Game/System/UIBasic/Textures/T_ValCurrency")
assert texture
texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_SIMPLE_AVERAGE)
texture.set_editor_property("filter", unreal.TextureFilter.TF_TRILINEAR)
texture.set_editor_property("never_stream", True)
assert unreal.EditorAssetLibrary.save_loaded_asset(texture, only_if_is_dirty=False)

library = unreal.EditorAssetLibrary
edit = unreal.MaterialEditingLibrary
directory = "/Game/System/UIBasic/Materials"
parent_path = directory + "/M_TextSheen"
if not library.does_asset_exist(parent_path):
    assert library.duplicate_asset("/Game/System/Map/MainMenu/Materials/M_TitleSheen", parent_path)
parent = unreal.load_asset(parent_path)
assert parent
# Duplicate the validated material graph instead of changing the main-menu material.
assert edit.get_num_material_expressions(parent) >= 8
assert library.save_loaded_asset(parent, only_if_is_dirty=False)
bp = unreal.load_asset("/Game/System/Map/BaseMap/UI/BP_BaseMapWidget")
assert bp
tools = unreal.AssetToolsHelpers.get_asset_tools()
for name, gain, duration in (("TitleText", .42, 2.25), ("CurrencyText", .22, 2.8)):
    asset_name = "MI_Base" + name + "Sheen"
    path = directory + "/" + asset_name
    material = unreal.load_asset(path) if library.does_asset_exist(path) else None
    if not material:
        material = tools.create_asset(asset_name, directory, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    edit.set_material_instance_parent(material, parent)
    for key, value in (("Interval", 7.0), ("Duration", duration), ("Gain", gain)):
        edit.set_material_instance_scalar_parameter_value(material, key, value)
    edit.update_material_instance(material)
    assert library.save_loaded_asset(material, only_if_is_dirty=False)
    widget = unreal.load_object(None, bp.get_path_name() + ":WidgetTree." + name)
    assert widget
    font = widget.get_editor_property("font")
    font.set_editor_property("font_material", material)
    widget.set_font(font)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
assert library.save_loaded_asset(bp, only_if_is_dirty=False)
unreal.log("BASE_UI_SAMPLING_AND_TEXT_OK")
