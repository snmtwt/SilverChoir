"""Read-only asset and CVar diagnostics; no saves, rebuilds, or setting changes.

Execute in Unreal Editor Python. This script only writes its JSON report under
Saved/BaseSandboxSetup. GPU BLAS sizes need D3D12.DumpRayTracingGeometries in a
rendering process; asset triangle counts cannot establish their memory cost.
"""
import json
import traceback
from pathlib import Path

import unreal


MESH_PATH = "/Game/Meshs/Map/SM_SandboxMap"
OUTPUT = Path(unreal.Paths.project_saved_dir()) / "BaseSandboxSetup" / "raytracing_asset.json"
REPORT = {"ok": False, "asset": MESH_PATH, "asset_or_settings_mutated": False}


def _value(value):
    if value is None or isinstance(value, (bool, int, float, str)):
        return value
    if isinstance(value, unreal.Object):
        return value.get_path_name()
    return str(value)


def _properties(obj, names):
    result = {}
    for name in names:
        try:
            result[name] = _value(obj.get_editor_property(name))
        except Exception as exc:
            result[name] = {"unavailable": str(exc)}
    return result


def _struct_fields(obj, preferred):
    # dir() returns names exposed by the official Unreal Python wrappers.
    # Probe only value attributes with get_editor_property, never invoke methods.
    names = sorted(set(preferred) | {name for name in dir(obj) if not name.startswith("_")})
    result = {}
    for name in names:
        try:
            result[name] = _value(obj.get_editor_property(name))
        except Exception:
            pass
    return {"fields": result, "text": str(obj), "api_names": names}


try:
    mesh = unreal.load_asset(MESH_PATH)
    if not isinstance(mesh, unreal.StaticMesh):
        raise RuntimeError("Missing StaticMesh: " + MESH_PATH)
    # StaticMeshEditor is not initialized in some Python commandlet contexts.
    # Asset reflection remains available and is sufficient for core diagnostics.
    editor = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    REPORT["static_mesh_editor_subsystem_available"] = editor is not None
    REPORT["properties"] = _properties(mesh, [
        "support_ray_tracing", "allow_cpu_access", "lod_group",
        "min_lod", "num_streamed_lods", "never_stream",
    ])
    count = int(mesh.get_num_lods())
    REPORT["render_lod_count"] = count
    lods = []
    for index in range(count):
        lod = {"index": index, "triangles": int(mesh.get_num_triangles(index)),
               "vertices": int(mesh.get_num_vertices(index)),
               "sections": int(mesh.get_num_sections(index))}
        if editor is not None:
            try:
                lod["reduction_settings"] = str(editor.get_lod_reduction_settings(mesh, index))
            except Exception as exc:
                lod["reduction_settings_unavailable"] = str(exc)
        lods.append(lod)
    REPORT["render_lods"] = lods
    REPORT["bounds"] = str(mesh.get_bounds())
    REPORT["material_slots"] = [
        {"index": index,
         "slot": str(slot.get_editor_property("material_slot_name")),
         "material": _value(slot.get_editor_property("material_interface"))}
        for index, slot in enumerate(mesh.get_editor_property("static_materials"))
    ]
    try:
        nanite = mesh.get_editor_property("nanite_settings")
        REPORT["nanite_settings"] = _struct_fields(nanite, [
            "enabled", "fallback_target", "fallback_percent_triangles",
            "fallback_relative_error", "keep_percent_triangles", "trim_relative_error",
        ])
    except Exception as exc:
        REPORT["nanite_settings"] = {"unavailable": str(exc)}
    for name in ("triangles", "vertices"):
        try:
            REPORT["nanite_render_" + name] = int(getattr(mesh, "get_num_nanite_" + name)())
        except Exception as exc:
            REPORT["nanite_render_" + name] = {"unavailable": str(exc)}
    try:
        proxy = mesh.get_editor_property("ray_tracing_proxy_settings")
        REPORT["ray_tracing_proxy_settings"] = _struct_fields(proxy, [
            "enabled", "fallback_target", "fallback_percent_triangles",
            "fallback_relative_error", "foliage_over_occlusion_bias",
        ])
    except Exception as exc:
        REPORT["ray_tracing_proxy_settings"] = {"unavailable": str(exc)}
    REPORT["console_variables"] = {}
    for name in [
        "r.RayTracing", "r.RayTracing.Enable", "r.Lumen.HardwareRayTracing",
        "r.RayTracing.UseReferenceBasedResidency",
        "r.RayTracing.ResidentGeometryMemoryPoolSizeInMB",
        "r.RayTracing.NumAlwaysResidentLODs",
        "r.RayTracing.Debug.GeometryMemoryPool.AlwaysResidentWarningPercentage",
        "r.RayTracing.RayTracingProxies.ProjectEnabled",
        "r.StaticMesh.RayTracingProxies", "r.StaticMesh.RayTracingProxies.LOD1MaxNumOfTriangles",
    ]:
        try:
            REPORT["console_variables"][name] = unreal.SystemLibrary.get_console_variable_int_value(name)
        except Exception as exc:
            REPORT["console_variables"][name] = {"unavailable": str(exc)}
    try:
        REPORT["console_variables"]["r.RayTracing.ApproximateCompactionRatio"] = (
            unreal.SystemLibrary.get_console_variable_float_value("r.RayTracing.ApproximateCompactionRatio"))
    except Exception as exc:
        REPORT["console_variables"]["r.RayTracing.ApproximateCompactionRatio"] = {"unavailable": str(exc)}
    REPORT["limits"] = [
        "Render LODs are not necessarily ray-tracing proxy LODs.",
        "This report contains no measured resident BLAS allocation sizes.",
        "In this UE 5.8 build, dedicated ray-tracing proxies require Nanite; non-Nanite meshes use render LODs.",
        "For Nanite meshes, project-enabled ray-tracing proxies can be built even when the per-asset enabled field is false.",
        "The warning describes the process-wide geometry manager, not one mesh or texture streaming.",
    ]
    REPORT["ok"] = True
except Exception:
    REPORT["error"] = traceback.format_exc()
    unreal.log_error(REPORT["error"])
finally:
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(json.dumps(REPORT, ensure_ascii=False, indent=2), encoding="utf-8")
    unreal.log("Sandbox ray-tracing asset diagnostics: " + str(OUTPUT))
