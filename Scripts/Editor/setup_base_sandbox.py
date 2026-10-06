"""Create the control-room tile sandbox. Run inside a fresh UE editor.

Re-runs update only assets and actors tagged by this installer. The original map
is backed up once before editing; existing room actors and global grid data stay intact.
"""
import json, math, runpy, shutil, traceback
from pathlib import Path
import unreal

ROOT = Path('S:/UE_WorkSpace/SilverChoir')
OUT = ROOT/'Saved/BaseSandboxSetup'
DEST = '/Game/System/Map/BaseMap/Sandbox'
LEVEL = '/Game/System/Map/BaseMap/L_BaseTemplate'
OWNER = 'BaseSandboxSetupV1'
AA = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
ASSETS = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
LEVELS = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
AT = unreal.AssetToolsHelpers.get_asset_tools()
ML = unreal.MaterialEditingLibrary
REPORT = {'ok':False}
def own(obj):
    ASSETS.set_metadata_tag(obj,'BaseSandboxOwner',OWNER)
    return obj
def existing(path):
    obj = unreal.load_asset(path) if ASSETS.does_asset_exist(path) else None
    if obj: assert ASSETS.get_metadata_tag(obj,'BaseSandboxOwner') == OWNER, 'Unowned destination: '+path
    return obj
def spawn(cls,label,loc,rot=None):
    a = AA.spawn_actor_from_class(cls,unreal.Vector(*loc),rot or unreal.Rotator())
    assert a
    a.set_actor_location(unreal.Vector(*loc),False,False)
    a.set_actor_rotation(rot or unreal.Rotator(),False)
    a.set_actor_label(label)
    a.set_editor_property('tags',[unreal.Name(OWNER)])
    a.set_folder_path('BaseSandbox')
    return a
def material(name,color,metallic,roughness,emission=0):
    path=DEST+'/Materials/'+name
    m=existing(path)
    if m:return m
    m=own(AT.create_asset(name,DEST+'/Materials',unreal.Material,unreal.MaterialFactoryNew()))
    c=ML.create_material_expression(m,unreal.MaterialExpressionConstant3Vector)
    c.set_editor_property('constant',unreal.LinearColor(*color,1))
    ML.connect_material_property(c,'',unreal.MaterialProperty.MP_BASE_COLOR)
    for prop,value in [(unreal.MaterialProperty.MP_METALLIC,metallic),(unreal.MaterialProperty.MP_ROUGHNESS,roughness)]:
        n=ML.create_material_expression(m,unreal.MaterialExpressionConstant);n.set_editor_property('r',value)
        ML.connect_material_property(n,'',prop)
    if emission:
        n=ML.create_material_expression(m,unreal.MaterialExpressionConstant3Vector)
        n.set_editor_property('constant',unreal.LinearColor(*(x*emission for x in color),1))
        ML.connect_material_property(n,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    ML.recompile_material(m);assert ASSETS.save_loaded_asset(m)
    return m
def cube(label,loc,size,mat):
    a=spawn(unreal.StaticMeshActor,label,loc)
    c=a.static_mesh_component;c.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
    c.set_material(0,mat);c.set_mobility(unreal.ComponentMobility.MOVABLE)
    c.set_editor_property('receives_decals',False)
    a.set_actor_scale3d(unreal.Vector(*(x/100 for x in size)))
    return a
try:
    OUT.mkdir(parents=True,exist_ok=True)
    src=ROOT/'Content/System/Map/BaseMap/L_BaseTemplate.umap'
    backup=OUT/'Backups/Content/System/Map/BaseMap/L_BaseTemplate.umap'
    backup.parent.mkdir(parents=True,exist_ok=True)
    if not backup.exists():shutil.copy2(src,backup)
    assert LEVELS.load_level(LEVEL)
    # Existing generated tile children are removed by ClearMapTiles first.
    for a in AA.get_all_level_actors():
        if OWNER in [str(t) for t in a.tags]:
            if isinstance(a,unreal.GSMMap3D):a.clear_map_tiles()
            AA.destroy_actor(a)
    mats=runpy.run_path(str(ROOT/'Scripts/Editor/base_sandbox_materials.py'))['ensure_materials']()
    REPORT['materials']=mats['report']
    terrain=mats['assets']['terrain'];water=mats['assets']['water'];grid=mats['assets']['grid']
    frame=material('M_BaseSandboxFrame',(.035,.047,.061),.7,.3)
    side=material('M_BaseSandboxSide',(.014,.02,.026),.55,.42)
    floor=material('M_BaseSandboxFloor',(.023,.033,.04),.2,.6)
    trim=material('M_BaseSandboxTrim',(.11,.35,.4),.5,.28,.6)
    bp_path=DEST+'/BP_BaseSandboxTile'
    bp=existing(bp_path)
    if not bp:
        factory=unreal.BlueprintFactory();factory.set_editor_property('parent_class',unreal.GSMTile3D)
        bp=own(AT.create_asset('BP_BaseSandboxTile',DEST,unreal.Blueprint,factory))
    tile_class=bp.generated_class()
    cdo=unreal.get_default_object(tile_class)
    cdo.get_editor_property('edge_decal_component').set_editor_property('decal_size',unreal.Vector(250,50,50))
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    bp.modify()
    tile_class=bp.generated_class()
    cdo=unreal.get_default_object(tile_class)
    cdo.modify()
    for key,value in {'use_tile_static_mesh_visual':False,'use_independent_tile_collision':True,
        'tile_collision_height':32.,'tile_collision_relative_location':unreal.Vector(0,0,8),
        'edge_decal_material':grid,'use_edge_decal':True,
        'edge_decal_size':unreal.Vector(250,50,50),'edge_decal_relative_location':unreal.Vector(0,0,25),
        'fit_edge_decal_to_runtime_square_tile_size':False,'scale_edge_decal_with_map_scale':True,
        'edge_decal_border_extent_scale':.5,
        'default_edge_decal_color':unreal.LinearColor(.30,.60,.65,1),
        'selected_edge_decal_color':unreal.LinearColor(1.,.55,.10,1)}.items():cdo.set_editor_property(key,value)
    assert ASSETS.save_loaded_asset(bp)
    cfg_path=DEST+'/DA_BaseSandboxMap'
    cfg=existing(cfg_path)
    if not cfg:cfg=own(ASSETS.duplicate_asset('/Game/System/SubSystem/GridMapSubSystem/DA_GridMapConfig',cfg_path))
    for key,value in {'map_name':'基地作战沙盘','default_tile_actor_class':tile_class,
        'whole_map_terrain_mesh':unreal.load_asset('/Game/Meshs/Map/SM_SandboxMap'),
        'whole_map_terrain_material_overrides':[terrain,water],
        'use_custom_stencil_for_map_decals':True,
        'fit_whole_map_terrain_mesh_to_tile_grid_bounds':True,'whole_map_terrain_mesh_base_scale':unreal.Vector(1,1,1),
        'whole_map_terrain_mesh_height_offset':.5,'whole_map_terrain_mesh_yaw_degrees':180.,
        'default_view_scale':1.,'default_view_center':unreal.Vector2D(0,0)}.items():cfg.set_editor_property(key,value)
    assert cfg.get_tile_count()==336
    valid=unreal.get_editor_subsystem(unreal.GSMEditorToolSubsystem).validate_map_config(cfg)
    REPORT['config_validation']=str(valid)
    assert ASSETS.save_loaded_asset(cfg)
    board=spawn(unreal.BaseSandboxMap,'BaseSandbox_Map_24x14',(0,0,105),unreal.Rotator(pitch=0,yaw=90,roll=0))
    board.set_editor_property('load_on_begin_play',True)
    board.set_editor_property('max_map_scale',3.)
    board.set_editor_property('coordinate_label_world_size',11.)
    board.set_editor_property('coordinate_label_offset',12.)
    board.set_editor_property('coordinate_label_z_offset',1.)
    board.set_editor_property('coordinate_label_color',unreal.LinearColor(.54,.72,.74,1))
    board.set_editor_property('show_bottom_coordinate_labels',True)
    board.set_editor_property('show_right_coordinate_labels',True)
    comp=board.get_board_mesh_component()
    for key,value in {'interior_width':720.,'interior_height':420.,'left_border_width':24.,'right_border_width':24.,
        'top_border_width':24.,'bottom_border_width':24.,'board_thickness':18.,'groove_depth':8.,
        'board_material':frame,'board_side_material':side,'groove_floor_material':floor}.items():comp.set_editor_property(key,value)
    comp.rebuild_board_mesh()
    board.set_map_config(cfg,True)
    tiles=list(board.get_editor_property('spawned_tiles'))
    assert len(tiles)==336
    for t in tiles:t.set_folder_path('BaseSandbox/Tiles')
    # A low equipment plinth and two supports fit within the existing reserved floor outline.
    cube('BaseSandbox_Plinth',(0,0,8),(350,620,16),side)
    for y in (-215,215):cube('BaseSandbox_Support',(0,y,51.5),(290,72,71),frame)
    for x in (-235,235):cube('BaseSandbox_LightStrip',(x,0,91),(2.5,704,2),trim)
    camera=spawn(unreal.CameraActor,'CAM_BaseSandbox_Overview',(-660,-60,470),unreal.Rotator(pitch=-27.3,yaw=5.2,roll=0))
    camera.camera_component.set_editor_property('field_of_view',74.)
    # Local table light, bounded to the control room; no global scene lighting change.
    light=spawn(unreal.RectLight,'BaseSandbox_TaskLight',(0,0,480),unreal.Rotator(pitch=-90,yaw=0,roll=0))
    lc=light.get_component_by_class(unreal.RectLightComponent)
    lc.set_mobility(unreal.ComponentMobility.MOVABLE);lc.set_intensity(1800.)
    lc.set_editor_property('attenuation_radius',650.)
    lc.set_editor_property('source_width',240.);lc.set_editor_property('source_height',400.)
    lc.set_editor_property('use_temperature',True);lc.set_temperature(6000.)
    unreal.EditorLevelLibrary.set_level_viewport_camera_info(camera.get_actor_location(),camera.get_actor_rotation())
    AA.set_selected_level_actors([board])
    assert LEVELS.save_current_level()
    REPORT.update(ok=True,map=LEVEL,actor=board.get_name(),config=cfg_path,tile_count=len(tiles),
        grid=[24,14],tile_size_cm=30,board_interior_cm=[720,420],board_outer_cm=[768,468],
        position=[0,0,105],yaw=90,mesh='/Game/Meshs/Map/SM_SandboxMap',
        backup=str(backup),terrain_transform=str(board.get_map_terrain_mesh_component().get_relative_transform()))
except Exception:
    REPORT['error']=traceback.format_exc();unreal.log_error(REPORT['error']);raise
finally:
    (OUT/'setup_result.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf8')
