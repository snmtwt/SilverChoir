"""Render overview and zoomed tabletop views; restore scene without saving test changes."""
import unreal, json, time, traceback
from pathlib import Path
OUT=Path('S:/UE_WorkSpace/SilverChoir/Saved/BaseSandboxSetup')
AA=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
WORLD=unreal.EditorLevelLibrary.get_editor_world()
BOARD=next(a for a in AA.get_all_level_actors() if isinstance(a,unreal.BaseSandboxMap))
CAMERA=next(a for a in AA.get_all_level_actors() if a.get_actor_label()=='CAM_BaseSandbox_Overview')
REPORT={'ok':False,'captures':[]}
STATE={'handle':None,'frames':0,'stage':0,'started':time.monotonic(),'busy':False}
CAP=AA.spawn_actor_from_class(unreal.SceneCapture2D,CAMERA.get_actor_location(),CAMERA.get_actor_rotation())
CAP.set_actor_location(CAMERA.get_actor_location(),False,False)
CAP.set_actor_rotation(CAMERA.get_actor_rotation(),False)
COMP=CAP.get_component_by_class(unreal.SceneCaptureComponent2D)
RT=unreal.RenderingLibrary.create_render_target2d(WORLD,1600,1100,unreal.TextureRenderTargetFormat.RTF_RGBA8)
RT.set_editor_property('target_gamma',2.2)
SETTINGS=COMP.get_editor_property('post_process_settings')
for k,v in {'override_auto_exposure_method':True,'auto_exposure_method':unreal.AutoExposureMethod.AEM_MANUAL,
    'override_auto_exposure_apply_physical_camera_exposure':True,'auto_exposure_apply_physical_camera_exposure':False,
    'override_auto_exposure_bias':True,'auto_exposure_bias':0.,
    'override_motion_blur_amount':True,'motion_blur_amount':0.,
    'override_depth_of_field_enabled':True,'depth_of_field_enabled':False}.items():SETTINGS.set_editor_property(k,v)
for k,v in {'texture_target':RT,'capture_source':unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR,
    'capture_every_frame':False,'capture_on_movement':False,'always_persist_rendering_state':True,
    'fov_angle':74.,'post_process_settings':SETTINGS,'post_process_blend_weight':1.}.items():COMP.set_editor_property(k,v)
def finish(error=None):
    if STATE['handle']:unreal.unregister_slate_post_tick_callback(STATE['handle'])
    BOARD.clear_selected_tile()
    BOARD.set_map_scale_at_world_location(BOARD.get_actor_location(),1.)
    AA.destroy_actor(CAP)
    REPORT.update(ok=error is None,error=error)
    (OUT/'review_result.json').write_text(json.dumps(REPORT,indent=2),encoding='utf8')
def tick(dt):
    if STATE['busy']:return
    STATE['busy']=True
    try:
        STATE['frames']+=1
        COMP.capture_scene()
        if STATE['frames']>=64 and time.monotonic()-STATE['started']>12:
            name=['overview','zoomed'][STATE['stage']]+'.png'
            unreal.RenderingLibrary.export_render_target(WORLD,RT,str(OUT),name)
            assert (OUT/name).stat().st_size>4096
            REPORT['captures'].append(str(OUT/name))
            if STATE['stage']==0:
                terrain=BOARD.get_map_terrain_mesh_component()
                REPORT['overview_scale']=str(terrain.get_editor_property('relative_scale3d'))
                BOARD.set_map_scale_at_world_location(BOARD.get_actor_location(),2.)
                BOARD.pan_map_by_world_delta(unreal.Vector(70,40,0))
                BOARD.switch_selected_tile(BOARD.get_tile_by_id('L7'))
                REPORT['zoomed_scale']=str(terrain.get_editor_property('relative_scale3d'))
                REPORT['tile_count']=len(BOARD.get_editor_property('spawned_tiles'))
                STATE.update(stage=1,frames=0,started=time.monotonic())
            else:finish()
    except Exception:finish(traceback.format_exc())
    finally:STATE['busy']=False
STATE['handle']=unreal.register_slate_post_tick_callback(tick)
