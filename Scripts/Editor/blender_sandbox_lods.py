"""Build reviewable sandbox mesh LOD candidates in Blender, outside Unreal.

Never alters the source FBX, the external SC_Scene project, or UE packages.
Usage: blender --background --factory-startup --python this.py -- --stage inspect
       blender --background source.blend --python this.py -- --stage build
"""
import argparse
import json
import math
from pathlib import Path
import sys
import time
import traceback
import gc
import hashlib
import ast

import bpy
import numpy as np
from mathutils import Vector, Euler
from mathutils.bvhtree import BVHTree
from mathutils.kdtree import KDTree

ROOT = Path("S:/UE_WorkSpace/SilverChoir/Saved/SandboxModelOptimization/Blender")
SOURCE = Path("S:/UE_WorkSpace/SC_Scene/Exports/SandboxNaturalV5_Combined/FBX/SandboxNaturalV5_TerrainAndWater_Import.fbx")
PARSER = argparse.ArgumentParser()
PARSER.add_argument("--stage", choices=("inspect", "build", "finalize"), default="inspect")
PARSER.add_argument("--lods", default="0,1,2")
ARGS = PARSER.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])
ROOT.mkdir(parents=True, exist_ok=True)
START = time.time()


def log(message):
    print("SANDBOX_LOD " + str(message), flush=True)


def write(name, data):
    (ROOT / name).write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding="utf-8")


def coords(mesh):
    values = np.empty(len(mesh.vertices) * 3, dtype=np.float32)
    mesh.vertices.foreach_get("co", values)
    return values.reshape((-1, 3))


def topology(mesh):
    mesh.calc_loop_triangles()
    faces = np.empty(len(mesh.loop_triangles) * 3, dtype=np.int32)
    mesh.loop_triangles.foreach_get("vertices", faces)
    return faces.reshape((-1, 3))


def summary(obj):
    mesh = obj.data
    p = coords(mesh)
    return {"name": obj.name, "vertices": len(p), "triangles": len(topology(mesh)),
            "materials": [m.name if m else None for m in mesh.materials],
            "uv_layers": [layer.name for layer in mesh.uv_layers],
            "bounds_local_min": p.min(axis=0).tolist(), "bounds_local_max": p.max(axis=0).tolist(),
            "matrix_world": [list(row) for row in obj.matrix_world],
            "custom_normals": mesh.has_custom_normals}


def import_source():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    bpy.ops.import_scene.fbx(filepath=str(SOURCE), use_custom_normals=True,
                             use_image_search=False, use_anim=False)
    objects = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
    assert len(objects) == 1, [obj.name for obj in objects]
    obj = objects[0]
    obj.name = "REFERENCE_SM_SandboxMap"
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    obj.hide_render = True
    report = summary(obj)
    assert report["triangles"] == 3066634, report
    assert len(report["materials"]) == 2, report
    report.update(source=str(SOURCE), blender=bpy.app.version_string,
                  scene_unit_scale=bpy.context.scene.unit_settings.scale_length,
                  elapsed_seconds=time.time() - START)
    write("source_inspection.json", report)
    bpy.ops.wm.save_as_mainfile(filepath=str(ROOT / "sandbox_source.blend"), check_existing=False)
    log(report)


def activate(obj):
    bpy.ops.object.select_all(action="DESELECT")
    obj.hide_set(False)
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj


def boundary_indices(faces):
    edges = np.concatenate((faces[:, [0, 1]], faces[:, [1, 2]], faces[:, [2, 0]]))
    edges.sort(axis=1)
    packed = edges[:, 0].astype(np.uint64) << 32 | edges[:, 1].astype(np.uint64)
    unique, counts = np.unique(packed, return_counts=True)
    boundary = unique[counts == 1]
    return np.unique(np.concatenate(((boundary >> 32).astype(np.int32),
                                    (boundary & np.uint64(0xffffffff)).astype(np.int32))))


def split_reference(ref):
    existing = [bpy.data.objects.get("REFERENCE_Terrain"), bpy.data.objects.get("REFERENCE_Water")]
    if all(existing):
        return existing
    obj = ref.copy()
    obj.data = ref.data.copy()
    bpy.context.collection.objects.link(obj)
    activate(obj)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.separate(type="MATERIAL")
    bpy.ops.object.mode_set(mode="OBJECT")
    parts = [o for o in bpy.context.selected_objects if o.type == "MESH"]
    assert len(parts) == 2, [o.name for o in parts]
    parts.sort(key=lambda o: len(o.data.polygons), reverse=True)
    for part, label in zip(parts, ("Terrain", "Water")):
        part.name = "REFERENCE_" + label
        part.hide_render = True
        log(summary(part))
    return parts


def prepare_masks(parts):
    water_levels = np.load("S:/UE_WorkSpace/SC_Scene/Design/SandboxMapConcept/TerrainNaturalV5/Water/water_surface_native_m.npy", mmap_mode="r")
    height, width = water_levels.shape
    image = bpy.data.images.load("S:/UE_WorkSpace/SC_Scene/Design/SandboxMapConcept/TerrainHarborV4/Water/T_WaterCorridor_HarborV4.png", check_existing=False)
    raw = np.empty(width * height * 4, np.float32)
    image.pixels.foreach_get(raw)
    coverage = raw.reshape((height, width, 4))[::-1, :, 0] > .5
    del raw
    bpy.data.images.remove(image)
    result = []
    for index, part in enumerate(parts):
        p = coords(part.data)
        f = topology(part.data)
        outer = boundary_indices(f)
        extreme = np.unique(np.concatenate((p.argmin(axis=0), p.argmax(axis=0))))
        rr = np.clip(np.rint((7000.0 - p[:, 1]) * (height - 1) / 14000.0).astype(np.int32), 0, height - 1)
        cc = np.clip(np.rint((p[:, 0] + 12000.0) * (width - 1) / 24000.0).astype(np.int32), 0, width - 1)
        wet = (p[:, 2] < water_levels[rr, cc]) & coverage[rr, cc]
        shore = np.empty(0, np.int32)
        if index == 0:
            wf = wet[f]
            shore = np.unique(f[wf.any(axis=1) & ~wf.all(axis=1)])
        else:
            log({"water_level_sample_error_m": np.percentile(np.abs(p[:, 2] - water_levels[rr, cc]), [50, 95, 100]).tolist()})
        masks = {"boundary": outer, "extrema": extreme, "shore": shore}
        np.savez_compressed(ROOT / ("terrain_protection.npz" if index == 0 else "water_protection.npz"), **masks)
        log({"part": part.name, "boundary_count": len(outer), "shore_vertices": len(shore), "extrema": p[extreme].tolist()})
        result.append(masks)
        del f, p
        gc.collect()
    return result


def set_protection(obj, ids):
    group = obj.vertex_groups.new(name="SimplificationAllowed")
    # Blender's collapse implementation skips an edge if either endpoint has
    # weight zero. Interior=1; omitted/protected vertices=0, a hard position lock.
    allowed = np.ones(len(obj.data.vertices), bool)
    allowed[ids] = False
    group.add(np.flatnonzero(allowed).tolist(), 1.0, "REPLACE")
    return group.name


def clamp_to_reference_bounds(obj, ref):
    bounds = coords(ref.data)
    p = coords(obj.data)
    corrected = np.clip(p, bounds.min(axis=0), bounds.max(axis=0))
    delta = np.linalg.norm(corrected - p, axis=1)
    if np.any(delta):
        obj.data.vertices.foreach_set("co", corrected.ravel())
        obj.data.update()
    return {"vertices_clamped_to_original_bounds": int(np.count_nonzero(delta)),
            "max_clamp_m": float(delta.max())}


def percentile(values):
    a = np.asarray(values, np.float64)
    return dict(zip(("p50", "p95", "p99", "max"), np.percentile(a, [50, 95, 99, 100]).tolist())) if len(a) else None


def quality(ref, obj, masks, index):
    src = coords(ref.data)
    dst = coords(obj.data)
    faces = topology(obj.data)
    log("Building quality BVH: " + obj.name)
    bvh = BVHTree.FromPolygons(dst.tolist(), faces.tolist(), all_triangles=True)
    rng = np.random.default_rng(27092026 + index)
    random_ids = rng.choice(len(src), min(60000, len(src)), replace=False)
    samples = np.unique(np.concatenate((random_ids, masks["shore"], masks["boundary"], masks["extrema"])))
    err, missed, locations = [], 0, []
    top = float(src[:, 2].max() + 100)
    for i in samples:
        p = src[i]
        hit, _, _, _ = bvh.ray_cast((float(p[0]), float(p[1]), top), (0, 0, -1), 5000.0)
        if hit is None:
            # Numerical edge rays can miss; nearest point still quantifies these.
            hit, _, _, _ = bvh.find_nearest(Vector(p))
            missed += 1
        error = abs(float(hit.z) - float(p[2])) if hit else float("inf")
        err.append(error)
        if error > 2.0 and len(locations) < 10:
            locations.append({"source": p.tolist(), "candidate_z": float(hit.z) if hit else None, "height_error_m": error})
    boundary = boundary_indices(faces)
    tree = KDTree(len(boundary))
    for j, v in enumerate(dst[boundary]):
        tree.insert(Vector(v), j)
    tree.balance()
    # Nearest vertices is a conservative upper bound on contour distance.
    boundary_dist = [tree.find(Vector(p))[2] for p in src[masks["boundary"]]]
    locktree = KDTree(len(dst))
    for j, v in enumerate(dst):
        locktree.insert(Vector(v), j)
    locktree.balance()
    lock_ids = masks.get("locked", np.empty(0, np.int32))
    lock_errors = [locktree.find(Vector(p))[2] for p in src[lock_ids]]
    report = {"height_error_m": percentile(err), "height_sample_count": len(samples),
              "height_vertical_ray_misses_nearest_fallback": missed,
              "first_height_errors_over_2m": locations,
              "boundary_vertex_distance_m": percentile(boundary_dist),
              "boundary_vertices_source": len(masks["boundary"]), "boundary_vertices_candidate": len(boundary),
              "protected_vertex_distance_m": percentile(lock_errors), "protected_vertices": len(lock_ids)}
    log({"quality": obj.name, **report})
    del bvh, tree, locktree
    gc.collect()
    return report


def simplify(ref, masks, target, lod, index):
    obj = ref.copy()
    obj.data = ref.data.copy()
    obj.name = ("Terrain" if index == 0 else "Water") + "_LOD" + str(lod)
    obj.hide_render = False
    bpy.context.collection.objects.link(obj)
    activate(obj)
    # LOD0 retains every original open boundary and every wet/dry crossing
    # triangle. Subsequent distant levels retain open boundaries and extrema.
    locked = np.unique(np.concatenate((masks["boundary"], masks["extrema"], masks["shore"] if lod == 0 else np.empty(0, np.int32))))
    name = set_protection(obj, locked)
    modifier = obj.modifiers.new("Quadric collapse preserving boundaries", "DECIMATE")
    modifier.decimate_type = "COLLAPSE"
    modifier.ratio = target / len(obj.data.polygons)
    modifier.use_collapse_triangulate = True
    modifier.vertex_group = name
    modifier.vertex_group_factor = 1.0
    log({"stage": "decimate", "object": obj.name, "target": target, "ratio": modifier.ratio, "locked": len(locked)})
    started = time.time()
    bpy.ops.object.modifier_apply(modifier=modifier.name)
    log({"stage": "decimate_complete", "object": obj.name, "triangles": len(obj.data.polygons), "seconds": time.time() - started})
    clamping = clamp_to_reference_bounds(obj, ref)
    # Re-sample the original authored corner normals: terrain slope shading in
    # the Unreal material must not become the simplified geometric face slope.
    transfer = obj.modifiers.new("Retain authored terrain normals", "DATA_TRANSFER")
    transfer.object = ref
    transfer.use_loop_data = True
    transfer.data_types_loops = {"CUSTOM_NORMAL"}
    transfer.loop_mapping = "POLYINTERP_NEAREST"
    bpy.ops.object.modifier_apply(modifier=transfer.name)
    q = quality(ref, obj, dict(masks, locked=locked), index)
    q.update(summary(obj))
    q.update(clamping)
    q["target_triangles"] = target
    q["decimation_seconds"] = time.time() - started
    return obj, q


def roundtrip(obj, path):
    before = coords(obj.data)
    before_normal = np.empty(len(obj.data.corner_normals) * 3, np.float32)
    obj.data.corner_normals.foreach_get("vector", before_normal)
    activate(obj)
    bpy.ops.export_scene.fbx(filepath=str(path), use_selection=True, object_types={"MESH"},
                             use_mesh_modifiers=True, mesh_smooth_type="FACE", use_tspace=False,
                             add_leaf_bones=False, bake_anim=False, path_mode="AUTO",
                             axis_forward="-Y", axis_up="Z", global_scale=1.0,
                             apply_unit_scale=True, apply_scale_options="FBX_SCALE_NONE",
                             bake_space_transform=False)
    bpy.ops.object.select_all(action="DESELECT")
    bpy.ops.import_scene.fbx(filepath=str(path), use_custom_normals=True,
                             use_image_search=False, use_anim=False)
    imported = [o for o in bpy.context.selected_objects if o.type == "MESH"]
    assert len(imported) == 1
    again = imported[0]
    activate(again)
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    after = coords(again.data)
    # FBX exporter preserves topology vertex order. Check every vertex, so a
    # reflection invisible in a symmetric bounding box cannot pass the test.
    assert before.shape == after.shape
    delta = np.linalg.norm(before - after, axis=1)
    normals = np.empty(len(again.data.corner_normals) * 3, np.float32)
    again.data.corner_normals.foreach_get("vector", normals)
    normal_error = np.abs(before_normal - normals)
    normal_a = before_normal.reshape((-1, 3)).astype(np.float64)
    normal_b = normals.reshape((-1, 3)).astype(np.float64)
    normal_a /= np.maximum(np.linalg.norm(normal_a, axis=1), 1e-12)[:, None]
    normal_b /= np.maximum(np.linalg.norm(normal_b, axis=1), 1e-12)[:, None]
    angles = np.degrees(np.arccos(np.clip(np.sum(normal_a * normal_b, axis=1), -1.0, 1.0)))
    report = {"all_vertex_roundtrip_distance_m": percentile(delta),
              "custom_normal_component_max_error": float(normal_error.max()),
              "custom_normal_angle_error_degrees": percentile(angles),
              "roundtrip": summary(again), "fbx": str(path), "fbx_bytes": path.stat().st_size,
              "asymmetric_landmarks": before[[int(np.argmax(before[:, 2])), len(before)//7, len(before)//3]].tolist()}
    before_uv = np.empty(len(obj.data.loops) * 2, np.float32)
    after_uv = np.empty(len(again.data.loops) * 2, np.float32)
    obj.data.uv_layers[0].data.foreach_get("uv", before_uv)
    again.data.uv_layers[0].data.foreach_get("uv", after_uv)
    report["uv_roundtrip_max_component_error"] = float(np.abs(before_uv - after_uv).max())
    assert float(delta.max()) < .005, report
    # Blender encodes custom normals in a quantized corner space. Check actual
    # directional error instead of an arbitrary component-coordinate threshold.
    assert float(angles.max()) < .2 and float(np.percentile(angles, 99)) < .1, report
    mesh = again.data
    imported_materials = list(mesh.materials)
    bpy.data.objects.remove(again, do_unlink=True)
    bpy.data.meshes.remove(mesh)
    for material in imported_materials:
        if material and material.users == 0:
            bpy.data.materials.remove(material)
    return report


def build():
    ref = bpy.data.objects.get("REFERENCE_SM_SandboxMap")
    if ref is None:
        bpy.ops.wm.open_mainfile(filepath=str(ROOT / "sandbox_source.blend"))
        ref = bpy.data.objects["REFERENCE_SM_SandboxMap"]
    parts = split_reference(ref)
    mask_files = [ROOT / "terrain_protection.npz", ROOT / "water_protection.npz"]
    masks = [dict(np.load(path)) for path in mask_files] if all(path.exists() for path in mask_files) else prepare_masks(parts)
    targets = [(360000, 140000), (110000, 40000), (38000, 12000)]
    report = json.loads((ROOT / "quality_report.json").read_text(encoding="utf-8")) if (ROOT / "quality_report.json").exists() else {"source": str(SOURCE), "blender": bpy.app.version_string, "lods": {},
              "coordinate_unit": "meter in Blender; centimeter FBX scale metadata",
              "material_constraints": "Authored normals and UV0 retained. Water pixel normal remains independent of geometry; coarser WPO interpolation must be reviewed at runtime."}
    # A quadric optimum can overshoot a retained summit by a few millimeters.
    # Keep the imported mesh's exact authored extent, including existing LODs.
    for key, record in report["lods"].items():
        existing = bpy.data.objects.get("SM_SandboxMap_LOD" + key)
        if existing:
            clamping = clamp_to_reference_bounds(existing, ref)
            if clamping["vertices_clamped_to_original_bounds"]:
                record["bounds_correction"] = clamping
                record["merged"] = summary(existing)
                record.update(roundtrip(existing, ROOT / ("lod" + key + ".fbx")))
                write("quality_report.json", report)
                bpy.ops.wm.save_as_mainfile(filepath=str(ROOT / "sandbox_lods.blend"), check_existing=False)
                log({"stage": "existing_lod_bounds_fixed", "lod": key, **clamping})
    # Resume an exported LOD after an overly strict legacy normal-component
    # check, without repeating the expensive collapse or losing its report.
    if "1" not in report["lods"] and (ROOT / "lod1.fbx").exists() and not bpy.data.objects.get("SM_SandboxMap_LOD1"):
        bpy.ops.object.select_all(action="DESELECT")
        bpy.ops.import_scene.fbx(filepath=str(ROOT / "lod1.fbx"), use_custom_normals=True,
                                 use_image_search=False, use_anim=False)
        recovered = next(o for o in bpy.context.selected_objects if o.type == "MESH")
        recovered.name = "SM_SandboxMap_LOD1"
        activate(recovered)
        bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
        for i, mat in enumerate(ref.data.materials):
            recovered.data.materials[i] = mat
        recovered.hide_render = True
        recovered.hide_set(True)
        previous_quality = []
        for line in (ROOT.parent / "blender_build_lod12.log").read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("SANDBOX_LOD {'quality':"):
                q = ast.literal_eval(line[len("SANDBOX_LOD "):])
                if q["quality"].endswith("_LOD1"):
                    q["name"] = q.pop("quality")
                    previous_quality.append(q)
        report["lods"]["1"] = {"parts": previous_quality, "merged": summary(recovered),
                                 "recovered_from_export_after_strict_component_check": True}
        write("quality_report.json", report)
        bpy.ops.wm.save_as_mainfile(filepath=str(ROOT / "sandbox_lods.blend"), check_existing=False)
        log("LOD1 recovered from completed FBX; continuing without re-running its collapse")
    for lod in (int(v) for v in ARGS.lods.split(",")):
        objects, qs = [], []
        for index, (part, mask, target) in enumerate(zip(parts, masks, targets[lod])):
            obj, q = simplify(part, mask, target, lod, index)
            objects.append(obj)
            qs.append(q)
        bpy.ops.object.select_all(action="DESELECT")
        for obj in objects:
            obj.select_set(True)
        bpy.context.view_layer.objects.active = objects[0]
        bpy.ops.object.join()
        merged = objects[0]
        merged.name = "SM_SandboxMap_LOD" + str(lod)
        record = {"parts": qs, "merged": summary(merged)}
        record.update(roundtrip(merged, ROOT / ("lod" + str(lod) + ".fbx")))
        report["lods"][str(lod)] = record
        report["elapsed_seconds"] = time.time() - START
        write("quality_report.json", report)
        bpy.ops.wm.save_as_mainfile(filepath=str(ROOT / "sandbox_lods.blend"), check_existing=False)
        merged.hide_set(lod != 0)
        merged.hide_render = lod != 0
        log({"stage": "lod_complete", "lod": lod, "triangles": record["merged"]["triangles"], "seconds": time.time() - START})
    ref.hide_set(True)
    for part in parts:
        part.hide_set(True)
    bpy.ops.wm.save_as_mainfile(filepath=str(ROOT / "sandbox_lods.blend"), check_existing=False)


def surface_sampling(ref, candidate, material_index):
    """Area-weighted source-surface samples avoid shoreline vertex-density bias."""
    src = coords(ref.data)
    sf = topology(ref.data)
    dst = coords(candidate.data)
    candidate.data.calc_loop_triangles()
    materials = np.empty(len(candidate.data.loop_triangles), np.int32)
    candidate.data.loop_triangles.foreach_get("material_index", materials)
    cf = topology(candidate.data)[materials == material_index]
    bvh = BVHTree.FromPolygons(dst.tolist(), cf.tolist(), all_triangles=True)
    area = np.abs((src[sf[:, 1], 0] - src[sf[:, 0], 0]) * (src[sf[:, 2], 1] - src[sf[:, 0], 1]) -
                  (src[sf[:, 1], 1] - src[sf[:, 0], 1]) * (src[sf[:, 2], 0] - src[sf[:, 0], 0])).astype(np.float64)
    rng = np.random.default_rng(19102026 + material_index)
    ids = rng.choice(len(sf), size=50000, p=area / area.sum())
    u = np.sqrt(rng.random(len(ids)))[:, None]
    v = rng.random(len(ids))[:, None]
    xyz = src[sf[ids, 0]] * (1 - u) + src[sf[ids, 1]] * u * (1 - v) + src[sf[ids, 2]] * u * v
    top = float(src[:, 2].max() + 100)
    errors, misses = [], 0
    for p in xyz:
        hit, _, _, _ = bvh.ray_cast((float(p[0]), float(p[1]), top), (0, 0, -1), 5000)
        if hit is None:
            misses += 1
        else:
            errors.append(abs(float(hit.z) - p[2]))
    # Track UV orientation and finite ranges; UV0 remains authored/interpolated.
    loops = np.empty(len(candidate.data.loops), np.int32)
    candidate.data.loops.foreach_get("vertex_index", loops)
    uv = np.empty(len(loops) * 2, np.float32)
    candidate.data.uv_layers[0].data.foreach_get("uv", uv)
    uv = uv.reshape((-1, 3, 2))[materials == material_index]
    signed = (uv[:, 1, 0] - uv[:, 0, 0]) * (uv[:, 2, 1] - uv[:, 0, 1]) - (uv[:, 1, 1] - uv[:, 0, 1]) * (uv[:, 2, 0] - uv[:, 0, 0])
    lengths = np.concatenate((np.linalg.norm(dst[cf[:, 0]] - dst[cf[:, 1]], axis=1),
                              np.linalg.norm(dst[cf[:, 1]] - dst[cf[:, 2]], axis=1),
                              np.linalg.norm(dst[cf[:, 2]] - dst[cf[:, 0]], axis=1)))
    result = {"area_weighted_samples": len(xyz), "vertical_ray_misses": misses,
              "area_weighted_height_error_m": percentile(errors), "triangle_edge_length_m": percentile(lengths),
              "uv_finite": bool(np.isfinite(uv).all()), "uv_min": uv.min(axis=(0, 1)).tolist(),
              "uv_max": uv.max(axis=(0, 1)).tolist(), "uv_triangle_positive_count": int((signed > 1e-12).sum()),
              "uv_triangle_negative_count": int((signed < -1e-12).sum()), "uv_triangle_degenerate_count": int((np.abs(signed) <= 1e-12).sum())}
    if material_index == 0:
        shore = np.load(ROOT / "terrain_protection.npz")["shore"]
        levels = np.load("S:/UE_WorkSpace/SC_Scene/Design/SandboxMapConcept/TerrainNaturalV5/Water/water_surface_native_m.npy", mmap_mode="r")
        shoreline_errors, changed, tested = [], 0, 0
        for p in src[shore]:
            hit, _, _, _ = bvh.ray_cast((float(p[0]), float(p[1]), top), (0, 0, -1), 5000)
            if hit is None:
                continue
            r = int(np.clip(round((7000.0 - float(p[1])) * (levels.shape[0] - 1) / 14000.0), 0, levels.shape[0] - 1))
            c = int(np.clip(round((float(p[0]) + 12000.0) * (levels.shape[1] - 1) / 24000.0), 0, levels.shape[1] - 1))
            level = float(levels[r, c])
            dz = float(hit.z) - float(p[2])
            shoreline_errors.append(abs(dz))
            # Ignore floating point equality at an unchanged waterline vertex.
            if abs(dz) > .001 and ((hit.z < level) != (float(p[2]) < level)):
                changed += 1
            tested += 1
        result["shoreline_vertex_height_error_m"] = percentile(shoreline_errors)
        result["shoreline_wet_dry_changed_samples"] = changed
        result["shoreline_wet_dry_tested_samples"] = tested
    return result


def finalize():
    report = json.loads((ROOT / "quality_report.json").read_text(encoding="utf-8"))
    parts = split_reference(bpy.data.objects["REFERENCE_SM_SandboxMap"])
    for lod in (0, 1, 2):
        obj = bpy.data.objects["SM_SandboxMap_LOD" + str(lod)]
        record = report["lods"][str(lod)]
        record.update(roundtrip(obj, ROOT / ("lod" + str(lod) + ".fbx")))
        record["area_weighted_validation"] = [surface_sampling(ref, obj, index) for index, ref in enumerate(parts)]
        record["export_smoothing_groups"] = "FACE"
        record["sha256"] = hashlib.sha256((ROOT / ("lod" + str(lod) + ".fbx")).read_bytes()).hexdigest()
        record["merged"] = summary(obj)
        write("quality_report.json", report)
        log({"stage": "finalized", "lod": lod, "validation": record["area_weighted_validation"]})
    for obj in bpy.context.scene.objects:
        obj.hide_set(obj.name != "SM_SandboxMap_LOD0")
        obj.hide_render = obj.name != "SM_SandboxMap_LOD0"
    activate(bpy.data.objects["SM_SandboxMap_LOD0"])
    # The combined reference already contains both source sections. Remove the
    # temporary split copies to avoid doubling reference geometry in the editor.
    for part in parts:
        mesh = part.data
        bpy.data.objects.remove(part, do_unlink=True)
        if mesh.users == 0:
            bpy.data.meshes.remove(mesh)
    bpy.context.scene.unit_settings.system = "METRIC"
    bpy.context.scene.unit_settings.length_unit = "METERS"
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type == "VIEW_3D":
                space = area.spaces.active
                space.clip_end = 1000000.0
                space.region_3d.view_distance = 26000.0
                space.region_3d.view_location = Vector((0.0, 0.0, 725.0))
                space.region_3d.view_rotation = Euler((math.radians(40), 0.0, math.radians(-25)), "XYZ").to_quaternion()
    report["editable_blend_objects"] = [{"name": obj.name, "viewport_hidden": obj.hide_get(),
                                         "render_hidden": obj.hide_render} for obj in bpy.context.scene.objects]
    report["source_sha256"] = hashlib.sha256(SOURCE.read_bytes()).hexdigest()
    write("quality_report.json", report)
    bpy.ops.wm.save_as_mainfile(filepath=str(ROOT / "sandbox_lods.blend"), check_existing=False)


if __name__ == "__main__":
    if ARGS.stage == "inspect":
        import_source()
    elif ARGS.stage == "build":
        try:
            build()
        except Exception:
            write("failure.json", {"traceback": traceback.format_exc(), "elapsed_seconds": time.time() - START})
            raise
    elif ARGS.stage == "finalize":
        finalize()
