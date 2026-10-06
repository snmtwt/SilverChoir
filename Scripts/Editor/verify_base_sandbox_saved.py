import unreal,json,traceback
from pathlib import Path
OUT=Path('S:/UE_WorkSpace/SilverChoir/Saved/BaseSandboxSetup')
report={'ok':False}
try:
    assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level('/Game/System/Map/BaseMap/L_BaseTemplate')
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    names={a.get_name() for a in actors}
    original=json.loads((OUT/'inspection.json').read_text(encoding='utf8'))['actors']
    missing=[a['name'] for a in original if a['name'] not in names]
    assert not missing,missing
    boards=[a for a in actors if isinstance(a,unreal.BaseSandboxMap)]
    assert len(boards)==1,len(boards)
    board=boards[0]
    tiles=[a for a in actors if isinstance(a,unreal.GSMTile3D)]
    assert len(tiles)==336,len(tiles)
    assert all(t.get_owner()==board for t in tiles)
    assert all(t.get_editor_property('edge_decal_material') is not None for t in tiles)
    p=board.get_actor_location();r=board.get_actor_rotation()
    assert abs(p.x)<.01 and abs(p.y)<.01 and abs(p.z-105)<.01
    assert abs(r.pitch)<.01 and abs(r.roll)<.01 and abs(r.yaw-90)<.01,str(r)
    cfg=board.get_editor_property('map_config')
    assert cfg.get_tile_count()==336
    assert cfg.get_editor_property('whole_map_terrain_mesh').get_path_name()=='/Game/Meshs/Map/SM_SandboxMap.SM_SandboxMap'
    assert len(cfg.get_editor_property('whole_map_terrain_material_overrides'))==2
    assert cfg.get_editor_property('use_custom_stencil_for_map_decals')
    assert not board.get_editor_property('use_default_map_data')
    assert board.get_editor_property('max_map_scale')==3.
    labels=board.get_components_by_class(unreal.TextRenderComponent)
    report.update(ok=True,fresh_process=True,all_original_actors_preserved=True,original_actor_count=len(original),
        sandbox_count=len(boards),tile_count=len(tiles),coordinate_labels=len(labels),rotation=str(r),
        location=str(p),materials=[m.get_path_name() for m in cfg.get_editor_property('whole_map_terrain_material_overrides')])
except Exception:report['error']=traceback.format_exc()
(OUT/'disk_validation.json').write_text(json.dumps(report,indent=2),encoding='utf8')
