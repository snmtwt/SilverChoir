"""Create missing editable main-map assets. Never replace existing maps or Blueprints."""
import unreal

ROOT = "/Game/System/Map/GameMainMap"
tools = unreal.AssetToolsHelpers.get_asset_tools()
library = unreal.EditorAssetLibrary

def blueprint(name, parent, widget=False):
    path = ROOT + "/" + name
    if library.does_asset_exist(path):
        return unreal.load_asset(path), False
    factory = unreal.WidgetBlueprintFactory() if widget else unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.load_class(None, "/Script/SilverChoir." + parent))
    bp = tools.create_asset(name, ROOT, None, factory)
    assert bp
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    return bp, True

def save(asset):
    assert library.save_loaded_asset(asset, only_if_is_dirty=False)

controller, fresh_pc = blueprint("BP_GameMainMapPlayerController", "GameMainMapPlayerController")
if fresh_pc:
    save(controller)
state, fresh_state = blueprint("BP_GameMainMapGameState", "GameMainMapGameState")
if fresh_state:
    save(state)
player, fresh_player = blueprint("BP_GameMainMapPlayerState", "GameMainMapPlayerState")
if fresh_player:
    save(player)

# Simple empty ordinary maps establish working soft references without inventing game content.
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
for name in ("L_BaseTemplate", "L_BattleTemplate"):
    path = ROOT + "/Maps/" + name
    if not library.does_asset_exist(path):
        assert levels.new_level(path)
        assert levels.save_current_level()

def map_asset(name, map_id, map_name):
    path = ROOT + "/Data/" + name
    if library.does_asset_exist(path):
        return unreal.load_asset(path)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.MTS_SubMapDataAsset)
    asset = tools.create_asset(name, ROOT + "/Data", None, factory)
    asset.set_editor_property("map_id", map_id)
    asset.set_editor_property("map_name", map_id)
    asset.set_editor_property("map_asset", unreal.load_asset(ROOT + "/Maps/" + map_name))
    save(asset)
    return asset

base = map_asset("DA_BaseMap", "Base", "L_BaseTemplate")
battle = map_asset("DA_BattleExample", "BattleExample", "L_BattleTemplate")

mode, fresh_mode = blueprint("BP_GameMainMapGameMode", "GameMainMapGameMode")
if fresh_mode:
    defaults = unreal.get_default_object(mode.generated_class())
    defaults.set_editor_property("player_controller_class", controller.generated_class())
    defaults.set_editor_property("game_state_class", state.generated_class())
    defaults.set_editor_property("player_state_class", player.generated_class())
    save(mode)
host_path = ROOT + "/GameMainMap"
if not library.does_asset_exist(host_path):
    assert levels.new_level(host_path)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", mode.generated_class())
    assert levels.save_current_level()
unreal.log("GAME_MAIN_MAP_ASSETS_OK: created missing assets without replacing existing content")
