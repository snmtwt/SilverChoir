"""Create missing region assets without replacing existing user assets."""
import unreal
ROOT = "/Game/System/Map/BaseMap"
lib = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
ml = unreal.MaterialEditingLibrary
path = ROOT + "/Materials/M_RegionBlock"
material = unreal.load_asset(path) if lib.does_asset_exist(path) else None
REFRESH_REGION_MATERIAL = False
if material is None or REFRESH_REGION_MATERIAL:
    if material is not None:
        ml.delete_all_material_expressions(material)
    else:
        material = tools.create_asset("M_RegionBlock", ROOT + "/Materials", unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    def node(cls, x, y):
        return ml.create_material_expression(material, cls, x, y)
    color = node(unreal.MaterialExpressionVectorParameter, -700, -200)
    color.set_editor_property("parameter_name", "RegionColor")
    color.set_editor_property("default_value", unreal.LinearColor(.025, .22, .36, 1))
    glow = node(unreal.MaterialExpressionScalarParameter, -700, -60)
    glow.set_editor_property("parameter_name", "GlowStrength")
    glow.set_editor_property("default_value", 1.2)
    normal = node(unreal.MaterialExpressionPixelNormalWS, -1150, 180)
    view = node(unreal.MaterialExpressionCameraVectorWS, -1150, 330)
    dot = node(unreal.MaterialExpressionDotProduct, -950, 180)
    assert ml.connect_material_expressions(normal, "", dot, "A")
    assert ml.connect_material_expressions(view, "", dot, "B")
    absolute = node(unreal.MaterialExpressionAbs, -780, 180)
    assert ml.connect_material_expressions(dot, "", absolute, ml.get_material_expression_input_names(absolute)[0])
    inverse = node(unreal.MaterialExpressionOneMinus, -600, 180)
    assert ml.connect_material_expressions(absolute, "", inverse, ml.get_material_expression_input_names(inverse)[0])
    fresnel = node(unreal.MaterialExpressionPower, -420, 180)
    fresnel.set_editor_property("const_exponent", 3.0)
    assert ml.connect_material_expressions(inverse, "", fresnel, "Base")
    fill = node(unreal.MaterialExpressionScalarParameter, -700, 360)
    fill.set_editor_property("parameter_name", "FillOpacity")
    fill.set_editor_property("default_value", .055)
    emissive = node(unreal.MaterialExpressionMultiply, -350, -150)
    assert ml.connect_material_expressions(color, "", emissive, "A")
    assert ml.connect_material_expressions(glow, "", emissive, "B")
    exposure = node(unreal.MaterialExpressionEyeAdaptationInverse, -80, -150)
    inputs = ml.get_material_expression_input_names(exposure)
    assert ml.connect_material_expressions(emissive, "", exposure, inputs[0])
    assert ml.connect_material_property(exposure, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    edge = node(unreal.MaterialExpressionMultiply, -400, 180)
    edge.set_editor_property("const_b", .55)
    assert ml.connect_material_expressions(fresnel, "", edge, "A")
    opacity = node(unreal.MaterialExpressionAdd, -200, 240)
    assert ml.connect_material_expressions(edge, "", opacity, "A")
    assert ml.connect_material_expressions(fill, "", opacity, "B")
    assert ml.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
    ml.recompile_material(material)
    assert lib.save_loaded_asset(material, only_if_is_dirty=False)

def blueprint(name, folder, parent, widget=False):
    asset_path = folder + "/" + name
    if lib.does_asset_exist(asset_path):
        return unreal.load_asset(asset_path), False
    factory = unreal.WidgetBlueprintFactory() if widget else unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent)
    bp = tools.create_asset(name, folder, None, factory)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    return bp, True

label, fresh = blueprint("WBP_RegionName", ROOT + "/UI", unreal.RegionNameWidget, True)
if fresh:
    assert lib.save_loaded_asset(label, only_if_is_dirty=False)
actor, fresh = blueprint("BP_RegionBlock", ROOT, unreal.RegionBlock)
if fresh:
    defaults = unreal.get_default_object(actor.generated_class())
    defaults.set_editor_property("name_widget_class", label.generated_class())
    defaults.set_editor_property("region_material", material)
    assert lib.save_loaded_asset(actor, only_if_is_dirty=False)
unreal.log("REGION_BLOCK_ASSETS_OK")

