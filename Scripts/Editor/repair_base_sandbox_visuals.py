import unreal,json
from pathlib import Path
ASSETS=unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
ML=unreal.MaterialEditingLibrary
DEST='/Game/System/Map/BaseMap/Sandbox'
report={}
for name in ['M_BaseSandboxTerrain','M_BaseSandboxWater','M_BaseSandboxGrid']:
    m=unreal.load_asset(DEST+'/Materials/'+name)
    if name=='M_BaseSandboxGrid':
        for n in ML.get_material_expressions(m):
            if isinstance(n,unreal.MaterialExpressionCustom):
                code=n.get_editor_property('code')
                n.set_editor_property('code',code.replace('float line =','float borderMask =').replace('return line *','return borderMask *'))
    report[name]=[str(x) for x in (ML.recompile_material(m) or [])]
    assert not report[name],report[name]
    ASSETS.save_loaded_asset(m)
bp=unreal.load_asset(DEST+'/BP_BaseSandboxTile')
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
cdo=unreal.get_default_object(bp.generated_class())
cdo.get_editor_property('edge_decal_component').set_editor_property('decal_size',unreal.Vector(250,50,50))
bp.modify()
cdo=unreal.get_default_object(bp.generated_class())
cdo.modify()
for k,v in {'edge_decal_material':unreal.load_asset(DEST+'/Materials/M_BaseSandboxGrid'),
    'edge_decal_size':unreal.Vector(250,50,50),'edge_decal_relative_location':unreal.Vector(0,0,25),
    'tile_collision_height':32.,'tile_collision_relative_location':unreal.Vector(0,0,8),
    'edge_decal_border_extent_scale':.5,
    'default_edge_decal_color':unreal.LinearColor(.30,.60,.65,1),
    'selected_edge_decal_color':unreal.LinearColor(1.,.55,.10,1)}.items():cdo.set_editor_property(k,v)
report['cdo_before_save']=str(cdo.get_editor_property('edge_decal_material'))
assert ASSETS.save_loaded_asset(bp,only_if_is_dirty=False)
cdo=unreal.get_default_object(bp.generated_class())
report['cdo_after_save']=str(cdo.get_editor_property('edge_decal_material'))
aa=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
board=next(a for a in aa.get_all_level_actors() if isinstance(a,unreal.BaseSandboxMap))
board.rebuild_map_from_config()
report['tile_class']=board.get_tile_by_id('L7').get_class().get_path_name()
report['tile_decal']=str(board.get_tile_by_id('L7').get_editor_property('edge_decal_material'))
for t in board.get_editor_property('spawned_tiles'):t.set_folder_path('BaseSandbox/Tiles')
assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
Path('S:/UE_WorkSpace/SilverChoir/Saved/BaseSandboxSetup/visual_repair.json').write_text(json.dumps(report,indent=2),encoding='utf8')
