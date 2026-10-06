"""Add ten fictional test templates while preserving all existing rows."""
from pathlib import Path
import datetime
import json
import shutil
import unreal

root = Path(unreal.Paths.project_dir())
folder = '/Game/System/SubSystem/PlayerUnitSubSystem'
table = unreal.load_asset(folder + '/DT_UnitTemplates')
assert table and table.get_editor_property('row_struct') == unreal.UnitTemplate.static_struct()
original = unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table)
rows = json.loads(original)
backup = root / 'Saved/UnitTemplateBackups' / datetime.datetime.now().strftime('%Y%m%d_%H%M%S')
backup.mkdir(parents=True, exist_ok=True)
(backup / 'DT_UnitTemplates.json').write_text(original, encoding='utf-8')
shutil.copy2(root / 'Content/System/SubSystem/PlayerUnitSubSystem/DT_UnitTemplates.uasset', backup)

portraits = []
for index in range(1, 5):
    name = f'T_TestPortrait_{index:02d}'
    path = folder + '/Portraits/' + name
    texture = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if not texture:
        task = unreal.AssetImportTask()
        task.filename = str(root / f'SourceAssets/Units/Portraits/portrait{index}.png')
        task.destination_path = folder + '/Portraits'
        task.destination_name = name
        task.automated = True
        task.save = True
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        texture = unreal.load_asset(path)
    assert texture
    texture.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_UI)
    texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    texture.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_SIMPLE_AVERAGE)
    texture.set_editor_property('filter', unreal.TextureFilter.TF_TRILINEAR)
    texture.set_editor_property('never_stream', True)
    assert unreal.EditorAssetLibrary.save_loaded_asset(texture, only_if_is_dirty=False)
    portraits.append(texture.get_path_name())

# All biographies and statistics below are fictional test content, not balance rules.
people = [
    ('LinShuang','林','霜','寒锋',26,'Female',0,'突击队员，擅长中距离火力推进。',120,110,32,13,14,12,10,13,72,25,10,18),
    ('BaiZhi','白','芷','白露',25,'Female',1,'战地医护，负责紧急救援与伤员稳定。',100,115,25,8,12,14,15,10,42,8,20,85),
    ('ShenYue','沈','月','夜莺',28,'Female',2,'侦察队员，擅长隐蔽行动与路线勘察。',105,130,27,10,17,15,12,11,65,18,30,25),
    ('LuHeng','陆','衡','磐石',29,'Male',3,'重装队员，负责掩护推进与防线压制。',155,100,45,18,8,10,10,17,68,40,5,15),
    ('XuLi','许','璃','脉冲',24,'Female',0,'电子技术员，擅长入侵终端与无人设备。',95,105,24,8,12,16,18,9,38,20,90,15),
    ('GuNing','顾','宁','银针',27,'Female',1,'精确射手，擅长远距离观察与目标压制。',105,100,28,10,11,18,14,10,92,10,12,20),
    ('TangYao','唐','遥','火花',28,'Female',2,'爆破工程师，负责障碍清除与危险物排查。',115,105,35,13,10,16,15,12,55,88,35,12),
    ('ChenYan','陈','砚','长风',29,'Male',3,'机动队员，擅长侧翼支援与快速转移。',120,135,33,13,16,13,11,13,70,22,15,20),
    ('WenXi','温','曦','晨星',26,'Female',0,'后勤支援队员，具备医疗与电子设备维护经验。',110,115,34,11,11,14,16,12,45,15,60,65),
    ('JiangLan','江','澜','霜叶',25,'Female',1,'新晋多面手，具有均衡的战术训练基础。',110,110,30,12,12,12,12,12,58,35,35,35),
]
keys = ['MaxHealth','MaxStamina','MaxCarryWeight','Strength','Agility','Dexterity','Intelligence','Constitution','Marksmanship','Demolition','Hacking','Medicine']
existing = {r['Name'] for r in rows}
added = []
for p in people:
    slug,last,first,code,age,gender,portrait,bio,*stats = p
    name = 'Test_' + slug
    if name in existing:
        continue
    rows.append({'Name':name,'Profile':{'FirstName':first,'LastName':last,'CodeName':code,
        'PortraitTexture':portraits[portrait], 'Gender':gender,'Age':age,
        'Nationality':'银湾联邦（测试）','Origin':'银湾城（测试）','PersonnelResume':bio},
        'Attributes':dict(zip(keys,stats))})
    added.append(name)
assert unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(rows,ensure_ascii=False))
assert unreal.EditorAssetLibrary.save_loaded_asset(table, only_if_is_dirty=False)
verified = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
by_name = {r['Name']:r for r in verified}
for p in people:
    row = by_name['Test_' + p[0]]
    assert row['Profile']['PortraitTexture'] not in ('None','',None)
    assert row['Profile']['Age'] >= 18
    assert row['Attributes']['MaxHealth'] > 0
unreal.log(f'UNIT_TEST_TEMPLATES_OK added={len(added)} total={len(verified)} portraits={len(portraits)}')
