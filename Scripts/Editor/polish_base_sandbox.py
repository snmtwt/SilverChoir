import unreal
aa=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for a in aa.get_all_level_actors():
    if a.get_actor_label().startswith('BaseSandbox_LightStrip'):
        p=a.get_actor_location();a.set_actor_location(unreal.Vector(235 if p.x>0 else -235,p.y,91),False,False)
    if a.get_actor_label()=='CAM_BaseSandbox_Overview':a.camera_component.set_editor_property('field_of_view',74.)
assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
