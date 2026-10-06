"""Run with Unreal's PythonScript commandlet; creates only missing display-platform assets."""
import unreal

ROOT = '/Game/System/Object/Unit/Display'
unreal.EditorAssetLibrary.make_directory(ROOT)
assets = unreal.AssetToolsHelpers.get_asset_tools()

def material(name, color, metallic, roughness, emissive=None):
    path = ROOT + '/' + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.load_asset(path)
    mat = assets.create_asset(name, ROOT, unreal.Material, unreal.MaterialFactoryNew())
    lib = unreal.MaterialEditingLibrary
    c = lib.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector)
    c.set_editor_property('constant', unreal.LinearColor(*color, 1))
    lib.connect_material_property(c, '', unreal.MaterialProperty.MP_BASE_COLOR)
    for value, prop in [(metallic, unreal.MaterialProperty.MP_METALLIC), (roughness, unreal.MaterialProperty.MP_ROUGHNESS)]:
        node = lib.create_material_expression(mat, unreal.MaterialExpressionConstant)
        node.set_editor_property('r', value)
        lib.connect_material_property(node, '', prop)
    if emissive:
        node = lib.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector)
        node.set_editor_property('constant', unreal.LinearColor(*emissive, 1))
        lib.connect_material_property(node, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.recompile_material(mat)
    assert unreal.EditorAssetLibrary.save_loaded_asset(mat)
    return mat

materials = [
    material('M_DisplayPlatform_Body', (0.022, 0.033, 0.045), 0.55, 0.48),
    material('M_DisplayPlatform_Top', (0.045, 0.064, 0.078), 0.25, 0.68),
    material('M_DisplayPlatform_Trim', (0.01, 0.27, 0.34), 0.35, 0.34, (0.0, 0.35, 0.48)),
]
path = ROOT + '/SM_UnitDisplayPlatform'
if not unreal.EditorAssetLibrary.does_asset_exist(path):
    mesh = unreal.DynamicMesh()
    primitive = unreal.GeometryScriptPrimitiveOptions()
    revolve = unreal.GeometryScriptRevolveOptions()
    revolve.set_editor_property('hard_normal_angle', 40.0)
    unreal.GeometryScript_Primitives.append_revolve_path(
        mesh, primitive, unreal.Transform(),
        [unreal.Vector2D(r,z) for r,z in [(66,0),(70,2),(70,8),(67,11.5)]],
        revolve, steps=64, capped=True)
    unreal.GeometryScript_Materials.clear_material_i_ds(mesh, 0)
    top = unreal.DynamicMesh()
    unreal.GeometryScript_Primitives.append_cylinder(top, primitive,
        unreal.Transform(location=[0,0,11.5]), radius=66.5, height=0.5, radial_steps=64)
    unreal.GeometryScript_Materials.clear_material_i_ds(top, 1)
    unreal.GeometryScript_MeshEdits.append_mesh(mesh, top, unreal.Transform())
    trim = unreal.DynamicMesh()
    unreal.GeometryScript_Primitives.append_revolve_polygon(trim, primitive, unreal.Transform(),
        [unreal.Vector2D(r,z) for r,z in [(69.8,5),(70.05,5),(70.05,6),(69.8,6)]],
        revolve, radius=0.0, steps=64)
    unreal.GeometryScript_Materials.clear_material_i_ds(trim, 2)
    unreal.GeometryScript_MeshEdits.append_mesh(mesh, trim, unreal.Transform())
    options = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
    options.set_editor_property('enable_collision', True)
    options.set_editor_property('collision_mode', unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    static_mesh, outcome = unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(mesh, path, options)
    assert static_mesh, str(outcome)
    for i, mat in enumerate(materials):
        static_mesh.set_material(i, mat)
    assert unreal.EditorAssetLibrary.save_loaded_asset(static_mesh)
    unreal.log('DISPLAY_PLATFORM_CREATED ' + str(static_mesh.get_bounding_box()))
unreal.log('DISPLAY_PLATFORM_ASSETS_OK')
