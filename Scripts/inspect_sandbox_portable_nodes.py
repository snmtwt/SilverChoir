import unreal,json
from pathlib import Path
ml=unreal.MaterialEditingLibrary
report=[]
for path in ('/Game/Meshs/Map/Materials/M_SandboxTerrain','/Game/Meshs/Map/Materials/M_SandboxWater'):
    mat=unreal.load_asset(path)
    for node in ml.get_material_expressions(mat):
        if isinstance(node,(unreal.MaterialExpressionTransform,unreal.MaterialExpressionTransformPosition)):
            report.append({'asset':path,'node':node.get_class().get_name(),'desc':str(node.get_editor_property('desc')),'input_names':list(ml.get_material_expression_input_names(node))})
Path('S:/UE_WorkSpace/SilverChoir/Saved/SandboxStaticMeshRepair/node_pins.json').write_text(json.dumps(report,indent=2),encoding='utf8')
