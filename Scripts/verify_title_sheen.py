"""Read-only verification of saved title materials and unchanged typography/palette."""
import unreal

root = "/Game/System/Map/MainMenu/"
bp = unreal.load_asset(root + "WBP_MainMenu")
material = unreal.load_asset(root + "Materials/M_TitleSheen")
assert material.get_editor_property("material_domain") == unreal.MaterialDomain.MD_UI
for name, suffix, size, rgb in (
    ("TitleSilver", "Silver", 90, (220, 233, 245)),
    ("TitleChoir", "Choir", 102, (39, 111, 152)),
):
    text = unreal.load_object(None, bp.get_path_name() + ":WidgetTree." + name)
    font = text.get_editor_property("font")
    assert font.get_editor_property("size") == size
    assert font.get_editor_property("font_material") == unreal.load_asset(root + "Materials/MI_TitleSheen_" + suffix)
    expected = unreal.MathLibrary.conv_color_to_linear_color(unreal.Color(r=rgb[0], g=rgb[1], b=rgb[2], a=255))
    actual = text.get_editor_property("color_and_opacity").get_editor_property("specified_color")
    for channel in ("r", "g", "b", "a"):
        assert abs(getattr(actual, channel) - getattr(expected, channel)) < .00001
unreal.log("TITLE_SHEEN_VERIFIED: saved native material references, original palette and 90/102 pt sizes")
