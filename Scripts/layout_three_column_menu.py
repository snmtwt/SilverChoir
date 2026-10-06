"""Move existing Designer widgets into three columns, preserving content and audio."""
from pathlib import Path
import datetime
import shutil
import unreal

project = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
asset_path = "/Game/System/Map/MainMenu/UI/WBP_MainMenu"
bp = unreal.load_asset(asset_path)
assert bp
def widget(name):
    result = unreal.load_object(None, bp.get_path_name() + ":WidgetTree." + name)
    assert result, name
    return result

names = ("NewGameButton", "LoadGameButton", "SettingsButton", "QuitButton")
sounds = {name: (widget(name).get_editor_property("hover_sound"), widget(name).get_editor_property("press_sound")) for name in names}
backup = project / "Saved/MainMenuAssetBackups" / datetime.datetime.now().strftime("%Y%m%d-%H%M%S-columns")
backup.mkdir(parents=True, exist_ok=True)
shutil.copy2(project / "Content/System/Map/MainMenu/UI/WBP_MainMenu.uasset", backup / "WBP_MainMenu.uasset")

frame = widget("ContentFrame")
def place(name, anchor, alignment, size, offset=(0, 0), reparent=False):
    item = widget(name)
    if reparent:
        item.remove_from_parent()
        slot = frame.add_child_to_canvas(item)
    else:
        slot = item.get_editor_property("slot")
    slot.set_anchors(unreal.Anchors(minimum=unreal.Vector2D(*anchor), maximum=unreal.Vector2D(*anchor)))
    slot.set_alignment(unreal.Vector2D(*alignment))
    slot.set_position(unreal.Vector2D(*offset))
    slot.set_size(unreal.Vector2D(*size))

place("MenuComposition", (.05, .5), (0, .5), (352, 240))
place("MenuHeading", (.95, .5), (1, .5), (300, 22), (0, -142), True)
place("MenuHint", (.95, .5), (1, .5), (132, 20), (0, -140), True)
place("MenuRule", (.95, .5), (1, .5), (300, 1), (0, -116), True)
place("MenuButtons", (.95, .5), (1, .5), (300, 224), (0, 15), True)
place("SignalCoreText", (.5, .5), (.5, .5), (90, 35))
place("SignalCoreCaption", (.5, .5), (.5, 0), (120, 20), (0, 21))
place("SignalTelemetry", (.5, .78), (.5, .5), (290, 22))
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
for name in names:
    assert sounds[name] == (widget(name).get_editor_property("hover_sound"), widget(name).get_editor_property("press_sound"))
assert unreal.EditorAssetLibrary.save_loaded_asset(bp, only_if_is_dirty=False)
unreal.log("THREE_COLUMN_MENU_OK: left title, centered radar, right menu; original button audio preserved")
