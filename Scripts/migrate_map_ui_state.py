"""Remove obsolete host UI, preserve map widget subclasses, and verify reflected state API."""
import unreal
from pathlib import Path
import shutil
from datetime import datetime
project = Path(unreal.Paths.project_dir())
backup = project / "Saved" / "MapUIStateBackups" / datetime.now().strftime("%Y%m%d-%H%M%S")
source = project / "Content" / "System" / "Map"
shutil.copytree(source, backup / "Map")
library = unreal.EditorAssetLibrary
# Load/recompile subclasses through the native class redirect before removing the obsolete UI.
assets = library.list_assets("/Game/System/Map", recursive=True, include_folder=False)
for path in assets:
    asset = unreal.load_asset(path)
    if isinstance(asset, unreal.Blueprint) and "WBP_GameMainMap" not in path:
        unreal.BlueprintEditorLibrary.compile_blueprint(asset)
        assert library.save_loaded_asset(asset, only_if_is_dirty=False)
controller = unreal.load_asset("/Game/System/Map/GameMainMap/BP_GameMainMapPlayerController")
defaults = unreal.get_default_object(controller.generated_class())
for prop, path in (("base_widget_class", "/Game/System/Map/BaseMap/UI/BP_BaseMapWidget"), ("battle_widget_class", "/Game/System/Map/BattleMap/BP_BattleMapWidget")):
    if not defaults.get_editor_property(prop):
        widget = unreal.load_asset(path)
        assert widget
        defaults.set_editor_property(prop, widget.generated_class())
assert library.save_loaded_asset(controller, only_if_is_dirty=False)
for path in ("/Game/System/Map/GameMainMap/UI/WBP_GameMainMap", "/Game/System/Map/GameMainMap/WBP_GameMainMap"):
    if library.does_asset_exist(path):
        assert library.delete_asset(path)
# Validate the relocated reflected functions and enum on the default object, restoring it afterwards.
state = unreal.get_default_object(unreal.GameMainMapGameState)
try:
    state.set_current_map_type(unreal.GameMainMapType.BASE)
    assert state.is_base_map() and not state.is_battle_map()
    state.set_current_map_type(unreal.GameMainMapType.BATTLE)
    assert state.is_battle_map() and not state.is_base_map()
finally:
    state.set_current_map_type(unreal.GameMainMapType.NONE)
assert not state.is_base_map() and not state.is_battle_map()
unreal.log("MAP_UI_STATE_MIGRATION_OK")
