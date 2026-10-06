"""Create the initial mouse input assets and assign the main controller defaults."""
import unreal
import shutil
from pathlib import Path
from datetime import datetime

ROOT = '/Game/System/Input'
tools = unreal.AssetToolsHelpers.get_asset_tools()

def asset(folder, name, cls):
    path = ROOT + '/' + folder + '/' + name
    existing = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if existing:
        assert isinstance(existing, cls), path
        return existing, False
    unreal.EditorAssetLibrary.make_directory(ROOT + '/' + folder)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property('data_asset_class', cls)
    result = tools.create_asset(name, ROOT + '/' + folder, cls, factory)
    assert result, path
    return result, True

contexts = {}
for folder in ['Common', 'Base', 'Battle']:
    context, created = asset(folder, 'IMC_' + folder, unreal.InputMappingContext)
    contexts[folder] = context
    if created:
        context.set_editor_property('context_description', unreal.Text(folder + ' input mappings'))
    if folder == 'Common':
        for name, key_name in [('IA_MouseLeft', 'LeftMouseButton'), ('IA_MouseMiddle', 'MiddleMouseButton'), ('IA_MouseRight', 'RightMouseButton')]:
            action, action_created = asset(folder, name, unreal.InputAction)
            if action_created:
                action.set_editor_property('value_type', unreal.InputActionValueType.BOOLEAN)
                action.set_editor_property('consume_input', False)
                action.set_editor_property('consumes_action_and_axis_mappings', False)
            key = unreal.Key()
            key.import_text(key_name)
            assert key.export_text() == key_name
            mappings = context.get_editor_property('default_key_mappings').get_editor_property('mappings')
            if not any(m.action == action and m.key == key for m in mappings):
                context.map_key(action, key)
            unreal.EditorAssetLibrary.save_loaded_asset(action, False)
    unreal.EditorAssetLibrary.save_loaded_asset(context, False)

bp_path = '/Game/System/Map/GameMainMap/BP_GameMainMapPlayerController'
project = Path(unreal.Paths.project_dir())
source = project / 'Content/System/Map/GameMainMap/BP_GameMainMapPlayerController.uasset'
backup = project / 'Saved/InputSetupBackups' / datetime.now().strftime('%Y%m%d-%H%M%S')
backup.mkdir(parents=True, exist_ok=True)
shutil.copy2(source, backup / source.name)
bp = unreal.load_asset(bp_path)
cdo = unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class(bp_path))
for folder, prop in [('Common','common_input_mapping_context'), ('Base','base_input_mapping_context'), ('Battle','battle_input_mapping_context')]:
    cdo.set_editor_property(prop, contexts[folder])
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
assert unreal.EditorAssetLibrary.save_loaded_asset(bp), 'Controller save failed'
for folder, prop in [('Common','common_input_mapping_context'), ('Base','base_input_mapping_context'), ('Battle','battle_input_mapping_context')]:
    assert unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class(bp_path)).get_editor_property(prop) == contexts[folder]
assert len(contexts['Common'].get_editor_property('default_key_mappings').get_editor_property('mappings')) == 3
assert len(contexts['Base'].get_editor_property('default_key_mappings').get_editor_property('mappings')) == 0
assert len(contexts['Battle'].get_editor_property('default_key_mappings').get_editor_property('mappings')) == 0
unreal.log('INPUT_SETUP_OK: Common=3 Base=0 Battle=0; controller references assigned')



