"""Move room widget assets through UE, retaining redirectors for existing scene graphs."""
from pathlib import Path
from datetime import datetime
import shutil
import unreal
root=Path(unreal.Paths.project_dir())
shutil.copytree(root/"Content/System/Map/BaseMap/UI",root/"Saved/PersonnelUIBackups"/datetime.now().strftime("%Y%m%d-%H%M%S-folders"))
base="/Game/System/Map/BaseMap/UI/SceneUI/"
shell=unreal.load_asset("/Game/System/Map/BaseMap/UI/BP_BaseMapWidget")
assert shell
for room in ("SquadMeetingRoom","CommanderOffice","OperationsCommandRoom","PersonnelPreparationRoom"):
    old=base+"WBP_"+room
    new=base+room+"/WBP_"+room
    if not unreal.EditorAssetLibrary.does_asset_exist(new):
        asset=unreal.load_asset(old)
        assert asset
        assert unreal.AssetToolsHelpers.get_asset_tools().rename_assets([unreal.AssetRenameData(asset,base+room,"WBP_"+room)])
    child=unreal.load_asset(new)
    assert child
    unreal.BlueprintEditorLibrary.compile_blueprint(child)
    assert unreal.EditorAssetLibrary.save_loaded_asset(child,only_if_is_dirty=False)
unreal.BlueprintEditorLibrary.compile_blueprint(shell)
assert unreal.EditorAssetLibrary.save_loaded_asset(shell,only_if_is_dirty=False)
unreal.log("SCENE_UI_FOLDERS_OK")
