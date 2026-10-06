"""Apply the larger title treatment to the existing Widget Blueprint only."""
from pathlib import Path
import datetime
import shutil
import unreal

project = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
bp = unreal.load_asset("/Game/System/Map/MainMenu/UI/WBP_MainMenu")
assert bp
backup = project / "Saved/MainMenuAssetBackups" / datetime.datetime.now().strftime("%Y%m%d-%H%M%S-title")
backup.mkdir(parents=True, exist_ok=True)
shutil.copy2(project / "Content/System/Map/MainMenu/UI/WBP_MainMenu.uasset", backup / "WBP_MainMenu.uasset")

def widget(name):
    result = unreal.load_object(None, bp.get_path_name() + ":WidgetTree." + name)
    assert result, name
    return result

widget("MenuComposition").get_editor_property("slot").set_size(unreal.Vector2D(430, 395))
for name, point, size, font_size in (
    ("TitleSilver", (-4, 32), (426, 138), 90),
    ("TitleChoir", (-4, 148), (426, 158), 102),
    ("TitleChinese", (0, 319), (340, 32), 15),
    ("Kicker", (0, 362), (340, 24), 10),
):
    text = widget(name)
    slot = text.get_editor_property("slot")
    slot.set_position(unreal.Vector2D(*point))
    slot.set_size(unreal.Vector2D(*size))
    font = text.get_editor_property("font")
    font.set_editor_property("size", font_size)
    text.set_font(font)

for name in ("MenuButtons", "MenuHeading", "MenuHint", "MenuRule"):
    slot = widget(name).get_editor_property("slot")
    anchors = slot.get_anchors()
    anchors.minimum.x = .90
    anchors.maximum.x = .90
    slot.set_anchors(anchors)
    position = slot.get_position()
    slot.set_position(unreal.Vector2D(-34, position.y))

unreal.BlueprintEditorLibrary.compile_blueprint(bp)
assert unreal.EditorAssetLibrary.save_loaded_asset(bp, only_if_is_dirty=False)
unreal.log("MENU_TITLE_OK: 90/102 pt title, original colors; right menu anchored at 90 percent with -34 offset")
