"""Capture comparable sandbox LOD views in the existing dedicated editor worker.

Optional request: Saved/BaseSandboxSetup/lod_review_request.json, for example:
  {"run_name":"before_reduction","lod_indices":[0,1,2],"include_auto":true}
  Add "mesh_path":"/Game/Meshs/Map/Optimization/SM_SandboxMap_Candidate" to
  review a candidate temporarily with the existing terrain material overrides.

Submit this script with the existing BaseSandboxSetup/job.json protocol. Worker
job_result.json only acknowledges scheduling: wait for this script's
LODReview/latest.json to report complete/failed and follow its report_path.
No asset/level saves, mesh edits, LOD rebuilds, or editor launches are performed.
Component mesh/materials/transform, forced LOD, dynamic water time, and viewport camera are restored.
"""
import csv
import datetime
import json
import re
import shutil
import statistics
import time
import traceback
from pathlib import Path

import unreal


ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
BASE = ROOT / 'Saved/BaseSandboxSetup'
REQUEST = BASE / 'lod_review_request.json'
OPTIONS = json.loads(REQUEST.read_text(encoding='utf-8-sig')) if REQUEST.exists() else {}
STAMP = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
RUN_NAME = str(OPTIONS.get('run_name', 'sandbox_lods'))
assert re.fullmatch(r'[A-Za-z0-9_-]{1,64}', RUN_NAME), 'run_name must be a short filename-safe label'
OUT = BASE / 'LODReview' / (RUN_NAME + '_' + STAMP)
OUT.mkdir(parents=True, exist_ok=False)
LATEST = BASE / 'LODReview/latest.json'
REPORT_PATH = OUT / 'report.json'
AA = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
STATE = {'handle': None, 'busy': False, 'finished': False, 'capture': None,
         'terrain': None, 'old_forced_lod': None, 'water': None,
         'old_mesh': None, 'old_material_overrides': None, 'old_relative_transform': None,
         'old_wave_time': None, 'old_viewport': None, 'index': 0,
         'phase': 'warming', 'frames': 0, 'started': 0., 'frame_deltas': []}
REPORT = {'ok': False, 'status': 'initializing', 'run_name': RUN_NAME,
          'report_path': str(REPORT_PATH), 'output_directory': str(OUT),
          'asset_or_level_saved': False, 'captures': [], 'options': OPTIONS,
          'limits': [
              'ForcedLodModel 1/2/3 selects render LOD0/1/2; zero selects automatic LOD.',
              'Automatic effective render LOD is not exposed by this script; it records requested mode only.',
              'RT CSV sizes are engine-tracked BLAS allocations, not guaranteed currently resident GPU bytes.',
              'The process may keep other LOD BLAS allocations after switching; compare named CSV rows, not only totals.',
              'Slate tick timing includes editor and repeated SceneCapture work; it is not a runtime FPS benchmark.',
              'LOD review captures the loaded editor scene. The existing SandboxScene regression checks the real NewGame flow.',
          ]}


def write():
    REPORT_PATH.write_text(json.dumps(REPORT, ensure_ascii=False, indent=2), encoding='utf-8')
    LATEST.write_text(json.dumps({'status': REPORT['status'], 'ok': REPORT['ok'],
                                 'report_path': str(REPORT_PATH),
                                 'completed_captures': len(REPORT['captures']),
                                 'total_captures': len(CASES) if 'CASES' in globals() else None},
                                indent=2), encoding='utf-8')


def restore_material_overrides():
    # Editor property notifications rerun the owner's construction scripts,
    # which rebuild GSM terrain, MIDs, transforms, and coordinate labels.
    # SetStaticMesh preserves the override array; native SetMaterial keeps it
    # intact without invoking editor property notifications.
    if STATE['terrain'] is not None and STATE['old_material_overrides'] is not None:
        for index, material in enumerate(STATE['old_material_overrides']):
            STATE['terrain'].set_material(index, material)


def finish(error=None):
    if STATE['finished']:
        return
    STATE['finished'] = True
    errors = [error] if error else []
    try:
        if STATE['handle'] is not None:
            unreal.unregister_slate_post_tick_callback(STATE['handle'])
            STATE['handle'] = None
    except Exception:
        errors.append(traceback.format_exc())
    for operation in (
        lambda: STATE['terrain'].set_static_mesh(STATE['old_mesh'])
            if STATE['terrain'] is not None and STATE['old_mesh'] is not None else None,
        restore_material_overrides,
        lambda: STATE['terrain'].set_relative_transform(STATE['old_relative_transform'], False, False)
            if STATE['terrain'] is not None and STATE['old_relative_transform'] is not None else None,
        lambda: STATE['terrain'].set_forced_lod_model(STATE['old_forced_lod'])
            if STATE['terrain'] is not None and STATE['old_forced_lod'] is not None else None,
        lambda: STATE['water'].set_scalar_parameter_value('WaveTimeOverride', STATE['old_wave_time'])
            if STATE['water'] is not None and STATE['old_wave_time'] is not None else None,
        lambda: unreal.EditorLevelLibrary.set_level_viewport_camera_info(*STATE['old_viewport'])
            if STATE['old_viewport'] is not None else None,
        lambda: AA.destroy_actor(STATE['capture']) if STATE['capture'] is not None else None,
    ):
        try:
            operation()
        except Exception:
            errors.append(traceback.format_exc())
    REPORT.update(ok=not errors, status='failed' if errors else 'complete',
                  restored_in_memory=not errors, error='\n'.join(errors) if errors else None)
    write()
    if errors:
        unreal.log_error(REPORT['error'])
    else:
        unreal.log('BASE_SANDBOX_LOD_REVIEW_OK ' + str(REPORT_PATH))


def csv_state():
    folder = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.profiling_dir()))
    return {str(p): (p.stat().st_mtime_ns, p.stat().st_size)
            for p in folder.glob('d3d12DumpRayTracingGeometries-*.csv')}


def record_rt_csv(case_report):
    current = csv_state()
    changed = [Path(p) for p, metadata in current.items() if STATE['csv_before'].get(p) != metadata]
    if not changed:
        case_report['raytracing'] = {'available': False,
            'reason': 'No new D3D12 RT CSV. Check rendering RHI, ray tracing support, and worker log.'}
        return
    latest = max(changed, key=lambda p: p.stat().st_mtime_ns)
    destination = OUT / (case_report['name'] + '_rt_geometry.csv')
    shutil.copy2(latest, destination)
    with destination.open(encoding='utf-8-sig', newline='') as stream:
        rows = list(csv.DictReader(stream))
    matching = [row for row in rows if 'sandbox' in str(row.get('Name', '')).lower()]
    case_report['raytracing'] = {
        'available': True, 'csv': str(destination), 'source_csv': str(latest),
        'sandbox_rows': matching,
        'sandbox_tracked_blas_mb': sum(float(row['Size (MBs)']) for row in matching),
        'all_tracked_blas_mb': sum(float(row['Size (MBs)']) for row in rows),
        'all_geometry_rows': len(rows),
    }


def start_case():
    case = CASES[STATE['index']]
    TERRAIN.set_forced_lod_model(case['forced_lod_model'])
    location, rotation, fov = VIEWS[case['view']]
    CAP.set_actor_location(location, False, False)
    CAP.set_actor_rotation(rotation, False)
    COMP.set_editor_property('fov_angle', fov)
    # Keep the main editor view on the same model, so RT residency diagnostics
    # are not dominated by an unrelated off-screen editor camera.
    unreal.EditorLevelLibrary.set_level_viewport_camera_info(location, rotation)
    STATE.update(phase='warming', frames=0, started=time.monotonic(), frame_deltas=[])
    REPORT['status'] = 'rendering_' + case['name']
    write()


def tick(delta):
    if STATE['busy'] or STATE['finished']:
        return
    STATE['busy'] = True
    try:
        case = CASES[STATE['index']]
        STATE['frames'] += 1
        COMP.capture_scene()
        if STATE['frames'] > 32:
            STATE['frame_deltas'].append(float(delta) * 1000.)
        if STATE['phase'] == 'warming':
            if STATE['frames'] < WARMUP_FRAMES or time.monotonic() - STATE['started'] < WARMUP_SECONDS:
                return
            STATE['csv_before'] = csv_state()
            unreal.log('SANDBOX_LOD_RT_BEGIN ' + case['name'])
            if DUMP_RT:
                unreal.SystemLibrary.execute_console_command(WORLD, 'D3D12.DumpRayTracingGeometries all Sandbox')
                unreal.SystemLibrary.execute_console_command(WORLD, 'D3D12.DumpRayTracingGeometriesToCSV')
            STATE.update(phase='exporting', export_after_frame=STATE['frames'] + 4)
            return
        if STATE['frames'] < STATE['export_after_frame']:
            return
        filename = case['name'] + '.png'
        unreal.RenderingLibrary.export_render_target(WORLD, RT, str(OUT), filename)
        assert (OUT / filename).stat().st_size > 4096, 'Screenshot was not written: ' + filename
        samples = sorted(STATE['frame_deltas'])
        result = dict(case, image=str(OUT / filename), warmup_frames=STATE['frames'],
                      elapsed_seconds=time.monotonic() - STATE['started'],
                      component_forced_lod_model=int(TERRAIN.get_editor_property('forced_lod_model')),
                      camera_location=str(CAP.get_actor_location()), camera_rotation=str(CAP.get_actor_rotation()),
                      fov_degrees=float(COMP.get_editor_property('fov_angle')))
        if samples:
            result['editor_capture_tick_ms'] = {'samples': len(samples), 'median': statistics.median(samples),
                'p95': samples[min(len(samples)-1, int(.95 * len(samples)))], 'max': max(samples)}
        if DUMP_RT:
            record_rt_csv(result)
        REPORT['captures'].append(result)
        STATE['index'] += 1
        if STATE['index'] == len(CASES):
            finish()
        else:
            start_case()
    except Exception:
        finish(traceback.format_exc())
    finally:
        STATE['busy'] = False


try:
    WORLD = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    boards = [a for a in AA.get_all_level_actors() if isinstance(a, unreal.BaseSandboxMap)]
    assert len(boards) == 1, 'Open the saved L_BaseTemplate in the dedicated worker first'
    BOARD = boards[0]
    TERRAIN = BOARD.get_map_terrain_mesh_component()
    original_mesh = TERRAIN.get_editor_property('static_mesh')
    assert original_mesh and original_mesh.get_path_name() == '/Game/Meshs/Map/SM_SandboxMap.SM_SandboxMap'
    mesh_path = str(OPTIONS.get('mesh_path', '/Game/Meshs/Map/SM_SandboxMap'))
    MESH = unreal.load_asset(mesh_path)
    assert isinstance(MESH, unreal.StaticMesh), 'mesh_path must reference a StaticMesh: ' + mesh_path
    CAMERA = next(a for a in AA.get_all_level_actors() if a.get_actor_label() == 'CAM_BaseSandbox_Overview')
    STATE.update(terrain=TERRAIN, old_forced_lod=int(TERRAIN.get_editor_property('forced_lod_model')),
                 old_mesh=original_mesh, old_material_overrides=list(TERRAIN.get_editor_property('override_materials')),
                 old_relative_transform=TERRAIN.get_relative_transform(),
                 old_viewport=unreal.EditorLevelLibrary.get_level_viewport_camera_info())
    if MESH != original_mesh:
        TERRAIN.set_static_mesh(MESH)
        restore_material_overrides()
        TERRAIN.set_relative_transform(STATE['old_relative_transform'], False, False)
        assert TERRAIN.get_editor_property('static_mesh') == MESH, 'Could not set temporary candidate mesh'
        assert list(TERRAIN.get_editor_property('override_materials')) == STATE['old_material_overrides'], 'Terrain material overrides changed'
    lod_count = int(MESH.get_num_lods())
    requested = sorted(set(int(v) for v in OPTIONS.get('lod_indices', [0, 1, 2])))
    assert all(v >= 0 for v in requested)
    available = [v for v in requested if v < lod_count]
    REPORT.update(mesh=MESH.get_path_name(), original_component_mesh=original_mesh.get_path_name(),
                  temporary_candidate_mesh=MESH != original_mesh, actor=BOARD.get_path_name(), lod_count=lod_count,
                  terrain_relative_transform=str(TERRAIN.get_relative_transform()),
                  unavailable_requested_lods=[v for v in requested if v >= lod_count],
                  map_scale=BOARD.get_current_map_scale(), actor_transform=str(BOARD.get_actor_transform()),
                  lods=[{'index': i, 'triangles': int(MESH.get_num_triangles(i)),
                         'vertices': int(MESH.get_num_vertices(i)), 'sections': int(MESH.get_num_sections(i))}
                        for i in range(lod_count)])
    water = TERRAIN.get_material(1)
    if isinstance(water, unreal.MaterialInstanceDynamic):
        STATE.update(water=water, old_wave_time=float(water.get_scalar_parameter_value('WaveTimeOverride')))
        water.set_scalar_parameter_value('WaveTimeOverride', 0.)
        REPORT['water_time_frozen_on_transient_mid'] = True
    else:
        REPORT['water_time_frozen_on_transient_mid'] = False
    overview_location, overview_rotation = CAMERA.get_actor_location(), CAMERA.get_actor_rotation()
    target = BOARD.get_actor_location() + unreal.Vector(0, 0, 10)
    near_factor = float(OPTIONS.get('closeup_distance_scale', .45))
    assert .1 <= near_factor <= .9
    near_location = target + (overview_location - target) * near_factor
    near_rotation = unreal.MathLibrary.find_look_at_rotation(near_location, target)
    fov = float(CAMERA.camera_component.get_editor_property('field_of_view'))
    VIEWS = {'overview': (overview_location, overview_rotation, fov),
             'closeup': (near_location, near_rotation, fov)}
    modes = [('lod' + str(i), i + 1, i) for i in available]
    if bool(OPTIONS.get('include_auto', True)):
        modes.append(('auto', 0, None))
    CASES = [{'name': view + '_' + name, 'view': view, 'forced_lod_model': force,
              'requested_lod_index': index} for view in VIEWS for name, force, index in modes]
    assert CASES, 'No available requested LODs or automatic cases'
    WARMUP_SECONDS = max(2., float(OPTIONS.get('warmup_seconds', 12.)))
    WARMUP_FRAMES = max(16, int(OPTIONS.get('warmup_frames', 64)))
    DUMP_RT = bool(OPTIONS.get('dump_raytracing', True))
    width, height = (int(v) for v in OPTIONS.get('resolution', [1600, 1100]))
    assert 320 <= width <= 3840 and 240 <= height <= 2160
    REPORT['resolution'] = [width, height]
    CAP = AA.spawn_actor_from_class(unreal.SceneCapture2D, overview_location, overview_rotation)
    assert CAP
    STATE['capture'] = CAP
    CAP.set_actor_label('Temporary_BaseSandbox_LOD_Review')
    COMP = CAP.get_component_by_class(unreal.SceneCaptureComponent2D)
    RT = unreal.RenderingLibrary.create_render_target2d(WORLD, width, height, unreal.TextureRenderTargetFormat.RTF_RGBA8)
    RT.set_editor_property('target_gamma', 2.2)
    settings = COMP.get_editor_property('post_process_settings')
    for key, value in {'override_auto_exposure_method': True, 'auto_exposure_method': unreal.AutoExposureMethod.AEM_MANUAL,
            'override_auto_exposure_apply_physical_camera_exposure': True, 'auto_exposure_apply_physical_camera_exposure': False,
            'override_auto_exposure_bias': True, 'auto_exposure_bias': 0.,
            'override_motion_blur_amount': True, 'motion_blur_amount': 0.,
            'override_depth_of_field_enabled': True, 'depth_of_field_enabled': False}.items():
        settings.set_editor_property(key, value)
    for key, value in {'texture_target': RT, 'capture_source': unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR,
            'capture_every_frame': False, 'capture_on_movement': False, 'always_persist_rendering_state': True,
            'projection_type': unreal.CameraProjectionMode.PERSPECTIVE,
            'post_process_settings': settings, 'post_process_blend_weight': 1.}.items():
        COMP.set_editor_property(key, value)
    start_case()
    STATE['handle'] = unreal.register_slate_post_tick_callback(tick)
except Exception:
    finish(traceback.format_exc())
    raise
