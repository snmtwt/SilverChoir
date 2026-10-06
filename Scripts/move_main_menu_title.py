"""Move the title into the marked area while retaining narrow-screen clearance."""
import datetime
from pathlib import Path
import shutil
import unreal

project = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
bp = unreal.load_asset("/Game/System/Map/MainMenu/UI/WBP_MainMenu")
assert bp
backup = project / "Saved/MainMenuAssetBackups" / datetime.datetime.now().strftime("%Y%m%d-%H%M%S-title-position")
backup.mkdir(parents=True, exist_ok=True)
shutil.copy2(project / "Content/System/Map/MainMenu/UI/WBP_MainMenu.uasset", backup / "WBP_MainMenu.uasset")
title = unreal.load_object(None, bp.get_path_name() + ":WidgetTree.MenuComposition")
assert title
slot = title.get_editor_property("slot")
anchors = slot.get_anchors()
slot.set_anchors(unreal.Anchors(minimum=unreal.Vector2D(.175, anchors.minimum.y), maximum=unreal.Vector2D(.175, anchors.maximum.y)))
position = slot.get_position()
# At 1920 logical width this moves x=96 to x=160. At 1440 (4:3)
# it moves x=72 to x=76, preserving separation from the centered radar.
slot.set_position(unreal.Vector2D(-176, position.y))
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
assert unreal.EditorAssetLibrary.save_loaded_asset(bp, only_if_is_dirty=False)
unreal.log("TITLE_POSITION_OK: title moved right into reference box; typography and vertical position unchanged")
