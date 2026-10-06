"""Create the native unit selection decal material; keep existing authored assets intact."""
import unreal

PATH = "/Game/System/Object/Unit/Materials"
NAME = "M_UnitSelectionDecal"
assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
ml = unreal.MaterialEditingLibrary
material = assets.load_asset(PATH + "/" + NAME) if assets.does_asset_exist(PATH + "/" + NAME) else None
if material:
    assert isinstance(material, unreal.Material)
    assert material.get_editor_property("material_domain") == unreal.MaterialDomain.MD_DEFERRED_DECAL
    unreal.log("UNIT_SELECTION_DECAL_REUSED " + material.get_path_name())
else:
    assets.make_directory(PATH)
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(NAME, PATH, unreal.Material, unreal.MaterialFactoryNew())
    assert material
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)

    def node(cls, x, y, **props):
        result = ml.create_material_expression(material, cls, x, y)
        assert result
        for key, value in props.items():
            result.set_editor_property(key, value)
        return result

    def scalar(name, value, y):
        return node(unreal.MaterialExpressionScalarParameter, -700, y,
                    parameter_name=name, default_value=float(value), group="Selection Ring")

    uv = node(unreal.MaterialExpressionTextureCoordinate, -700, -240)
    inputs = {"UV": uv, "Radius": scalar("RingRadius", .80, -100),
              "Width": scalar("RingWidth", .025, 30), "Opacity": scalar("RingOpacity", .88, 160)}
    custom_inputs = []
    for name in inputs:
        item = unreal.CustomInput()
        item.set_editor_property("input_name", name)
        custom_inputs.append(item)
    mask = node(unreal.MaterialExpressionCustom, -330, -130, description="Antialiased thin selection ring",
                output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT1, inputs=custom_inputs, code="""
float radius = length((UV.xy - 0.5) * 2.0);
float distanceToRing = abs(radius - clamp(Radius, 0.1, 0.94));
float width = clamp(Width, 0.003, 0.06);
float aa = max(fwidth(radius), 0.001);
float core = 1.0 - smoothstep(width, width + aa, distanceToRing);
float halo = (1.0 - smoothstep(width + aa, width + aa + 0.035, distanceToRing)) * 0.10;
return saturate(core + halo) * saturate(Opacity);
""")
    for name, source in inputs.items():
        assert ml.connect_material_expressions(source, "", mask, name)
    color = node(unreal.MaterialExpressionVectorParameter, -330, 250, parameter_name="RingColor",
                 default_value=unreal.LinearColor(.015, .62, .85, 1), group="Selection Ring")
    strength = scalar("GlowStrength", 1.2, 440)
    glow = node(unreal.MaterialExpressionMultiply, -70, 260)
    assert ml.connect_material_expressions(color, "", glow, "A")
    assert ml.connect_material_expressions(strength, "", glow, "B")
    assert ml.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    assert ml.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    assert ml.connect_material_property(mask, "", unreal.MaterialProperty.MP_OPACITY)
    errors = ml.recompile_material(material)
    assert not errors, str(errors)
    assert assets.save_loaded_asset(material, False)
    unreal.log("UNIT_SELECTION_DECAL_CREATED " + material.get_path_name())

assert ml.get_material_property_input_node(material, unreal.MaterialProperty.MP_OPACITY)
assert ml.get_material_property_input_node(material, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
errors = ml.recompile_material(material)
assert not errors, str(errors)
unreal.log("UNIT_SELECTION_DECAL_ASSET_OK")
