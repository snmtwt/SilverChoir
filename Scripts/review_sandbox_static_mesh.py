"""Render original-size and movable tabletop variants of the repaired static mesh."""
from pathlib import Path
import json,math,time,traceback
import unreal
ROOT=Path('S:/UE_WorkSpace/SilverChoir')
OUT=ROOT/'Saved/SandboxStaticMeshRepair'
MAP='/Game/Meshs/Map/Preview/L_SandboxMapPreview'
MESH='/Game/Meshs/Map/SM_SandboxMap'
OWNER='SilverSandboxStaticPreviewV1'
OWNER_KEY='SilverSandboxPreviewOwner'
ML=unreal.MaterialEditingLibrary
AA=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
LEVELS=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ASSETS=unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
REPORT={'ok':False,'status':'initializing','captures':[]}
STATE={'handle':None,'busy':False,'capture':None,'component':None,'target':None,'index':0,'frames':0,'export_frame':None,
       'water_override_active':False,'water_original':None,'finished':False}
WATER=None
VIEWS=[{'name':'full_scale','scale':(1.,1.,1.),'yaw':0.,'location':(0.,0.,0.)},
       {'name':'tabletop_rotated','scale':(.0001,.0001,.0001),'yaw':180.,'location':(650.,200.,75.)},
       {'name':'tabletop_nonuniform','scale':(.00012,.00008,.00016),'yaw':37.,'location':(400.,900.,75.)}]

def write():
    OUT.mkdir(parents=True,exist_ok=True)
    (OUT/'preview_result.json').write_text(json.dumps(REPORT,indent=2),encoding='utf8')

def spawn_owned(cls,location,rotation=None,label=None):
    actor=(AA.spawn_actor_from_class(cls,location,rotation) if rotation is not None
           else AA.spawn_actor_from_class(cls,location))
    assert actor,'Could not create preview actor: '+str(cls)
    actor.set_editor_property('tags',list(actor.get_editor_property('tags'))+[unreal.Name(OWNER)])
    if label:actor.set_actor_label(label)
    return actor

def restore_water():
    if not STATE['water_override_active']:return
    assert WATER is not None and STATE['water_original'] is not None
    ML.set_material_instance_scalar_parameter_value(WATER,'WaveTimeOverride',STATE['water_original'])
    ML.update_material_instance(WATER)
    # The temporary capture override was never saved; restore in memory only.
    assert ML.get_material_instance_scalar_parameter_value(WATER,'WaveTimeOverride')==STATE['water_original']
    STATE['water_override_active']=False
    REPORT['animated_water_restored']=True

def prepare_preview_level():
    if ASSETS.does_asset_exist(MAP):
        existing=unreal.load_asset(MAP)
        assert ASSETS.get_metadata_tag(existing,OWNER_KEY)==OWNER,'Refusing unowned preview map: '+MAP
        assert LEVELS.load_level(MAP)
        # Rebuild only this script's actors. User-added actors remain untouched.
        for actor in AA.get_all_level_actors():
            if OWNER in [str(tag) for tag in actor.get_editor_property('tags')]:
                assert AA.destroy_actor(actor),'Failed to remove owned preview actor'
        REPORT['reused_owned_preview']=True
    else:
        assert LEVELS.new_level(MAP)
    world=unreal.EditorLevelLibrary.get_editor_world()
    ASSETS.set_metadata_tag(world,OWNER_KEY,OWNER)
    # Persist ownership before initialization so a failed run can safely retry.
    assert LEVELS.save_current_level()
    return world

def exposure(settings):
    values={'override_auto_exposure_method':True,'auto_exposure_method':unreal.AutoExposureMethod.AEM_MANUAL,
        'override_auto_exposure_apply_physical_camera_exposure':True,'auto_exposure_apply_physical_camera_exposure':False,
        'override_auto_exposure_bias':True,'auto_exposure_bias':0.,'override_bloom_intensity':True,'bloom_intensity':0.,
        'override_vignette_intensity':True,'vignette_intensity':0.,'override_motion_blur_amount':True,'motion_blur_amount':0.,
        'override_depth_of_field_enabled':True,'depth_of_field_enabled':False,
        'override_local_exposure_highlight_contrast_scale':True,'local_exposure_highlight_contrast_scale':1.,
        'override_local_exposure_shadow_contrast_scale':True,'local_exposure_shadow_contrast_scale':1.}
    for k,v in values.items():settings.set_editor_property(k,v)
    return settings

def stop():
    if STATE['handle'] is not None:unreal.unregister_slate_post_tick_callback(STATE['handle']);STATE['handle']=None

def clean_capture():
    if STATE['capture'] is not None:AA.destroy_actor(STATE['capture']);STATE['capture']=None
    STATE['component']=STATE['target']=None

def camera_for(view):
    s=view['scale'];r=math.radians(view['yaw']);co,si=math.cos(r),math.sin(r)
    local=(0,1700000*s[1],1950000*s[2]);loc=view['location']
    x=local[0]*co-local[1]*si;y=local[0]*si+local[1]*co
    position=unreal.Vector(loc[0]+x,loc[1]+y,loc[2]+local[2])
    # Original overview looked near sea datum (-10000 cm).
    target_z=loc[2]-10000*s[2]
    pitch=math.degrees(math.atan2(target_z-position.z,math.hypot(x,y)))
    rotation=unreal.Rotator(pitch=pitch,yaw=view['yaw']-90,roll=0.)
    return position,rotation,2580000*max(s[0],s[1])

def apply_view(view):
    MODEL.set_actor_location(unreal.Vector(*view['location']),False,False)
    MODEL.set_actor_rotation(unreal.Rotator(pitch=0,yaw=view['yaw'],roll=0),False)
    MODEL.set_actor_scale3d(unreal.Vector(*view['scale']))
    SUN.set_actor_rotation(unreal.Rotator(pitch=-28,yaw=-135+view['yaw'],roll=0),False)
    position,rotation,width=camera_for(view)
    CAMERA.set_actor_location(position,False,False);CAMERA.set_actor_rotation(rotation,False)
    CAMERA.camera_component.set_editor_property('ortho_width',width)
    unreal.EditorLevelLibrary.set_level_viewport_camera_info(position,rotation)
    return position,rotation,width

def finish(error=None):
    if STATE['finished']:return
    STATE['finished']=True
    errors=[error] if error else []
    for cleanup in (stop,clean_capture,restore_water):
        try:cleanup()
        except Exception:errors.append(traceback.format_exc())
    if not errors:
        try:
            # Leave a 2.4 x 1.4 m translated and rotated model for the preview level.
            apply_view(VIEWS[1]);assert LEVELS.save_current_level()
        except Exception:errors.append(traceback.format_exc())
    if errors:
        error='\n'.join(errors)
        REPORT.update(ok=False,status='failed',error=error);unreal.log_error(error)
    else:REPORT.update(ok=True,status='complete',map=MAP,animated_water_restored=True)
    write()

def begin():
    view=VIEWS[STATE['index']]
    pos,rot,width=apply_view(view)
    cap=spawn_owned(unreal.SceneCapture2D,pos,rot,'SandboxPortableTemporaryCapture')
    STATE['capture']=cap
    comp=cap.get_component_by_class(unreal.SceneCaptureComponent2D)
    rt=unreal.RenderingLibrary.create_render_target2d(WORLD,1920,1120,unreal.TextureRenderTargetFormat.RTF_RGBA8)
    rt.set_editor_property('target_gamma',2.2)
    for k,v in {'texture_target':rt,'capture_source':unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR,
        'capture_every_frame':False,'capture_on_movement':False,'always_persist_rendering_state':True,
        'projection_type':unreal.CameraProjectionMode.ORTHOGRAPHIC,'ortho_width':width,
        'auto_calculate_ortho_planes':True,'max_view_distance_override':8000000.,
        'post_process_settings':exposure(comp.get_editor_property('post_process_settings')),
        'post_process_blend_weight':1.}.items():comp.set_editor_property(k,v)
    flags=[]
    for name in ('DepthOfField','MotionBlur','Bloom'):
        flag=unreal.EngineShowFlagsSetting();flag.set_editor_property('show_flag_name',name);flag.set_editor_property('enabled',False);flags.append(flag)
    comp.set_editor_property('show_flag_settings',flags)
    STATE.update(component=comp,target=rt,started=time.monotonic(),frames=0,export_frame=None)
    REPORT['status']='rendering_'+view['name'];write()
    STATE['handle']=unreal.register_slate_post_tick_callback(tick)

def tick(delta):
    if STATE['busy']:return
    STATE['busy']=True
    try:
        STATE['frames']+=1
        if STATE['export_frame'] is None:
            STATE['component'].capture_scene()
            if STATE['frames']>=64 and time.monotonic()-STATE['started']>=15:STATE['export_frame']=STATE['frames']+4
        elif STATE['frames']>=STATE['export_frame']:
            view=VIEWS[STATE['index']];name=view['name']+'.png'
            unreal.RenderingLibrary.export_render_target(WORLD,STATE['target'],str(OUT),name)
            assert (OUT/name).is_file() and (OUT/name).stat().st_size>4096
            REPORT['captures'].append({**view,'file':str(OUT/name)})
            stop();clean_capture();STATE['index']+=1
            if STATE['index']<len(VIEWS):begin()
            else:finish()
    except Exception:finish(traceback.format_exc())
    finally:STATE['busy']=False

try:
    mesh=unreal.load_asset(MESH);assert mesh and mesh.get_editor_property('allow_cpu_access')
    WORLD=prepare_preview_level()
    MODEL=spawn_owned(unreal.StaticMeshActor,unreal.Vector(0,0,0),label='SM_SandboxMap_Portable')
    MODEL.static_mesh_component.set_static_mesh(mesh)
    MODEL.static_mesh_component.set_editor_property('bounds_scale',1.10)
    MODEL.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    MODEL.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    WATER=unreal.load_asset('/Game/Meshs/Map/Materials/MI_SandboxWater')
    assert WATER is not None
    STATE['water_original']=ML.get_material_instance_scalar_parameter_value(WATER,'WaveTimeOverride')
    assert STATE['water_original']==-1.
    STATE['water_override_active']=True
    ML.set_material_instance_scalar_parameter_value(WATER,'WaveTimeOverride',0.);ML.update_material_instance(WATER)
    SUN=spawn_owned(unreal.DirectionalLight,unreal.Vector(0,0,500000),label='SandboxPreviewSun')
    c=SUN.get_component_by_class(unreal.DirectionalLightComponent);c.set_mobility(unreal.ComponentMobility.MOVABLE)
    c.set_intensity(3.5);c.set_editor_property('use_temperature',True);c.set_temperature(6200)
    sky=spawn_owned(unreal.SkyLight,unreal.Vector(0,0,100000),label='SandboxPreviewSky')
    c=sky.get_component_by_class(unreal.SkyLightComponent);c.set_mobility(unreal.ComponentMobility.MOVABLE)
    c.set_editor_property('source_type',unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
    c.set_cubemap(unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'));c.set_intensity(.85)
    c.set_editor_property('lower_hemisphere_is_black',False);c.set_editor_property('cast_shadows',False);c.recapture_sky()
    pp=spawn_owned(unreal.PostProcessVolume,unreal.Vector(0,0,0),label='SandboxPreviewExposure');pp.set_editor_property('unbound',True)
    pp.set_editor_property('settings',exposure(pp.get_editor_property('settings')))
    CAMERA=spawn_owned(unreal.CameraActor,unreal.Vector(0,0,0),label='SandboxPreviewCamera')
    CAMERA.camera_component.set_editor_property('projection_mode',unreal.CameraProjectionMode.ORTHOGRAPHIC)
    CAMERA.camera_component.set_editor_property('post_process_settings',exposure(CAMERA.camera_component.get_editor_property('post_process_settings')))
    begin()
except Exception:
    finish(traceback.format_exc());raise
