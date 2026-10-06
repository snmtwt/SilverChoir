"""Create three owned vehicle template assets; never replace unrelated user assets.

Run with UnrealEditor-Cmd -run=pythonscript -script=<this file>.
Add -VehicleTemplatesVerifyOnly to inspect the saved assets without editing them.
"""
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import struct
import unreal


ROOT = Path(unreal.Paths.project_dir()).resolve()
FOLDER = "/Game/System/Data/Vehicles"
TABLE_PATH = FOLDER + "/DT_VehicleTemplates"
IMAGE_FOLDER = FOLDER + "/Images"
OWNER_KEY = "SilverChoir.VehicleTemplates.Owner"
OWNER = "VehicleTemplates_20260928_v1"
SOURCE_KEY = "SilverChoir.VehicleTemplates.SourceSHA256"
LIBRARY = unreal.EditorAssetLibrary
VERIFY_ONLY = "-VehicleTemplatesVerifyOnly" in unreal.SystemLibrary.get_command_line()
# Placeholder balance examples, not the game's final vehicle balance.
DEFINITIONS = (
    ("SportsCar_2Seat", "疾影跑车", "双座快速公路载具；测试模板，平衡数值待调整。", 2, 100., 60., 80., 6., 4.),
    ("Sedan_4Seat", "巡航轿车", "四座通用公路载具；测试模板，平衡数值待调整。", 4, 140., 70., 180., 4., 2.5),
    ("SUV_6Seat", "远征越野车", "六座远征载具；测试模板，平衡数值待调整。", 6, 180., 100., 350., 3., 4.),
)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def texture_settings():
    return {
        "lod_group": unreal.TextureGroup.TEXTUREGROUP_UI,
        "compression_settings": unreal.TextureCompressionSettings.TC_EDITOR_ICON,
        "mip_gen_settings": unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS,
        "power_of_two_mode": unreal.TexturePowerOfTwoSetting.NONE,
        "filter": unreal.TextureFilter.TF_BILINEAR,
        "never_stream": True,
        "srgb": True,
        "compression_no_alpha": False,
        "max_texture_size": 0,
    }


def source_info(name):
    source = ROOT / "SourceArt" / "Vehicles" / (name + ".png")
    require(source.is_file(), "Missing source image: " + str(source))
    raw = source.read_bytes()
    require(raw[:8] == b"\x89PNG\r\n\x1a\n" and raw[12:16] == b"IHDR", "Expected PNG: " + str(source))
    width, height, depth, color = struct.unpack(">IIBB", raw[16:26])
    require(width > 0 and height > 0, "Empty PNG dimensions")
    require(abs((width / height) / (16. / 9.) - 1.) <= 0.01,
            "Preview must be approximately 16:9: %s (%s x %s)" % (source, width, height))
    require(color in (4, 6), "Preview must retain an alpha channel: " + str(source))
    return {"source": str(source), "width": width, "height": height,
            "bit_depth": depth, "sha256": hashlib.sha256(raw).hexdigest()}


def owned_asset(path, asset_type):
    if not LIBRARY.does_asset_exist(path):
        return None
    asset = LIBRARY.load_asset(path)
    require(isinstance(asset, asset_type), "Unexpected existing asset type: " + path)
    require(LIBRARY.get_metadata_tag(asset, OWNER_KEY) == OWNER,
            "Refusing to overwrite an existing unowned asset: " + path)
    return asset


def validate_texture(texture, info):
    require(LIBRARY.get_metadata_tag(texture, SOURCE_KEY) == info["sha256"],
            "Owned texture source changed; no automatic overwrite: " + texture.get_path_name())
    for property_name, expected in texture_settings().items():
        actual = texture.get_editor_property(property_name)
        require(actual == expected, "%s.%s changed: %s != %s" %
                (texture.get_path_name(), property_name, actual, expected))


def make_rows():
    rows = []
    for key, name, description, seats, durability, fuel, cargo, speed, fuel_cost in DEFINITIONS:
        texture_name = "T_Vehicle_" + key
        rows.append({
            "Name": key,
            "Profile": {"VehicleName": name, "Description": description,
                        "PreviewImage": IMAGE_FOLDER + "/" + texture_name + "." + texture_name},
            "Attributes": {"MaxDurability": durability, "MaxFuel": fuel,
                           "PassengerCapacity": seats, "MaxCargoWeight": cargo},
            "EntityData": {"VehiclePawnClass": "None"},
            "StrategicMovementData": {"SpeedTilesPerHour": speed, "StaminaCostPerTile": 0.,
                                      "FuelCostPerTile": fuel_cost},
        })
    return rows


def object_path(value):
    # DataTable export may wrap the path in a Texture2D'/Game/...' literal.
    text = str(value)
    return text.split("'", 2)[1] if "'" in text else text


def text_source(value):
    # DataTable FText fields acquire stable localization keys on import.
    text = str(value)
    for macro in ("NSLOCTEXT", "LOCTEXT", "INVTEXT"):
        if text.startswith(macro + "(") and text.endswith(")"):
            return json.loads("[" + text[len(macro) + 1:-1] + "]")[-1]
    return text


def validate_table(table, expected_rows):
    require(table.get_editor_property("row_struct") == unreal.VehicleTemplate.static_struct(),
            "DT_VehicleTemplates row struct is not FVehicleTemplate")
    actual_rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    require(len(actual_rows) == 3, "Expected exactly three vehicle template rows")
    by_name = {row["Name"]: row for row in actual_rows}
    require(set(by_name) == {row["Name"] for row in expected_rows}, "Unexpected vehicle template row names")
    for expected in expected_rows:
        actual = by_name[expected["Name"]]
        for section in ("Profile", "Attributes", "EntityData", "StrategicMovementData"):
            for key, value in expected[section].items():
                found = actual[section][key]
                if key in ("PreviewImage", "VehiclePawnClass"):
                    found = object_path(found)
                elif key in ("VehicleName", "Description"):
                    found = text_source(found)
                require(found == value, "%s.%s.%s differs: %s != %s" %
                        (expected["Name"], section, key, found, value))
    return actual_rows


def verify_factory(table, textures):
    tile_id = unreal.Name("VehicleTemplate_Verification")
    function = unreal.VehicleDataLibrary.create_vehicle_data_from_table
    unreal.log("VEHICLE_FACTORY_SIGNATURE " + str(function.__doc__))
    # UE Python strips a bool success return when out parameters exist: None on
    # failure; (OutData, OutError) on success. It does not return a three-tuple.
    result = function(table, [unreal.Name(row[0]) for row in DEFINITIONS], tile_id)
    require(result is not None and len(result) == 2, "Vehicle table factory failed")
    vehicles, error = result
    require(not str(error), "Factory returned an error: " + str(error))
    require(len(vehicles) == 3, "Factory did not produce three vehicles")
    ids = []
    for data, definition in zip(vehicles, DEFINITIONS):
        key, name, description, seats, durability, fuel, cargo, speed, fuel_cost = definition
        require(unreal.GuidLibrary.is_valid_guid(data.vehicle_id), "Factory produced an invalid vehicle ID")
        ids.append(data.vehicle_id.to_string())
        require(str(data.source_template_row) == key, "Incorrect source template row")
        require(data.runtime_data.tile_id == tile_id, "Incorrect current tile ID")
        require(data.attributes.passenger_capacity == seats, "Incorrect seat count")
        require(data.runtime_data.current_durability == durability, "Durability is not full")
        require(data.runtime_data.current_fuel == fuel, "Fuel is not full")
        require(data.profile.preview_image == textures[key], "Preview texture reference was not copied")
        require(data.entity_data.vehicle_pawn_class is None, "Unexpected fabricated vehicle entity class")
        require(data.strategic_movement_data.speed_tiles_per_hour == speed, "Incorrect strategic speed")
        require(data.strategic_movement_data.fuel_cost_per_tile == fuel_cost, "Incorrect strategic fuel cost")
    require(len(set(ids)) == 3, "Factory did not create three unique IDs")
    return {"tile_id": str(tile_id), "unique_vehicle_ids": ids, "count": len(vehicles)}


def main():
    # Check every target and source before creating anything. Existing assets are
    # only reused after ownership/content checks, never reimported or rewritten.
    rows = make_rows()
    infos = {definition[0]: source_info("T_Vehicle_" + definition[0]) for definition in DEFINITIONS}
    textures = {}
    for key in infos:
        texture = owned_asset(IMAGE_FOLDER + "/T_Vehicle_" + key, unreal.Texture2D)
        if texture:
            validate_texture(texture, infos[key])
        textures[key] = texture
    table = owned_asset(TABLE_PATH, unreal.DataTable)
    if table:
        validate_table(table, rows)
    if VERIFY_ONLY:
        require(table is not None and all(textures.values()), "Verify-only requires all four saved assets")
    else:
        asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
        for key, texture in list(textures.items()):
            if texture:
                continue
            task = unreal.AssetImportTask()
            task.filename = infos[key]["source"]
            task.destination_path = IMAGE_FOLDER
            task.destination_name = "T_Vehicle_" + key
            task.automated = True
            task.save = False
            task.replace_existing = False
            asset_tools.import_asset_tasks([task])
            texture = LIBRARY.load_asset(IMAGE_FOLDER + "/T_Vehicle_" + key)
            require(isinstance(texture, unreal.Texture2D), "Texture import failed: " + key)
            for property_name, value in texture_settings().items():
                texture.set_editor_property(property_name, value)
            LIBRARY.set_metadata_tag(texture, OWNER_KEY, OWNER)
            LIBRARY.set_metadata_tag(texture, SOURCE_KEY, infos[key]["sha256"])
            validate_texture(texture, infos[key])
            require(LIBRARY.save_loaded_asset(texture, False), "Texture save failed: " + key)
            textures[key] = texture
        if table is None:
            factory = unreal.DataTableFactory()
            factory.set_editor_property("struct", unreal.VehicleTemplate.static_struct())
            table = asset_tools.create_asset("DT_VehicleTemplates", FOLDER, unreal.DataTable, factory)
            require(table is not None, "DataTable creation failed")
            require(unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(
                table, json.dumps(rows, ensure_ascii=False)), "DataTable JSON import failed")
            LIBRARY.set_metadata_tag(table, OWNER_KEY, OWNER)
            validate_table(table, rows)
            require(LIBRARY.save_loaded_asset(table, False), "DataTable save failed")
    actual_rows = validate_table(table, rows)
    for key, texture in textures.items():
        validate_texture(texture, infos[key])
    report = {"owner": OWNER, "verify_only": VERIFY_ONLY, "table": table.get_path_name(),
              "sources": infos, "rows": actual_rows, "factory": verify_factory(table, textures),
              "texture_settings": {key: str(value) for key, value in texture_settings().items()}}
    report_folder = ROOT / "Saved" / "VehicleTemplateImports"
    report_folder.mkdir(parents=True, exist_ok=True)
    report_path = report_folder / (datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ") + ".json")
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    unreal.log("VEHICLE_TEMPLATES_OK rows=3 textures=3 factory=3 verify_only=%s report=%s" %
               (VERIFY_ONLY, report_path))


try:
    main()
except Exception as exception:
    unreal.log_error("VEHICLE_TEMPLATES_FAILED " + str(exception))
    raise
