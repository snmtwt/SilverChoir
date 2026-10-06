"""Import the supplied transparent Val symbol without changing its artwork."""
from pathlib import Path
import shutil
import unreal

root = Path(unreal.Paths.project_dir())
source = root / "SourceAssets/UI/val-currency-symbol-white.png"
source.parent.mkdir(parents=True, exist_ok=True)
if not source.exists():
    shutil.copy2(root / "Docs/SilverChoirGameDesign/Assets/val-currency-symbol-white.png", source)
path = "/Game/System/UIBasic/Textures/T_ValCurrency"
texture = unreal.load_asset(path)
if not texture:
    task = unreal.AssetImportTask()
    task.filename = str(source)
    task.destination_path = "/Game/System/UIBasic/Textures"
    task.destination_name = "T_ValCurrency"
    task.automated = True
    task.save = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = unreal.load_asset(path)
assert texture
texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_SIMPLE_AVERAGE)
texture.set_editor_property("filter", unreal.TextureFilter.TF_TRILINEAR)
texture.set_editor_property("never_stream", True)
assert unreal.EditorAssetLibrary.save_loaded_asset(texture)
unreal.log("BASE_CURRENCY_IMPORT_OK")
