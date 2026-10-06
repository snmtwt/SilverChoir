"""Create editable player camera assets and synchronize the host GameMode preview default."""
import datetime
from pathlib import Path
import shutil
import unreal

root = "/Game/System/SubSystem/PlayerSubSystem"
tools = unreal.AssetToolsHelpers.get_asset_tools()
library = unreal.EditorAssetLibrary
project = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
backup = project / "Saved/PlayerCameraBackups" / datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
backup.mkdir(parents=True, exist_ok=True)

def save(asset):
    relative = asset.get_path_name().split(".")[0].replace("/Game/", "Content/") + ".uasset"
    source = project / relative
    if source.exists():
        shutil.copy2(source, backup / source.name)
    assert library.save_loaded_asset(asset, only_if_is_dirty=False)

config_path = root + "/DA_PlayerCameraConfig"
config = unreal.load_asset(config_path) if library.does_asset_exist(config_path) else None
if not config:
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.FCS_CameraConfigDataAsset)
    config = tools.create_asset("DA_PlayerCameraConfig", root, None, factory)
    save(config)

pawn_path = root + "/BP_PlayerPawn"
pawn = unreal.load_asset(pawn_path) if library.does_asset_exist(pawn_path) else None
if not pawn:
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.PlayerCameraPawn)
    pawn = tools.create_asset("BP_PlayerPawn", root, None, factory)
    unreal.BlueprintEditorLibrary.compile_blueprint(pawn)
    unreal.get_default_object(pawn.generated_class()).set_editor_property("camera_config", config)
    save(pawn)
assert isinstance(unreal.get_default_object(pawn.generated_class()), unreal.FCS_FreeCameraPawn)
camera_config_before = unreal.get_default_object(pawn.generated_class()).get_editor_property("camera_config")
# Preserve the existing Blueprint and its camera tuning while adding native inventory ownership.
source = project / (pawn_path.replace("/Game/", "Content/") + ".uasset")
if source.exists():
    shutil.copy2(source, backup / (source.stem + "_before_reparent.uasset"))
unreal.BlueprintEditorLibrary.reparent_blueprint(pawn, unreal.PlayerCameraPawn)
unreal.BlueprintEditorLibrary.compile_blueprint(pawn)
camera_defaults = unreal.get_default_object(pawn.generated_class())
assert isinstance(camera_defaults, unreal.PlayerCameraPawn)
assert camera_defaults.get_editor_property("unit_inventory_component") is not None
assert camera_defaults.get_editor_property("camera_config") == camera_config_before
save(pawn)

controller = unreal.load_asset("/Game/System/Map/GameMainMap/BP_GameMainMapPlayerController")
unreal.BlueprintEditorLibrary.compile_blueprint(controller)
assert unreal.get_default_object(controller.generated_class()).get_editor_property("player_inventory_manager") is not None
save(controller)

mode = unreal.load_asset("/Game/System/Map/GameMainMap/BP_GameMainMapGameMode")
defaults = unreal.get_default_object(mode.generated_class())
defaults.set_editor_property("default_pawn_class", pawn.generated_class())
save(mode)
assert defaults.get_editor_property("default_pawn_class") == pawn.generated_class()
unreal.log("PLAYER_CAMERA_ASSETS_OK")
