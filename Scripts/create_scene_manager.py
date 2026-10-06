"""Create an empty, user-editable scene manager Blueprint; never overwrite existing configuration."""
import unreal

path = "/Game/System/SubSystem/SceneSystem/BP_SceneManager"
bp = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
if bp is None:
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.load_class(None, "/Script/SceneManagementSystem.SMS_SceneManager"))
    bp = unreal.AssetToolsHelpers.get_asset_tools().create_asset("BP_SceneManager", "/Game/System/SubSystem/SceneSystem", unreal.Blueprint, factory)
assert bp
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
assert unreal.EditorAssetLibrary.save_loaded_asset(bp, only_if_is_dirty=False)
unreal.log("SCENE_MANAGER_BLUEPRINT_OK")
