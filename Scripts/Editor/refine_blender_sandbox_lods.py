"""Restore the original linear UV map and repair rare collapse foldovers.

Operates only on the reviewable Blender candidates; never opens Unreal assets.
Local vertex moves remain inside the positive-orientation kernel of their entire
one-ring, with source boundary/shore locks retained and source height resampled.
"""
from pathlib import Path
import json
import runpy
import shutil
import bpy
import numpy as np
from mathutils.bvhtree import BVHTree

UTIL = runpy.run_path("S:/UE_WorkSpace/SilverChoir/Scripts/Editor/blender_sandbox_lods.py")
ROOT = UTIL["ROOT"]
coords = UTIL["coords"]
topology = UTIL["topology"]
activate = UTIL["activate"]
backup = ROOT / "before_local_repair"
backup.mkdir(exist_ok=True)
for name in ("lod0.fbx", "lod1.fbx", "lod2.fbx", "sandbox_lods.blend", "quality_report.json"):
    if not (backup / name).exists():
        shutil.copy2(ROOT / name, backup / name)
ref = bpy.data.objects["REFERENCE_SM_SandboxMap"]
parts = UTIL["split_reference"](ref)
source = [coords(part.data) for part in parts]
source_bvh = [BVHTree.FromPolygons(p.tolist(), topology(part.data).tolist(), all_triangles=True)
              for part, p in zip(parts, source)]
masks = [dict(np.load(ROOT / name)) for name in ("terrain_protection.npz", "water_protection.npz")]


def signed_area(p, f):
    return ((p[f[:, 1], 0]-p[f[:, 0], 0])*(p[f[:, 2], 1]-p[f[:, 0], 1])-
            (p[f[:, 1], 1]-p[f[:, 0], 1])*(p[f[:, 2], 0]-p[f[:, 0], 0]))


def safe_kernel_point(p, f, vertex):
    incident = f[np.any(f == vertex, axis=1)]
    centered = p[:, :2].astype(np.float64) - p[vertex, :2]
    neighbours = np.unique(incident)
    span = float(np.linalg.norm(centered[neighbours], axis=1).max() * 2 + 1)
    polygon = [np.array(v, np.float64) for v in ((-span, -span), (span, -span), (span, span), (-span, span))]
    constraints = []
    for tri in incident:
        k = int(np.flatnonzero(tri == vertex)[0])
        a, b = centered[tri[(k+1) % 3]], centered[tri[(k+2) % 3]]
        normal = np.array((a[1]-b[1], b[0]-a[0]))
        constant = float(a[0]*b[1]-a[1]*b[0])
        # At least a millimeter of signed altitude from every incident edge.
        threshold = max(.01, np.linalg.norm(normal) * .002)
        constraints.append((normal, constant, threshold))
        if not polygon:
            return None
        clipped = []
        for start, end in zip(polygon, polygon[1:]+polygon[:1]):
            fs = float(normal @ start + constant - threshold)
            fe = float(normal @ end + constant - threshold)
            if fs >= 0:
                clipped.append(start)
            if (fs >= 0) != (fe >= 0):
                clipped.append(start + (end-start) * fs / (fs-fe))
        polygon = clipped
    if not polygon:
        return None
    centroid = np.mean(polygon, axis=0)
    possibilities = [centroid]
    for a, b in zip(polygon, polygon[1:]+polygon[:1]):
        edge = b-a
        point = a + edge * np.clip(-float(a @ edge) / max(float(edge @ edge), 1e-16), 0, 1)
        possibilities.append(point * .99 + centroid * .01)
    point = min(possibilities, key=lambda v: float(v @ v))
    point += p[vertex, :2]
    # Check the rounded mesh coordinates too, not only double-precision clipping.
    test = p[incident].copy()
    test[np.any(incident[:, :, None] == vertex, axis=2), :2] = point.astype(np.float32)
    area = ((test[:, 1, 0]-test[:, 0, 0])*(test[:, 2, 1]-test[:, 0, 1])-
            (test[:, 1, 1]-test[:, 0, 1])*(test[:, 2, 0]-test[:, 0, 0]))
    return point if np.all(area > 0) else None


results = {}
for lod in (0, 1, 2):
    obj = bpy.data.objects["SM_SandboxMap_LOD" + str(lod)]
    p = coords(obj.data)
    f = topology(obj.data)
    mat = np.empty(len(obj.data.polygons), np.int32)
    obj.data.polygons.foreach_get("material_index", mat)
    locked_positions = set()
    for index in (0, 1):
        mask = masks[index]
        lock = np.unique(np.concatenate((mask["boundary"], mask["extrema"],
                          mask["shore"] if lod == 0 else np.empty(0, np.int32))))
        locked_positions.update(tuple(row) for row in source[index][lock])
    locked = np.array([tuple(row) in locked_positions for row in p])
    repairs = []
    initial = np.flatnonzero(signed_area(p, f) <= 0)
    for step in range(200):
        invalid = np.flatnonzero(signed_area(p, f) <= 0)
        if not len(invalid):
            break
        face_id = int(invalid[0])
        proposals = []
        for vertex in f[face_id]:
            if locked[vertex]:
                continue
            point = safe_kernel_point(p, f, int(vertex))
            if point is not None:
                proposals.append((float(np.linalg.norm(point - p[vertex, :2])), int(vertex), point))
        assert proposals, (lod, face_id, "no unlocked positive one-ring kernel")
        distance, vertex, point = min(proposals, key=lambda v: v[0])
        material = int(mat[face_id])
        hit, _, _, _ = source_bvh[material].ray_cast((float(point[0]), float(point[1]), 1800.0), (0, 0, -1), 5000)
        assert hit is not None, (lod, face_id, point)
        old = p[vertex].copy()
        p[vertex] = (point[0], point[1], hit.z)
        repairs.append({"vertex": vertex, "material": material, "before": old.tolist(),
                        "after": p[vertex].tolist(), "xy_distance_m": distance})
    assert not np.any(signed_area(p, f) <= 0)
    obj.data.vertices.foreach_set("co", p.ravel())
    obj.data.update()
    loops = np.empty(len(obj.data.loops), np.int32)
    obj.data.loops.foreach_get("vertex_index", loops)
    uv = np.column_stack(((p[loops, 0]+12000.0)/24000.0, (7000.0-p[loops, 1])/14000.0))
    obj.data.uv_layers[0].data.foreach_set("uv", uv.ravel())
    activate(obj)
    for index, part in enumerate(parts):
        group = obj.vertex_groups.new(name="NormalTransferMaterial" + str(index))
        group.add(np.unique(f[mat == index]).tolist(), 1.0, "REPLACE")
        group_name = group.name
        modifier = obj.modifiers.new("Source authored normals", "DATA_TRANSFER")
        modifier.object = part
        modifier.vertex_group = group.name
        modifier.use_loop_data = True
        modifier.data_types_loops = {"CUSTOM_NORMAL"}
        modifier.loop_mapping = "POLYINTERP_NEAREST"
        bpy.ops.object.modifier_apply(modifier=modifier.name)
        current_group = obj.vertex_groups.get(group_name)
        if current_group:
            obj.vertex_groups.remove(current_group)
    results[str(lod)] = {"negative_xy_faces_before": len(initial), "negative_xy_faces_after": 0,
                         "repaired_vertices": repairs, "uv_restored_from_original_linear_mapping": True}
    print("SANDBOX_LOCAL_REPAIR", lod, "repaired", len(repairs), flush=True)
(ROOT / "local_repair_report.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
report = json.loads((ROOT / "quality_report.json").read_text(encoding="utf-8"))
for key, repair in results.items():
    report["lods"][key]["local_repair"] = repair
(ROOT / "quality_report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
UTIL["finalize"]()
