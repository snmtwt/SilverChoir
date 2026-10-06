"""Import the reviewed Blender files as a separate candidate, leaving the original intact."""
import runpy

helpers = runpy.run_path('S:/UE_WorkSpace/SilverChoir/Scripts/Editor/import_sandbox_lods.py')
helpers['prepare_candidate']()
