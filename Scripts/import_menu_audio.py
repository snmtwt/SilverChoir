"""Import the menu hover/press sounds without replacing Designer layouts.

UnrealEditor-Cmd <project> -run=pythonscript -script=<this file> -unattended
Add -MenuAudioVerify to check saved references without modifying assets.
Add -MenuPressOnly to change/verify only the press sound, preserving hover choices.
"""
import datetime
from pathlib import Path
import shutil
import unreal

ROOT = "/Game/System/Map/MainMenu/"
SOUNDS = {
    "hover_sound": ("UI_MenuHover_Electronic", 0.16, 0.12),
    "press_sound": ("UI_MenuPress_Terminal", 0.50, 0.24),
}
BUTTON_NAMES = ("NewGameButton", "LoadGameButton", "SettingsButton", "QuitButton")
PROJECT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
VERIFY = "-MenuAudioVerify" in unreal.SystemLibrary.get_command_line()
if "-MenuPressOnly" in unreal.SystemLibrary.get_command_line():
    SOUNDS = {"press_sound": SOUNDS["press_sound"]}


def button_templates():
    bp = unreal.load_asset(ROOT + "WBP_MainMenu")
    assert bp, "Main menu Blueprint missing"
    buttons = []
    for name in BUTTON_NAMES:
        widget = unreal.load_object(None, bp.get_path_name() + ":WidgetTree." + name)
        assert widget, "Menu button template missing: " + name
        buttons.append(widget)
    return bp, buttons


button_bp = unreal.load_asset(ROOT + "WBP_MenuButton")
assert button_bp, "Button Blueprint missing"
menu_bp, buttons = button_templates()
sounds = {}

if not VERIFY:
    # Preserve the current user-authored layouts before changing only sound references.
    backup = PROJECT / "Saved/MainMenuAssetBackups" / datetime.datetime.now().strftime("%Y%m%d-%H%M%S-audio")
    backup.mkdir(parents=True, exist_ok=True)
    for relative in ("WBP_MenuButton.uasset", "WBP_MainMenu.uasset", *("Audio/" + entry[0] + ".uasset" for entry in SOUNDS.values())):
        source = PROJECT / "Content/System/Map/MainMenu" / relative
        if source.exists():
            target = backup / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)

for prop, (name, volume, duration) in SOUNDS.items():
    path = ROOT + "Audio/" + name
    sound = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if not VERIFY and sound is None:
        task = unreal.AssetImportTask()
        task.filename = str(PROJECT / "SourceAssets/Audio/UI" / (name + ".wav"))
        task.destination_path = ROOT + "Audio"
        task.destination_name = name
        task.automated = True
        task.save = False
        task.factory = unreal.SoundFactory()
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        sound = unreal.load_asset(path)
    assert isinstance(sound, unreal.SoundWave), "SoundWave import failed"
    if not VERIFY:
        sound.set_editor_property("volume", volume)
        sound.set_editor_property("sound_group", unreal.SoundGroup.SOUNDGROUP_UI)
        sound.set_editor_property("loading_behavior", unreal.SoundWaveLoadingBehavior.FORCE_INLINE)
        sound.set_editor_property("looping", False)
        assert unreal.EditorAssetLibrary.save_loaded_asset(sound, only_if_is_dirty=False)
    assert abs(sound.get_editor_property("duration") - duration) < .005
    assert abs(sound.get_editor_property("volume") - volume) < .001
    assert sound.get_editor_property("loading_behavior") == unreal.SoundWaveLoadingBehavior.FORCE_INLINE
    sounds[prop] = sound

if not VERIFY:
    button_class = unreal.EditorAssetLibrary.load_blueprint_class(ROOT + "WBP_MenuButton")
    for prop, sound in sounds.items():
        unreal.get_default_object(button_class).set_editor_property(prop, sound)
    unreal.BlueprintEditorLibrary.compile_blueprint(button_bp)
    assert unreal.EditorAssetLibrary.save_loaded_asset(button_bp, only_if_is_dirty=False)

    # Assign existing Designer instances explicitly; do not rely on CDO propagation.
    menu_bp, buttons = button_templates()
    for button in buttons:
        for prop, sound in sounds.items():
            button.set_editor_property(prop, sound)
    unreal.BlueprintEditorLibrary.compile_blueprint(menu_bp)
    assert unreal.EditorAssetLibrary.save_loaded_asset(menu_bp, only_if_is_dirty=False)

button_class = unreal.EditorAssetLibrary.load_blueprint_class(ROOT + "WBP_MenuButton")
for prop, sound in sounds.items():
    assert unreal.get_default_object(button_class).get_editor_property(prop) == sound
menu_bp, buttons = button_templates()
for button in buttons:
    for prop, sound in sounds.items():
        assert button.get_editor_property(prop) == sound, button.get_name()
unreal.log("MENU_AUDIO_OK: selected sounds, volumes, durations, button defaults and all four menu instances verified")
