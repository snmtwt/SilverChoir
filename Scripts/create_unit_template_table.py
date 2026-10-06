"""Create the editable unit template table without overwriting existing rows."""
import unreal

folder = "/Game/System/SubSystem/PlayerUnitSubSystem"
path = folder + "/DT_UnitTemplates"
library = unreal.EditorAssetLibrary
table = unreal.load_asset(path) if library.does_asset_exist(path) else None
if table is None:
    library.make_directory(folder)
    factory = unreal.DataTableFactory()
    factory.set_editor_property("struct", unreal.UnitTemplate.static_struct())
    table = unreal.AssetToolsHelpers.get_asset_tools().create_asset("DT_UnitTemplates", folder, unreal.DataTable, factory)
    assert table
    assert library.save_loaded_asset(table, only_if_is_dirty=False)
assert table.get_editor_property("row_struct") == unreal.UnitTemplate.static_struct()
unreal.log("UNIT_TEMPLATE_TABLE_OK " + table.get_path_name())
