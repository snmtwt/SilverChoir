"""UE 5.8 sandbox mesh import helpers. Importing this file performs no asset writes.

In an Unreal Python session:
    exec(open(SCRIPT, encoding="utf-8").read())
    prepare_candidate()  # Only writes the separate candidate asset and JSON report.

After visual review of all candidate LODs, pass the report file's SHA-256 explicitly:
    promote_candidate("<reviewed candidate_validation.json SHA-256>")

Promotion preserves the original asset path/object and first creates a source backup.
No Blender process, editor launch, or automatic promotion occurs in this script.
"""

import contextlib
import hashlib
import json
from pathlib import Path
from datetime import datetime, timezone

import unreal


SOURCE_PATH = "/Game/Meshs/Map/SM_SandboxMap"
CANDIDATE_PATH = "/Game/Meshs/Map/Optimization/SM_SandboxMap_Candidate"
OUTPUT = Path(unreal.Paths.project_saved_dir()) / "SandboxModelOptimization"
REPORT_PATH = OUTPUT / "candidate_validation.json"
SOURCE_DIR = Path(unreal.Paths.project_dir()) / "SourceAssets" / "SandboxMap" / "Optimized"
DEFAULT_FBXS = [SOURCE_DIR / ("lod%d.fbx" % i) for i in range(3)]
SCREEN_SIZES = [1.0, 0.45, 0.18]
MESH_PROPERTIES = (
    "positive_bounds_extension", "negative_bounds_extension",
    "light_map_resolution", "light_map_coordinate_index", "lod_for_collision",
)


def _check(condition, message):
    if not condition:
        raise RuntimeError(message)


def _smes():
    return unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)


def _sha(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def _load(path):
    mesh = unreal.EditorAssetLibrary.load_asset(path)
    _check(isinstance(mesh, unreal.StaticMesh), "Missing static mesh: " + path)
    return mesh


def _role(slot):
    name = str(slot.get_editor_property("material_slot_name")).lower()
    if "water" in name:
        return "water"
    if "terrain" in name:
        return "terrain"
    raise RuntimeError("Unrecognized material slot; refusing positional remap: " + name)


def _slots(mesh):
    return list(mesh.get_editor_property("static_materials"))


def _roles(mesh):
    return [_role(slot) for slot in _slots(mesh)]


def _vec(vector):
    return [float(vector.x), float(vector.y), float(vector.z)]


def _snapshot(mesh):
    subsystem = _smes()
    bounds = mesh.get_bounds()
    slots = _slots(mesh)
    lods = []
    for lod in range(subsystem.get_lod_count(mesh)):
        sections = []
        for section in range(mesh.get_num_sections(lod)):
            slot = subsystem.get_lod_material_slot(mesh, lod, section)
            sections.append({
                "slot": slot, "role": _role(slots[slot]),
                "collision": subsystem.is_section_collision_enabled(mesh, lod, section),
                "shadow": subsystem.is_section_cast_shadow_enabled(mesh, lod, section),
            })
        lods.append({
            "triangles": mesh.get_num_triangles(lod),
            "vertices": mesh.get_num_vertices(lod),
            "uv_channels": subsystem.get_num_uv_channels(mesh, lod),
            "sections": sections,
        })
    body = mesh.get_editor_property("body_setup")
    return {
        "path": mesh.get_path_name(),
        "bounds_origin": _vec(bounds.origin), "bounds_extent": _vec(bounds.box_extent),
        "allow_cpu_access": bool(mesh.get_editor_property("allow_cpu_access")),
        "nanite_enabled": bool(subsystem.get_nanite_settings(mesh).get_editor_property("enabled")),
        "screen_sizes": list(subsystem.get_lod_screen_sizes(mesh)),
        "collision_complexity": str(body.get_editor_property("collision_trace_flag")),
        "simple_collision_count": subsystem.get_simple_collision_count(mesh),
        "slots": [{"name": str(s.get_editor_property("material_slot_name")),
                   "material": s.get_editor_property("material_interface").get_path_name()
                   if s.get_editor_property("material_interface") else None}
                  for s in slots],
        "lods": lods,
    }


@contextlib.contextmanager
def _legacy_fbx():
    # ImportLOD also routes through Interchange unless the translator is disabled.
    # Its Interchange branch can return success before checking the import result.
    cvar = "Interchange.FeatureFlags.Import.FBX"
    previous = unreal.SystemLibrary.get_console_variable_int_value(cvar)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    unreal.SystemLibrary.execute_console_command(world, cvar + " 0")
    try:
        yield
    finally:
        unreal.SystemLibrary.execute_console_command(world, cvar + " " + str(previous))


def _import_lod0(filename):
    options = unreal.FbxImportUI()
    for name, value in {
        "automated_import_should_detect_type": False,
        "mesh_type_to_import": unreal.FBXImportType.FBXIT_STATIC_MESH,
        "import_mesh": True, "import_as_skeletal": False,
        "import_materials": False, "import_textures": False,
    }.items():
        options.set_editor_property(name, value)
    data = options.get_editor_property("static_mesh_import_data")
    for name, value in {
        "combine_meshes": True, "import_mesh_lo_ds": False,
        "auto_generate_collision": False, "build_nanite": False,
        "generate_lightmap_u_vs": False, "reorder_material_to_fbx_order": False,
        "normal_import_method": unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS,
        "import_translation": unreal.Vector(0.0, 0.0, 0.0),
        "import_rotation": unreal.Rotator(0.0, 0.0, 0.0),
        "import_uniform_scale": 1.0, "convert_scene": True, "convert_scene_unit": True,
        "force_front_x_axis": False, "transform_vertex_to_absolute": True,
        "bake_pivot_in_vertex": False,
    }.items():
        data.set_editor_property(name, value)
    task = unreal.AssetImportTask()
    for name, value in {
        "filename": str(filename),
        "destination_path": CANDIDATE_PATH.rsplit("/", 1)[0],
        "destination_name": CANDIDATE_PATH.rsplit("/", 1)[1],
        "replace_existing": True, "replace_existing_settings": True,
        "automated": True, "save": False, "async_": False,
        "factory": unreal.FbxFactory(), "options": options,
    }.items():
        task.set_editor_property(name, value)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    objects = list(task.get_objects())
    candidate = _load(CANDIDATE_PATH)
    _check(candidate in objects, "FBX task did not import the expected candidate asset.")
    return candidate


def _restore_settings(mesh, source, source_snapshot):
    subsystem = _smes()
    source_slots = _slots(source)
    _check(len(source_slots) == 2 and set(_roles(source)) == {"terrain", "water"},
           "Source must have exactly the two terrain/water slots.")
    role_to_slot = {_role(slot): index for index, slot in enumerate(source_slots)}
    current_roles = _roles(mesh)
    mapping = []
    for lod in range(subsystem.get_lod_count(mesh)):
        sections = []
        for section in range(mesh.get_num_sections(lod)):
            role = current_roles[subsystem.get_lod_material_slot(mesh, lod, section)]
            sections.append((section, role))
        _check(len(sections) == 2 and {role for _, role in sections} == {"terrain", "water"},
               "LOD%d did not retain one terrain section and one water section." % lod)
        mapping.append(sections)
    shadows = {section["role"]: section["shadow"] for section in source_snapshot["lods"][0]["sections"]}
    mesh.set_editor_property("static_materials", source_slots)
    for name in MESH_PROPERTIES:
        mesh.set_editor_property(name, source.get_editor_property(name))
    body = mesh.get_editor_property("body_setup")
    source_body = source.get_editor_property("body_setup")
    for name in ("collision_trace_flag", "agg_geom", "phys_material"):
        body.set_editor_property(name, source_body.get_editor_property(name))
    subsystem.set_allow_cpu_access(mesh, True)
    nanite = subsystem.get_nanite_settings(mesh)
    nanite.set_editor_property("enabled", False)  # Preserve the translucent water rendering path.
    subsystem.set_nanite_settings(mesh, nanite, True)
    for lod, sections in enumerate(mapping):
        for section, role in sections:
            subsystem.set_lod_material_slot(mesh, role_to_slot[role], lod, section)
            subsystem.enable_section_collision(mesh, role == "terrain", lod, section)
            subsystem.enable_section_cast_shadow(mesh, shadows[role], lod, section)
    # Per-section setters rebuild; write screen sizes last.
    _check(subsystem.set_lod_screen_sizes(mesh, SCREEN_SIZES), "Setting LOD screen sizes failed.")


def _validate(candidate, source_snapshot):
    current = _snapshot(candidate)
    _check(len(current["lods"]) == 3, "Exactly three LODs are required.")
    counts = [lod["triangles"] for lod in current["lods"]]
    _check(0 < counts[2] < counts[1] < counts[0] < source_snapshot["lods"][0]["triangles"],
           "Triangle counts must decrease from original through LOD0, LOD1 and LOD2: " + str(counts))
    _check(current["slots"] == source_snapshot["slots"], "Material slot names/order/references changed.")
    _check(current["allow_cpu_access"] and not current["nanite_enabled"], "CPU/Nanite settings mismatch.")
    _check(current["collision_complexity"] == source_snapshot["collision_complexity"], "Collision mode changed.")
    _check(current["simple_collision_count"] == source_snapshot["simple_collision_count"], "Simple collision changed.")
    expected_shadows = {s["role"]: s["shadow"] for s in source_snapshot["lods"][0]["sections"]}
    for lod in current["lods"]:
        _check(lod["uv_channels"] == source_snapshot["lods"][0]["uv_channels"], "UV channel count changed.")
        for section in lod["sections"]:
            _check(section["collision"] == (section["role"] == "terrain"), "Terrain/water collision mismatch.")
            _check(section["shadow"] == expected_shadows[section["role"]], "Section shadow setting changed.")
    for key in ("bounds_origin", "bounds_extent"):
        for axis, (before, after) in enumerate(zip(source_snapshot[key], current[key])):
            tolerance = max(1.0, abs(source_snapshot["bounds_extent"][axis]) * 0.001)
            _check(abs(before - after) <= tolerance,
                   "%s axis %d changed: %.4f -> %.4f (tolerance %.4f)" % (key, axis, before, after, tolerance))
    _check(all(abs(a - b) < 0.00001 for a, b in zip(current["screen_sizes"], SCREEN_SIZES)), "LOD screens mismatch.")
    return current


def _compare_surface_landmarks(source, candidate):
    """Compare asymmetric terrain heights and face normals with component-only complex traces."""
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    probe_actor = actor_subsystem.spawn_actor_from_class(
        unreal.StaticMeshActor, unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator(), True)
    _check(probe_actor is not None, "Cannot create the transient collision probe actor.")
    try:
        component = probe_actor.get_component_by_class(unreal.StaticMeshComponent)
        component.set_mobility(unreal.ComponentMobility.MOVABLE)
        # Editor spawning may snap/adjust placement; traces use the mesh's local centimetres.
        probe_actor.set_actor_location_and_rotation(unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator(), False, True)
        probe_actor.set_actor_scale3d(unreal.Vector(1.0, 1.0, 1.0))
        component.set_collision_enabled(unreal.CollisionEnabled.QUERY_ONLY)
        bounds = source.get_bounds()
        center, extent = bounds.origin, bounds.box_extent
        points = []
        # Deliberately asymmetric locations catch mirrored and rotated imports whose bounds agree.
        for index in range(48):
            nx = (((index * 17 + 5) % 53) + 0.31) / 53.0 * 1.84 - 0.92
            ny = (((index * 23 + 11) % 59) + 0.67) / 59.0 * 1.78 - 0.89
            points.append((center.x + extent.x * nx, center.y + extent.y * ny))

        def probe(mesh):
            _check(component.set_static_mesh(mesh), "Setting probe mesh failed.")
            samples = []
            for x, y in points:
                result = component.line_trace_component(
                    unreal.Vector(x, y, center.z + extent.z + 10000.0),
                    unreal.Vector(x, y, center.z - extent.z - 10000.0), True, False, False)
                # UE 5.8 PyGenUtil::PackReturnValues removes a bool return when there
                # are out parameters: false becomes None; true becomes the out tuple.
                # PrimitiveComponent.h declares outputs in this exact order:
                # HitLocation, HitNormal, BoneName, OutHit. No HitResult reflection.
                if result is None:
                    samples.append(None)
                else:
                    _check(isinstance(result, tuple) and len(result) == 4,
                           "Unexpected line_trace_component return: " + repr(result))
                    hit_location, hit_normal, _, _ = result
                    _check(isinstance(hit_location, unreal.Vector) and isinstance(hit_normal, unreal.Vector),
                           "Unexpected line_trace_component location/normal output types.")
                    samples.append({"height": float(hit_location.z), "normal": _vec(hit_normal)})
            return samples

        original, reduced = probe(source), probe(candidate)
        errors, normal_dots, samples = [], [], []
        for (x, y), before, after in zip(points, original, reduced):
            _check((before is None) == (after is None), "Terrain coverage changed at %.2f,%.2f" % (x, y))
            if before is None:
                continue
            error = abs(before["height"] - after["height"])
            dot = sum(a * b for a, b in zip(before["normal"], after["normal"]))
            errors.append(error)
            normal_dots.append(dot)
            samples.append({"xy_cm": [x, y], "source": before, "candidate": after,
                            "height_error_cm": error, "normal_dot": dot})
        _check(len(samples) >= 24, "Too few terrain complex-trace hits; review collision before promotion.")
        _check(max(errors) <= max(100.0, extent.z * 0.02), "Surface landmarks differ too much; check units/orientation.")
        _check(sum(normal_dots) / len(normal_dots) >= 0.85, "Surface normals differ too much; check winding/orientation.")
        return {"passed": True, "sample_count": len(samples), "max_height_error_cm": max(errors),
                "mean_height_error_cm": sum(errors) / len(errors),
                "mean_normal_dot": sum(normal_dots) / len(normal_dots), "samples": samples}
    finally:
        actor_subsystem.destroy_actor(probe_actor)


def prepare_candidate(fbx_paths=None):
    """Import and validate a separate candidate. Does not mutate SM_SandboxMap."""
    paths = [Path(p).resolve() for p in (fbx_paths or DEFAULT_FBXS)]
    _check(len(paths) == 3 and all(p.is_file() for p in paths), "Provide three existing LOD FBX files.")
    source = _load(SOURCE_PATH)
    source_snapshot = _snapshot(source)
    _check(not source_snapshot["nanite_enabled"], "Source uses Nanite; this standard-LOD path needs review.")
    with _legacy_fbx():
        candidate = _import_lod0(paths[0])
        for index in (1, 2):
            _check(_smes().import_lod(candidate, index, str(paths[index])) == index,
                   "LOD%d import failed." % index)
    _restore_settings(candidate, source, source_snapshot)
    candidate_snapshot = _validate(candidate, source_snapshot)
    landmarks = _compare_surface_landmarks(source, candidate)
    _check(_snapshot(source) == source_snapshot, "Original source was unexpectedly changed.")
    _check(unreal.EditorAssetLibrary.save_loaded_asset(candidate, False), "Candidate save failed.")
    report = {"structural_validation_passed": True, "visual_review_required": True,
              "source": source_snapshot, "candidate": candidate_snapshot,
              "surface_landmarks": landmarks,
              "fbx": [{"path": str(p), "sha256": _sha(p)} for p in paths]}
    _write(REPORT_PATH, report)
    unreal.log("Candidate prepared; visual review required. Report SHA-256: " + _sha(REPORT_PATH))
    return report


def promote_candidate(expected_report_sha256):
    """Explicit second step, called only after reviewing the exact candidate report."""
    _check(REPORT_PATH.is_file() and _sha(REPORT_PATH) == expected_report_sha256,
           "Provide the exact reviewed candidate report SHA-256 before promotion.")
    report = json.loads(REPORT_PATH.read_text(encoding="utf-8"))
    _check(report.get("structural_validation_passed"), "Candidate has not passed structural validation.")
    _check(report.get("surface_landmarks", {}).get("passed"), "Candidate surface landmarks are not verified.")
    source, candidate = _load(SOURCE_PATH), _load(CANDIDATE_PATH)
    _check(_snapshot(source) == report["source"], "Original source changed since candidate creation.")
    _check(_validate(candidate, report["source"]) == report["candidate"], "Candidate changed after validation.")
    for fbx in report["fbx"]:
        _check(_sha(fbx["path"]) == fbx["sha256"], "Candidate FBX changed after validation.")
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    backup_path = "/Game/Meshs/Map/Optimization/Backups/SM_SandboxMap_Before_" + stamp
    backup = unreal.EditorAssetLibrary.duplicate_asset(SOURCE_PATH, backup_path)
    _check(backup and unreal.EditorAssetLibrary.save_loaded_asset(backup, False), "Source backup failed.")
    _check(_snapshot(backup)["lods"] == report["source"]["lods"], "Source backup geometry mismatch.")
    # Keep the original UObject/package; all level and configuration references stay intact.
    try:
        _smes().remove_lods(source)
        for lod in range(3):
            _check(_smes().set_lod_from_static_mesh(source, lod, candidate, lod, True) == lod,
                   "Copying candidate LOD%d to original failed." % lod)
        _restore_settings(source, backup, report["source"])
        # Geometry copying updates per-LOD SourceImportFilename, but not AssetImportData's LOD0 entry.
        import_data = source.get_editor_property("asset_import_data")
        _check(import_data is not None, "Original asset import metadata is missing.")
        import_data.scripted_add_filename(report["fbx"][0]["path"], 0, "Optimized LOD0")
        promoted = _validate(source, report["source"])
        _check(source.get_path_name() == report["source"]["path"], "Original asset path changed.")
        _check(unreal.EditorAssetLibrary.save_loaded_asset(source, False), "Promoted source save failed.")
    except Exception:
        unreal.log_error("Promotion did not complete. Original asset was not explicitly saved after failure. "
                         "Reload its on-disk package before continuing. Saved backup: " + backup_path)
        raise
    result = {"promoted": True, "backup": backup_path, "asset": promoted,
              "reviewed_report_sha256": expected_report_sha256}
    _write(OUTPUT / "promotion_result.json", result)
    return result
