"""Align the right menu group with the user-marked position; preserve vertical layout."""
import datetime
from pathlib import Path
import shutil
import unreal

project = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
bp = unreal.load_asset("/Game/System/Map/MainMenu/UI/WBP_MainMenu")
assert bp
backup = project / "Saved/MainMenuAssetBackups" / datetime.datetime.now().strftime("%Y%m%d-%H%M%S-button-position")
backup.mkdir(parents=True, exist_ok=True)
shutil.copy2(project / "Content/System/Map/MainMenu/UI/WBP_MainMenu.uasset", backup / "WBP_MainMenu.uasset")
for name in ("MenuButtons", "MenuHeading", "MenuHint", "MenuRule"):
    widget = unreal.load_object(None, bp.get_path_name() + ":WidgetTree." + name)
    assert widget, name
    slot = widget.get_editor_property("slot")
    anchors = slot.get_anchors()
    anchors.minimum.x = .90
    anchors.maximum.x = .90
    slot.set_anchors(anchors)
    position = slot.get_position()
    slot.set_position(unreal.Vector2D(-34, position.y))
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
assert unreal.EditorAssetLibrary.save_loaded_asset(bp, only_if_is_dirty=False)
unreal.log("MENU_POSITION_OK: right group moved left 90 logical units at 1920x1080; vertical layout preserved")
