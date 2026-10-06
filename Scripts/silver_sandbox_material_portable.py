"""Anchor copied Natural V5 material graphs to a movable static sandbox.

Run ``patch_materials(terrain_master, water_master)`` in Unreal Python after
duplicating the original assets. This module never imports, creates, saves, or
assigns assets. It only edits the two explicitly supplied duplicate graphs.
Positions remain authored centimetres; arbitrary translation, rotation, and
nonzero nonuniform object scale are supported for a StaticMeshComponent.
"""
import unreal

ML = unreal.MaterialEditingLibrary
PREFIX = "SilverSandboxPortable: "


def _nodes(material):
    # Rebuilt source masters may retain disconnected old expressions. Traverse
    # only nodes contributing to an active material output, never orphan copies.
    pending = []
    for name in ("MP_BASE_COLOR", "MP_NORMAL", "MP_ROUGHNESS", "MP_AMBIENT_OCCLUSION",
                 "MP_OPACITY", "MP_OPACITY_MASK", "MP_SPECULAR", "MP_METALLIC",
                 "MP_WORLD_POSITION_OFFSET", "MP_EMISSIVE_COLOR"):
        node = ML.get_material_property_input_node(material, getattr(unreal.MaterialProperty, name))
        if node is not None:
            pending.append(node)
    seen = set()
    active = []
    while pending:
        node = pending.pop()
        key = node.get_path_name()
        if key in seen:
            continue
        seen.add(key)
        active.append(node)
        pending.extend(child for child in ML.get_inputs_for_material_expression(material, node)
                       if child is not None)
    return active


def _description(node):
    if isinstance(node, unreal.MaterialExpressionCustom):
        return str(node.get_editor_property("description"))
    return str(node.get_editor_property("desc"))


def _one(nodes, cls=None, description=None):
    matches = [node for node in nodes
               if (cls is None or isinstance(node, cls))
               and (description is None or _description(node) == description)]
    assert len(matches) == 1, (str(cls), description, len(matches))
    return matches[0]


def _node(material, cls, label, **properties):
    result = ML.create_material_expression(material, cls, -2400, -600)
    assert result
    result.set_editor_property("desc", PREFIX + label)
    for key, value in properties.items():
        result.set_editor_property(key, value)
    return result


def _wire(source, dest, inlet, outlet=""):
    # MaterialEditingLibrary uses the graph's shortened display pin name.
    # UE maps an FExpressionInput named "Input" to NAME_None; an empty string
    # explicitly addresses GetInput(0) and works for both Transform classes.
    connection_name = "" if inlet == "Input" else inlet
    assert ML.connect_material_expressions(source, outlet, dest, connection_name), \
        (source.get_path_name(), outlet, dest.get_path_name(), inlet,
         list(ML.get_material_expression_input_names(dest)))


def _custom(material, description, code, sources, size=3):
    inputs = []
    for name in sources:
        entry = unreal.CustomInput()
        entry.set_editor_property("input_name", name)
        inputs.append(entry)
    node = _node(material, unreal.MaterialExpressionCustom, description,
                 description=PREFIX + description, code=code, inputs=inputs,
                 output_type=getattr(unreal.CustomMaterialOutputType, "CMOT_FLOAT" + str(size)))
    for name, source in sources.items():
        if isinstance(source, tuple):
            _wire(source[0], node, name, source[1])
        else:
            _wire(source, node, name)
    return node


def _position_to_local(material, world_position):
    world_position.set_editor_property("world_position_shader_offset",
        unreal.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
    result = _node(material, unreal.MaterialExpressionTransformPosition,
                   "Authored position from object-local centimetres",
                   transform_source_type=unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD,
                   transform_type=unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    _wire(world_position, result, "Input")
    return result


def _transform_vector(material, source, local_to_world, description, outlet=""):
    result = _node(material, unreal.MaterialExpressionTransform, description,
        transform_source_type=(unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL
                               if local_to_world else unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD),
        transform_type=(unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD
                        if local_to_world else unreal.MaterialVectorCoordTransform.TRANSFORM_LOCAL))
    _wire(source, result, "Input", outlet)
    return result


def _basis(material):
    output = {}
    for key, rgb in [("Bx", (1, 0, 0)), ("By", (0, 1, 0)), ("Bz", (0, 0, 1))]:
        constant = _node(material, unreal.MaterialExpressionConstant3Vector,
                         key + " local unit axis", constant=unreal.LinearColor(*rgb, 1))
        output[key] = _transform_vector(material, constant, True, key + " scaled world axis")
    return output


def _normal_to_local(material, world_normal, basis):
    # If B contains the scaled orthogonal object axes, local covector = B^T Nw.
    # A plain inverse-vector transform would be wrong under nonuniform scaling.
    return _custom(material, "Recover authored normal with transpose basis",
        "return normalize(float3(dot(N,Bx),dot(N,By),dot(N,Bz)));",
        dict(N=world_normal, **basis))


def _normal_to_world(material, local_normal, basis):
    # B^-T = [Bx/|Bx|^2, By/|By|^2, Bz/|Bz|^2] for an orthogonal R*S.
    return _custom(material, "Inverse-transpose normal for nonuniform model scale",
        "return normalize(Bx*N.x/max(dot(Bx,Bx),1e-20)"
        "+By*N.y/max(dot(By,By),1e-20)+Bz*N.z/max(dot(Bz,Bz),1e-20));",
        dict(N=local_normal, **basis))


def _guard(material):
    assert isinstance(material, unreal.Material), str(material)
    assert not material.get_path_name().startswith("/Game/SandboxMap/"), \
        "Pass duplicated destination-project masters; original SandboxMap assets are protected"
    assert not any(_description(node).startswith(PREFIX) for node in _nodes(material)), \
        "Material already patched or partially patched; use a fresh duplicate"
    assert not material.get_editor_property("tangent_space_normal")


def _parameters(nodes):
    result = {}
    for node in nodes:
        if isinstance(node, unreal.MaterialExpressionScalarParameter):
            result[str(node.get_editor_property("parameter_name"))] = {
                "kind": "scalar", "value": float(node.get_editor_property("default_value"))}
        elif isinstance(node, unreal.MaterialExpressionVectorParameter):
            value = node.get_editor_property("default_value")
            result[str(node.get_editor_property("parameter_name"))] = {
                "kind": "vector", "value": [value.r, value.g, value.b, value.a]}
        elif isinstance(node, (unreal.MaterialExpressionTextureObjectParameter,
                               unreal.MaterialExpressionTextureSampleParameter2D)):
            tex = node.get_editor_property("texture")
            result[str(node.get_editor_property("parameter_name"))] = {
                "kind": "texture", "asset": tex.get_path_name() if tex else None}
    return result


def _patch_terrain(material):
    _guard(material)
    original = _nodes(material)
    before = _parameters(original)
    shader = _one(original, unreal.MaterialExpressionCustom,
                  "Stochastic PBR terrain / metres / world-space normals")
    p = _position_to_local(material, _one(original, unreal.MaterialExpressionWorldPosition))
    basis = _basis(material)
    n = _normal_to_local(material, _one(original, unreal.MaterialExpressionVertexNormalWS), basis)
    _wire(p, shader, "P")
    _wire(n, shader, "N")
    normal_world = _normal_to_world(material, (shader, "WorldNormal"), basis)
    assert ML.connect_material_property(normal_world, "", unreal.MaterialProperty.MP_NORMAL)
    assert before == _parameters(_nodes(material)), "Terrain parameter defaults changed"
    return {"asset": material.get_path_name(), "authored_position_inputs": 1,
            "normal_transform": "transpose to local / inverse transpose to world",
            "parameter_defaults_preserved": True, "parameter_count": len(before)}


def _patch_water(material):
    _guard(material)
    original = _nodes(material)
    before = _parameters(original)
    world_position = _one(original, unreal.MaterialExpressionWorldPosition)
    p = _position_to_local(material, world_position)
    basis = _basis(material)
    local_geom = _normal_to_local(material, _one(original, unreal.MaterialExpressionVertexNormalWS), basis)
    # All four original geographic consumers use a named P input.
    consumers = []
    for node in original:
        if isinstance(node, unreal.MaterialExpressionCustom):
            names = [str(i.get_editor_property("input_name")) for i in node.get_editor_property("inputs")]
            if "P" in names:
                _wire(p, node, "P")
                consumers.append(_description(node))
    assert len(consumers) == 4, consumers
    normal = _one(original, unreal.MaterialExpressionCustom,
                  "World normal: filtered micro ripples and slow long waves")
    _wire(local_geom, normal, "N")
    normal_world = _normal_to_world(material, normal, basis)
    assert ML.connect_material_property(normal_world, "", unreal.MaterialProperty.MP_NORMAL)
    response = _one(original, unreal.MaterialExpressionCustom,
                    "Depth absorption, inward corridor fade and subdued wet reflection")
    _wire(normal_world, response, "N")
    displacement = _one(original, unreal.MaterialExpressionCustom,
                        "Physical water waves, zero within 25m of shore")
    displacement_world = _transform_vector(material, displacement, True,
                                           "Scaled and rotated physical wave displacement")
    assert ML.connect_material_property(displacement_world, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    view = _one(original, unreal.MaterialExpressionCameraVectorWS)
    unit_view = _custom(material, "Unit world view ray", "return normalize(V);", {"V": view})
    local_view = _transform_vector(material, unit_view, False, "Authored view ray per world centimetre")
    for distance_description, fade_description in [
        ("Depth colour metres to centimetres", "Normalized view-ray water thickness"),
        ("Shore feather metres to centimetres", "Soft physical terrain intersection")]:
        authored_distance = _one(original, unreal.MaterialExpressionCustom, distance_description)
        fade = _one(original, unreal.MaterialExpressionDepthFade, fade_description)
        scaled_distance = _custom(material, "View-aware scaled " + distance_description,
            "return D/max(length(V),1e-12);", {"D": authored_distance, "V": local_view}, 1)
        _wire(scaled_distance, fade, "FadeDistance")
    assert before == _parameters(_nodes(material)), "Water parameter defaults changed"
    return {"asset": material.get_path_name(), "authored_position_inputs": len(consumers),
            "position_consumers": consumers,
            "normal_transform": "transpose to local / inverse transpose to world",
            "wave_transform": "local displacement vector to world including object scale",
            "depth_fade_transform": "authored distance / length(inverse object transform of unit world view ray)",
            "parameter_defaults_preserved": True, "parameter_count": len(before)}


def patch_materials(terrain_master, water_master, recompile=True):
    """Patch two fresh duplicates; return a serializable validation report.

    The caller owns asset saves and assigning material instances. Zero object
    scale and sheared component matrices are intentionally outside the contract.
    """
    _guard(terrain_master)
    _guard(water_master)
    report = {"terrain": _patch_terrain(terrain_master), "water": _patch_water(water_master),
              "translation_rotation_nonuniform_scale": True,
              "nanite_supported": False, "saves_or_assignments_performed": False}
    for key, material in [("terrain", terrain_master), ("water", water_master)]:
        ML.layout_material_expressions(material)
        if recompile:
            errors = list(ML.recompile_material(material) or [])
            report[key]["compile_errors"] = [str(e) for e in errors]
            assert not errors, (key, errors)
            statistics = ML.get_statistics(material)
            report[key]["statistics"] = {name: int(statistics.get_editor_property(name)) for name in
                ("num_pixel_shader_instructions", "num_vertex_shader_instructions", "num_samplers", "num_pixel_texture_samples")}
    report["ok"] = True
    return report
