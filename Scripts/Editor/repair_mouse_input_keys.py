import unreal,shutil
from pathlib import Path
from datetime import datetime
p=Path(unreal.Paths.project_dir())
b=p/'Saved/InputSetupBackups'/datetime.now().strftime('%Y%m%d-%H%M%S');b.mkdir(parents=True,exist_ok=True)
shutil.copy2(p/'Content/System/Input/Common/IMC_Common.uasset',b/'IMC_Common.uasset')
c=unreal.load_asset('/Game/System/Input/Common/IMC_Common')
data=c.get_editor_property('default_key_mappings');mappings=data.get_editor_property('mappings')
keys={'IA_MouseLeft':'LeftMouseButton','IA_MouseMiddle':'MiddleMouseButton','IA_MouseRight':'RightMouseButton'}
for i,m in enumerate(mappings):
 if m.action and m.action.get_name() in keys:
  name=keys[m.action.get_name()];key=unreal.Key();key.import_text(name)
  assert key.export_text()==name, str(key.export_text())
  m.set_editor_property('key',key)
  mappings[i]=m
data.set_editor_property('mappings',mappings);c.set_editor_property('default_key_mappings',data)
assert unreal.EditorAssetLibrary.save_loaded_asset(c, False)
for m in c.get_editor_property('default_key_mappings').get_editor_property('mappings'):
 assert m.key.export_text()==keys[m.action.get_name()]
 unreal.log('REPAIRED_KEY '+m.action.get_name()+' '+m.key.export_text())
unreal.log('MOUSE_KEYS_REPAIRED')


