import unreal, json, traceback
from pathlib import Path

out = Path('S:/UE_WorkSpace/SilverChoir/Saved/BaseSandboxSetup')
out.mkdir(parents=True, exist_ok=True)
report = {}
def props(obj, names):
    result = {}
    for name in names:
        try: result[name] = str(obj.get_editor_property(name))
        except Exception as e: result[name] = str(e)
    return result
try:
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert levels.load_level('/Game/System/Map/BaseMap/L_BaseTemplate')
    aa = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    report['actors'] = [{'name':a.get_name(),'label':a.get_actor_label(),'class':a.get_class().get_path_name(),
        'location':str(a.get_actor_location()),'rotation':str(a.get_actor_rotation()),'scale':str(a.get_actor_scale3d())}
        for a in aa.get_all_level_actors()]
    mesh = unreal.load_asset('/Game/Meshs/Map/SM_SandboxMap')
    report['mesh'] = {'bounds':str(mesh.get_bounds()),'box':str(mesh.get_bounding_box()),'materials':str(mesh.get_editor_property('static_materials'))}
    cfg = unreal.load_asset('/Game/System/SubSystem/GridMapSubSystem/DA_GridMapConfig')
    report['existing_config'] = props(cfg, ['editor_grid_columns','editor_grid_rows','default_tile_actor_class','whole_map_terrain_mesh','tile_entries','default_view_scale'])
    report['api'] = {n: [s for s in dir(getattr(unreal,n)) if not s.startswith('_')] for n in ['GSMMap3D','GSMMapSubsystem','GSMEditorToolSubsystem','GSMTileEntry'] if hasattr(unreal,n)}
    report['ok'] = True
except Exception:
    report['error'] = traceback.format_exc()
finally:
    (out/'inspection.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
