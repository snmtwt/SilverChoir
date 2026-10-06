import unreal, json
from pathlib import Path
aa = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
board = next(a for a in aa.get_all_level_actors() if isinstance(a, unreal.BaseSandboxMap))
terrain = board.get_map_terrain_mesh_component()
report = {'board_transform': str(board.get_actor_transform()),
          'terrain_transform': str(terrain.get_world_transform()),
          'terrain_relative_location': str(terrain.get_editor_property('relative_location')),
          'terrain_relative_rotation': str(terrain.get_editor_property('relative_rotation')),
          'terrain_relative_scale': str(terrain.get_editor_property('relative_scale3d')),
          'mesh': terrain.get_editor_property('static_mesh').get_path_name(),
          'tiles':len([a for a in aa.get_all_level_actors() if isinstance(a,unreal.GSMTile3D)]),
          'spawned_tiles':len(board.get_editor_property('spawned_tiles')),
          'text_components':len(board.get_components_by_class(unreal.TextRenderComponent)),
          'custom_depth':terrain.get_editor_property('render_custom_depth'),
          'stencil':terrain.get_editor_property('custom_depth_stencil_value'),
          'hit_api':dir(unreal.HitResult)}
actor = aa.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0,0,0), unreal.Rotator(), True)
try:
    comp = actor.get_component_by_class(unreal.StaticMeshComponent)
    comp.set_mobility(unreal.ComponentMobility.MOVABLE)
    comp.set_static_mesh(unreal.load_asset('/Game/Meshs/Map/SM_SandboxMap'))
    actor.set_actor_location(unreal.Vector(0,0,0),False,False)
    actor.set_actor_rotation(unreal.Rotator(pitch=0,yaw=0,roll=0),False)
    comp.set_collision_enabled(unreal.CollisionEnabled.QUERY_ONLY)
    report['probe_transform']=str(comp.get_world_transform())
    result=comp.line_trace_component(unreal.Vector(-650000,350000,200000),unreal.Vector(-650000,350000,-50000),True,False,False)
    report['trace_result']=str(result)
    report['trace_type']=str(type(result))
    report['trace_part_types']=[str(type(v)) for v in result] if isinstance(result,(tuple,list)) else []
finally:
    aa.destroy_actor(actor)
Path('S:/UE_WorkSpace/SilverChoir/Saved/SandboxModelOptimization/candidate_state.json').write_text(json.dumps(report,indent=2),encoding='utf8')
