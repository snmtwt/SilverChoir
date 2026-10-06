"""Seed the scene UI registry, preserving existing user mappings and Blueprint graphs."""
from pathlib import Path
from datetime import datetime
import shutil
import unreal

root = Path(unreal.Paths.project_dir())
source = root / "Content/System/Map/BaseMap/UI"
backup = root / "Saved/BaseUIBackups" / datetime.now().strftime("%Y%m%d-%H%M%S-registry")
shutil.copytree(source, backup)
bp = unreal.load_asset("/Game/System/Map/BaseMap/UI/BP_BaseMapWidget")
assert bp
defaults = unreal.get_default_object(bp.generated_class())
registry = defaults.get_editor_property("scene_ui_classes")
for room in ("SquadMeetingRoom", "CommanderOffice", "OperationsCommandRoom", "PersonnelPreparationRoom"):
    child = unreal.load_asset("/Game/System/Map/BaseMap/UI/SceneUI/" + room + "/WBP_" + room)
    assert child
    unreal.BlueprintEditorLibrary.compile_blueprint(child)
    tag = unreal.get_default_object(child.generated_class()).get_editor_property("scene_tag")
    if tag not in registry:
        registry[tag] = child.generated_class()
    assert unreal.EditorAssetLibrary.save_loaded_asset(child, only_if_is_dirty=False)
defaults.set_editor_property("scene_ui_classes", registry)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
assert unreal.EditorAssetLibrary.save_loaded_asset(bp, only_if_is_dirty=False)
unreal.log("BASE_SCENE_UI_REGISTRY_OK entries=" + str(len(registry)))
