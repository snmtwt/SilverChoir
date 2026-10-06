import json
import unreal
table = unreal.load_asset('/Game/System/SubSystem/PlayerUnitSubSystem/DT_UnitTemplates')
rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
test_rows = [row for row in rows if row['Name'].startswith('Test_')]
assert len(test_rows) >= 10
assert all(row['Profile']['PersonnelResume'] for row in test_rows)
unreal.log('UNIT_RESUME_REDIRECT_OK rows=' + str(len(test_rows)))
