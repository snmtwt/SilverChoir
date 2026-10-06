"""Cold-load the saved panel assets and verify their native classes and configured vehicle child."""
import unreal

ROOT = "/Game/System/Map/BattleMap/UI/"
expected = {
    "Components/WBP_人员卡片": unreal.BattlePersonnelCardWidget,
    "Components/WBP_成员模式": unreal.BattleMemberModeWidget,
    "Components/WBP_车辆面板": unreal.BattleVehiclePanelWidget,
    "Components/WBP_BattleVehicleButton": unreal.BattleHUDButton,
    "WBP_BattleHUD": unreal.BattleMapWidget,
}
defaults = {}
for relative, native in expected.items():
    asset = unreal.load_asset(ROOT + relative)
    assert asset, relative
    unreal.BlueprintEditorLibrary.compile_blueprint(asset)
    cls = asset.generated_class()
    cdo = unreal.get_default_object(cls)
    assert isinstance(cdo, native), relative
    defaults[relative] = cdo
    unreal.log("BATTLE_PANEL_COLD_CLASS_OK " + relative)

vehicle_class = defaults["Components/WBP_成员模式"].get_editor_property("vehicle_panel_class")
assert vehicle_class.get_path_name() == ROOT + "Components/WBP_车辆面板.WBP_车辆面板_C"
assert not defaults["Components/WBP_车辆面板"].get_editor_property("panel_open")
button = defaults["Components/WBP_BattleVehicleButton"]
assert button.get_editor_property("glyph") == unreal.BattleGlyph.VEHICLE
assert str(button.get_editor_property("button_text")) == "车\n辆"
unreal.log("BATTLE_PANEL_COLD_VERIFY_OK")

import runpy
runpy.run_path(unreal.Paths.project_dir() + "Scripts/Editor/create_unit_selection_decal.py", run_name="__main__")
