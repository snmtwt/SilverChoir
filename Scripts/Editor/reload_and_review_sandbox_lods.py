"""Reset the task editor to the saved scene before a non-persistent mesh comparison."""
import runpy
import unreal

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level('/Engine/Maps/Entry')
assert levels.load_level('/Game/System/Map/BaseMap/L_BaseTemplate')
# Initialize the same transient grid sizing caches that BeginPlay populates.
# The comparison editor never saves this rebuilt preview back into the map.
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
board = next(actor for actor in actors if isinstance(actor, unreal.BaseSandboxMap))
board.rebuild_map_from_config()
runpy.run_path('S:/UE_WorkSpace/SilverChoir/Scripts/Editor/review_base_sandbox_lods.py')
