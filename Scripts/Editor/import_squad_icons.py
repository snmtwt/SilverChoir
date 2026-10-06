"""Import generated squad emblems as UI textures; do not replace existing artwork."""
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir())
destination = "/Game/System/Map/BaseMap/UI/SceneUI/SquadMeetingRoom/Icons"
for name in ("Raven", "Wolf", "Northstar", "Shield"):
    asset_path = f"{destination}/T_Squad_{name}"
    texture = unreal.load_asset(asset_path)
    if not texture:
        source = root / "Art/UI/SquadIcons" / f"{name}.png"
        assert source.is_file(), source
        task = unreal.AssetImportTask()
        task.filename = str(source)
        task.destination_path = destination
        task.destination_name = f"T_Squad_{name}"
        task.automated = True
        task.save = False
        task.replace_existing = False
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        texture = unreal.load_asset(asset_path)
    assert isinstance(texture, unreal.Texture2D), asset_path
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    texture.set_editor_property("power_of_two_mode", unreal.TexturePowerOfTwoSetting.STRETCH_TO_POWER_OF_TWO)
    texture.set_editor_property("max_texture_size", 512)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_SIMPLE_AVERAGE)
    texture.set_editor_property("filter", unreal.TextureFilter.TF_TRILINEAR)
    texture.set_editor_property("never_stream", True)
    texture.set_editor_property("srgb", True)
    assert unreal.EditorAssetLibrary.save_loaded_asset(texture, False), asset_path
    unreal.log(f"SQUAD_ICON_IMPORTED {asset_path}")
unreal.log("SQUAD_ICONS_IMPORT_OK")
