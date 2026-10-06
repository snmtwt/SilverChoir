#include "Data/Vehicles/VehicleDataLibrary.h"

FVehicleData UVehicleDataLibrary::CreateVehicleDataFromTemplate(const FVehicleTemplate& VehicleTemplate, FName TileId)
{
    FVehicleData Data;
    Data.VehicleId = FGuid::NewGuid();
    Data.Profile = VehicleTemplate.Profile;
    Data.Attributes = VehicleTemplate.Attributes.GetSanitized();
    Data.EntityData = VehicleTemplate.EntityData;
    Data.StrategicMovementData = VehicleTemplate.StrategicMovementData.GetSanitized();
    Data.RuntimeData.TileId = TileId;
    Data.RuntimeData.CurrentDurability = Data.Attributes.MaxDurability;
    Data.RuntimeData.CurrentFuel = Data.Attributes.MaxFuel;
    return Data;
}

bool UVehicleDataLibrary::CreateVehicleDataBatch(const FVehicleTemplate& VehicleTemplate, int32 Count,
    TArray<FVehicleData>& OutData, FText& OutError, FName TileId)
{
    OutData.Reset();
    OutError = FText::GetEmpty();
    if (Count < 0)
    {
        OutError = NSLOCTEXT("VehicleData", "InvalidCount", "创建车辆的数量不能小于 0。");
        return false;
    }

    OutData.Reserve(Count);
    for (int32 Index = 0; Index < Count; ++Index)
        OutData.Add(CreateVehicleDataFromTemplate(VehicleTemplate, TileId));
    return true;
}

bool UVehicleDataLibrary::CreateVehicleDataFromTable(UDataTable* Table, const TArray<FName>& RowNames,
    TArray<FVehicleData>& OutData, FText& OutError, FName TileId)
{
    OutData.Reset();
    OutError = FText::GetEmpty();
    if (!IsValid(Table) || Table->GetRowStruct() != FVehicleTemplate::StaticStruct())
    {
        OutError = NSLOCTEXT("VehicleData", "InvalidTemplateTable", "车辆模板表未配置，或行结构不是 FVehicleTemplate。");
        return false;
    }

    // Validate every requested row before allocating IDs, so a missing later row cannot produce partial vehicles.
    TArray<const FVehicleTemplate*> Templates;
    Templates.Reserve(RowNames.Num());
    for (const FName RowName : RowNames)
    {
        const FVehicleTemplate* Row = RowName.IsNone() ? nullptr
            : Table->FindRow<FVehicleTemplate>(RowName, TEXT("CreateVehicleDataFromTable"), false);
        if (!Row)
        {
            OutError = FText::Format(NSLOCTEXT("VehicleData", "MissingTemplateRow", "车辆模板行不存在或行名为空：{0}"), FText::FromName(RowName));
            return false;
        }
        Templates.Add(Row);
    }

    OutData.Reserve(Templates.Num());
    for (int32 Index = 0; Index < Templates.Num(); ++Index)
    {
        FVehicleData Data = CreateVehicleDataFromTemplate(*Templates[Index], TileId);
        Data.SourceTemplateRow = RowNames[Index];
        OutData.Add(MoveTemp(Data));
    }
    return true;
}
