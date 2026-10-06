"""Create scene-local GSM materials without modifying SM_SandboxMap or its assets.

Run ``ensure_materials()`` in Unreal Editor Python. The returned ``assets`` map
contains MaterialInterface objects for component overrides and tile defaults;
``report`` contains only JSON-compatible values. Repeated calls reuse validated
owned assets and do not duplicate the clipping graphs.
"""
import unreal


DESTINATION = "/Game/System/Map/BaseMap/Sandbox/Materials"
SOURCE = "/Game/Meshs/Map/Materials"
OWNER_KEY = "BaseSandboxMaterialOwner"
OWNER = "SilverChoir.BaseTemplateSandbox"
VERSION_KEY = "BaseSandboxMaterialVersion"
VERSION = "1"
SOURCE_KEY = "BaseSandboxMaterialSource"
PREFIX = "BaseSandbox: "
ML = unreal.MaterialEditingLibrary
ASSETS = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)

NAMES = {
    "terrain_master": "M_BaseSandboxTerrain",
    "water_master": "M_BaseSandboxWater",
    "terrain": "MI_BaseSandboxTerrain",
    "water": "MI_BaseSandboxWater",
    "grid": "M_BaseSandboxGrid",
}
SOURCES = {
    "terrain_master": SOURCE + "/M_SandboxTerrain",
    "water_master": SOURCE + "/M_SandboxWater",
    "terrain": SOURCE + "/MI_SandboxTerrain",
    "water": SOURCE + "/MI_SandboxWater",
}


def _require(condition, message):
    if not condition:
        raise RuntimeError(message)


def _node(material, cls, label, **properties):
    node = ML.create_material_expression(material, cls, -1200, 400)
    _require(node is not None, "Cannot create material expression: " + label)
    node.set_editor_property("desc", PREFIX + label)
    for name, value in properties.items():
        node.set_editor_property(name, value)
    return node


def _wire(source, destination, inlet, outlet=""):
    _require(ML.connect_material_expressions(source, outlet, destination, inlet),
             "Cannot connect {}:{} -> {}:{}".format(source.get_name(), outlet,
                                                      destination.get_name(), inlet))


def _property(source, material_property, outlet=""):
    _require(ML.connect_material_property(source, outlet, material_property),
             "Cannot connect material property: " + str(material_property))


def _scalar(material, name, value):
    return _node(material, unreal.MaterialExpressionScalarParameter, name,
                 parameter_name=name, default_value=float(value),
                 group="Base Sandbox")


def _vector(material, name, value):
    return _node(material, unreal.MaterialExpressionVectorParameter, name,
                 parameter_name=name, default_value=unreal.LinearColor(*value),
                 group="Base Sandbox")


def _custom(material, label, code, inputs, output_type):
    custom_inputs = []
    for name in inputs:
        entry = unreal.CustomInput()
        entry.set_editor_property("input_name", name)
        custom_inputs.append(entry)
    node = _node(material, unreal.MaterialExpressionCustom, label,
                 description=PREFIX + label, code=code, inputs=custom_inputs,
                 output_type=output_type)
    for name, source in inputs.items():
        _wire(source, node, name)
    return node


def _bounds_mask(material):
    position = _node(material, unreal.MaterialExpressionWorldPosition,
                     "Absolute world position for GSM bounds",
                     world_position_shader_offset=
                     unreal.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
    inputs = {
        "P": position,
        "Enabled": _scalar(material, "GSM_MapBoundsMaskEnabled", 0),
        "Center": _vector(material, "GSM_MapBoundsCenter", (0, 0, 0, 0)),
        "AxisX": _vector(material, "GSM_MapBoundsAxisX", (1, 0, 0, 0)),
        "AxisY": _vector(material, "GSM_MapBoundsAxisY", (0, 1, 0, 0)),
        "HalfSize": _vector(material, "GSM_MapBoundsHalfSize", (1000, 1000, 0, 0)),
        "Feather": _scalar(material, "GSM_MapBoundsFeather", 0),
    }
    return _custom(material, "GSM oriented world bounds mask", """
float3 delta = P - Center.rgb;
float2 localPosition = float2(dot(delta, AxisX.rgb), dot(delta, AxisY.rgb));
float2 remaining = max(HalfSize.rg, 0.0) - abs(localPosition);
float edgeDistance = min(remaining.x, remaining.y);
float mask = Feather > 0.0001 ? saturate(edgeDistance / Feather) : step(0.0, edgeDistance);
return lerp(1.0, mask, saturate(Enabled));
""", inputs, unreal.CustomMaterialOutputType.CMOT_FLOAT1)


def _is_patched(material, prop):
    node = ML.get_material_property_input_node(material, prop)
    return (isinstance(node, unreal.MaterialExpressionMultiply) and
            str(node.get_editor_property("desc")) == PREFIX + "Preserve opacity times GSM bounds")


def _patch_surface(material, terrain):
    _require(not material.get_editor_property("use_material_attributes"),
             "Source uses material attributes; refuse to replace its graph outputs")
    prop = (unreal.MaterialProperty.MP_OPACITY_MASK if terrain
            else unreal.MaterialProperty.MP_OPACITY)
    if _is_patched(material, prop):
        return
    if terrain:
        _require(material.get_editor_property("blend_mode") in (
            unreal.BlendMode.BLEND_OPAQUE, unreal.BlendMode.BLEND_MASKED),
            "Unexpected terrain blend mode")
    else:
        _require(material.get_editor_property("blend_mode") == unreal.BlendMode.BLEND_TRANSLUCENT,
                 "Expected translucent water; its shading and opacity must remain intact")
    previous = ML.get_material_property_input_node(material, prop)
    outlet = ML.get_material_property_input_node_output_name(material, prop) if previous else ""
    mask = _bounds_mask(material)
    combined = _node(material, unreal.MaterialExpressionMultiply,
                     "Preserve opacity times GSM bounds", const_a=1.0)
    if previous:
        _wire(previous, combined, "A", outlet)
    _wire(mask, combined, "B")
    if terrain:
        material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    _property(combined, prop)


def _build_grid(material):
    # This master is created exclusively by this helper. Rebuilding an incomplete
    # owned grid graph is safe, unlike rebuilding a duplicated terrain master.
    ML.delete_all_material_expressions(material)
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    uv = _node(material, unreal.MaterialExpressionTextureCoordinate, "Decal UV")
    bounds = _bounds_mask(material)
    stencil = _node(material, unreal.MaterialExpressionSceneTexture, "Only project onto sandbox terrain",
                    scene_texture_id=unreal.SceneTextureId.PPI_CUSTOM_STENCIL)
    selected = _scalar(material, "SelectedAmount", 0)
    opacity = _custom(material, "Fine antialiased tile border", """
float2 centered = abs(UV.xy - 0.5);
float edge = max(centered.x, centered.y);
float selected = saturate(Selected);
float width = max(lerp(Width, SelectedWidth, selected), 0.0001);
// Keep the selected stroke inside its own tile's projection volume.
float extent = min(clamp(Extent, 0.0, 0.5), 0.5 - selected * width);
float distanceToBorder = abs(edge - extent);
float aa = max(fwidth(edge), 0.0001);
float borderMask = 1.0 - smoothstep(width, width + aa, distanceToBorder);
float receiver = lerp(1.0, 1.0 - step(0.5, abs(Stencil.r - StencilValue)), saturate(StencilEnabled));
return borderMask * saturate(lerp(Opacity, SelectedOpacity, selected)) * saturate(Bounds) * (1.0 - saturate(Hidden)) * receiver;
""", {
        "UV": uv,
        "Extent": _scalar(material, "BorderExtent", 0.5),
        "Width": _scalar(material, "BorderWidth", 0.0035),
        "Opacity": _scalar(material, "BorderOpacity", 0.35),
        "Selected": selected,
        "SelectedWidth": _scalar(material, "SelectedBorderWidth", 0.015),
        "SelectedOpacity": _scalar(material, "SelectedBorderOpacity", 1),
        "Hidden": _scalar(material, "HiddenAmount", 0),
        "Bounds": bounds,
        "Stencil": stencil,
        "StencilEnabled": _scalar(material, "GSM_DecalReceiverStencilEnabled", 1),
        "StencilValue": _scalar(material, "GSM_DecalReceiverStencilValue", 71),
    }, unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    color = _vector(material, "BorderColor", (0.32, 0.56, 0.58, 1.0))
    glow = _custom(material, "Selected border emission", "return Color.rgb * lerp(NormalGlow, SelectedGlow, saturate(Selected));", {
        "Color": color,
        "NormalGlow": _scalar(material, "BorderEmissiveStrength", 0.015),
        "SelectedGlow": _scalar(material, "SelectedBorderEmissiveStrength", 4),
        "Selected": selected,
    }, unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    diffuse = _node(material, unreal.MaterialExpressionMultiply, "Low reflectance grid", const_b=0.025)
    _wire(color, diffuse, "A")
    _property(diffuse, unreal.MaterialProperty.MP_BASE_COLOR)
    # The decal pass applies opacity, including receiver/bounds masks, once.
    _property(glow, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    _property(opacity, unreal.MaterialProperty.MP_OPACITY)
    _property(_scalar(material, "BorderSpecular", 0), unreal.MaterialProperty.MP_SPECULAR)
    _property(_scalar(material, "BorderRoughness", 0.9), unreal.MaterialProperty.MP_ROUGHNESS)


def _validate_owned(obj, key):
    expected = unreal.MaterialInstanceConstant if key in ("terrain", "water") else unreal.Material
    _require(isinstance(obj, expected), "Unexpected destination asset class: " + obj.get_path_name())
    _require(ASSETS.get_metadata_tag(obj, OWNER_KEY) == OWNER,
             "Refusing to modify an unowned asset: " + obj.get_path_name())
    _require(ASSETS.get_metadata_tag(obj, SOURCE_KEY) == SOURCES.get(key, "generated"),
             "Destination source metadata differs: " + obj.get_path_name())
    version = ASSETS.get_metadata_tag(obj, VERSION_KEY)
    _require(version in ("", VERSION), "Unsupported sandbox material version: " + str(version))


def _validate_graph(material, key):
    if key == "grid":
        _require(material.get_editor_property("material_domain") ==
                 unreal.MaterialDomain.MD_DEFERRED_DECAL, "Grid material is no longer a decal")
        prop = unreal.MaterialProperty.MP_OPACITY
        node = ML.get_material_property_input_node(material, prop)
        _require(isinstance(node, unreal.MaterialExpressionCustom) and
                 str(node.get_editor_property("description")) == PREFIX + "Fine antialiased tile border",
                 "Grid opacity graph changed; preserve it and inspect manually")
    else:
        terrain = key == "terrain_master"
        prop = unreal.MaterialProperty.MP_OPACITY_MASK if terrain else unreal.MaterialProperty.MP_OPACITY
        _require(_is_patched(material, prop), "Missing owned bounds-mask output: " + material.get_path_name())
        expected = unreal.BlendMode.BLEND_MASKED if terrain else unreal.BlendMode.BLEND_TRANSLUCENT
        _require(material.get_editor_property("blend_mode") == expected,
                 "Sandbox material blend mode changed: " + material.get_path_name())


def ensure_materials(save=True):
    """Return owned materials, preserving all source assets and instance overrides.

    An existing asset must carry the matching owner, source and version metadata.
    Completed graphs are validated and reused; unrelated destination assets cause
    a failure before any asset creation. This function never assigns materials to
    a mesh, actor, level, or project-wide configuration.
    """
    result = {}
    sources = {}
    # Preflight every source and existing destination before any mutations.
    for key, path in SOURCES.items():
        source = unreal.load_asset(path)
        expected = unreal.MaterialInstanceConstant if key in ("terrain", "water") else unreal.Material
        _require(isinstance(source, expected), "Missing or invalid source material: " + path)
        sources[key] = source
    for key, name in NAMES.items():
        path = DESTINATION + "/" + name
        if ASSETS.does_asset_exist(path):
            obj = unreal.load_asset(path)
            _validate_owned(obj, key)
            if key not in ("terrain", "water") and ASSETS.get_metadata_tag(obj, VERSION_KEY) == VERSION:
                _validate_graph(obj, key)
            result[key] = obj
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    created = []
    changed = []
    for key, name in NAMES.items():
        if key in result:
            continue
        if key in SOURCES:
            obj = ASSETS.duplicate_asset(SOURCES[key], DESTINATION + "/" + name)
        else:
            obj = tools.create_asset(name, DESTINATION, unreal.Material, unreal.MaterialFactoryNew())
        _require(obj is not None, "Failed to create sandbox material: " + name)
        ASSETS.set_metadata_tag(obj, OWNER_KEY, OWNER)
        ASSETS.set_metadata_tag(obj, SOURCE_KEY, SOURCES.get(key, "generated"))
        ASSETS.set_metadata_tag(obj, VERSION_KEY, "")
        result[key] = obj
        created.append(key)
    report = {"owner": OWNER, "version": VERSION, "created": created,
              "source_assets_modified": False, "materials": {}}
    for key in ("terrain_master", "water_master", "grid"):
        material = result[key]
        if ASSETS.get_metadata_tag(material, VERSION_KEY) != VERSION:
            if key == "grid":
                _build_grid(material)
            else:
                _patch_surface(material, key == "terrain_master")
            ML.layout_material_expressions(material)
            errors = [str(error) for error in (ML.recompile_material(material) or [])]
            _require(not errors, "Material compile failed for {}: {}".format(key, errors))
            _validate_graph(material, key)
            ASSETS.set_metadata_tag(material, VERSION_KEY, VERSION)
            changed.append(key)
        report["materials"][key] = {"path": material.get_path_name(),
                                    "blend_mode": str(material.get_editor_property("blend_mode")),
                                    "validated": True}
    for key, parent_key in (("terrain", "terrain_master"), ("water", "water_master")):
        instance = result[key]
        parent = result[parent_key]
        if ASSETS.get_metadata_tag(instance, VERSION_KEY) != VERSION:
            ML.set_material_instance_parent(instance, parent)
            # Copied source instance overrides must not undo terrain masking.
            overrides = instance.get_editor_property("base_property_overrides")
            _require(not overrides.get_editor_property("override_blend_mode"),
                     "Source MI overrides blend mode; inspect before installing sandbox materials")
            ML.update_material_instance(instance)
            ASSETS.set_metadata_tag(instance, VERSION_KEY, VERSION)
            changed.append(key)
        _require(instance.get_editor_property("parent") == parent,
                 "Owned material instance parent changed: " + instance.get_path_name())
        report["materials"][key] = {"path": instance.get_path_name(),
                                    "parent": parent.get_path_name(), "validated": True}
    if save:
        # Save even previously completed but dirty assets after a prior interrupted
        # run. The owner guard prevents touching any unrelated package.
        for obj in result.values():
            _require(ASSETS.save_loaded_asset(obj, only_if_is_dirty=True),
                     "Could not save sandbox material: " + obj.get_path_name())
    report.update(ok=True, changed=changed, saved=bool(save))
    return {"assets": result, "report": report}
