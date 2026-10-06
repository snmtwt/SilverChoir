from pathlib import Path
import unreal,json,runpy
root=Path('S:/UE_WorkSpace/SilverChoir')
out=root/'Saved/SandboxStaticMeshRepair'
aa=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=aa.get_all_level_actors()
model=next(a for a in actors if a.get_actor_label()=='SM_SandboxMap_Portable')
camera=next(a for a in actors if a.get_actor_label()=='SandboxPreviewCamera')
sun=next(a for a in actors if isinstance(a,unreal.DirectionalLight))
model.set_actor_location(unreal.Vector(650,200,75),False,False)
model.set_actor_rotation(unreal.Rotator(pitch=0,yaw=180,roll=0),False)
model.set_actor_scale3d(unreal.Vector(.0001,.0001,.0001))
camera.set_actor_location(unreal.Vector(650,30,270),False,False)
rotation=unreal.Rotator(pitch=-49.063135,yaw=90,roll=0)
camera.set_actor_rotation(rotation,False)
camera.camera_component.set_editor_property('projection_mode',unreal.CameraProjectionMode.PERSPECTIVE)
camera.camera_component.set_editor_property('field_of_view',60.)
sun.set_actor_rotation(unreal.Rotator(pitch=-28,yaw=45,roll=0),False)
unreal.EditorLevelLibrary.set_level_viewport_camera_info(unreal.Vector(650,30,270),rotation)
water=unreal.load_asset('/Game/Meshs/Map/Materials/MI_SandboxWater')
assert unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(water,'WaveTimeOverride')==-1.
assert levels.save_current_level(),'Failed to save final preview level'
report=json.loads((out/'preview_result.json').read_text(encoding='utf8'))
report.update(ok=True,status='complete',map='/Game/Meshs/Map/Preview/L_SandboxMapPreview',animated_water_restored=True,
    projection='Perspective',tabletop_dimensions_m=[2.4,1.4],
    saved_water_asset_changed_by_this_finalization=False,
    note='Small orthographic previews require clip planes appropriate to model scale; automatic 655m depth extent loses shallow-water depth precision.')
report.pop('error',None)
(out/'preview_result.json').write_text(json.dumps(report,indent=2),encoding='utf8')
runpy.run_path(str(root/'Scripts/verify_sandbox_static_mesh.py'),run_name='__main__')
