// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sampling/EHBMeshSampleValidation.h"

#include "Actors/EHB_DoorWindow.h"
#include "Actors/EHB_Pillar.h"
#include "Actors/EHB_Railing.h"
#include "Actors/EHB_Wall.h"
#include "Sampling/EHBDoorWindowMeshData.h"
#include "Sampling/EHBPillarMeshData.h"
#include "Sampling/EHBRailingMeshData.h"
#include "Sampling/EHBWallMeshData.h"

#define LOCTEXT_NAMESPACE "EHBMeshSampleValidation"

namespace
{
	constexpr int32 EHBSampleLightTriangleCost = 2000;
	constexpr int32 EHBSampleModerateTriangleCost = 12000;
	constexpr int32 EHBSampleHeavyTriangleCost = 50000;
	constexpr int32 EHBWallMaxRepeatCount = 1024;
	constexpr float EHBLargeSizeDeltaRatio = 0.25f;

	bool HasUsableWallSurface(const FEHBWallMeshSampleSurface& Surface)
	{
		return Surface.SampleTriangleCount > 0
			&& Surface.Vertices.Num() > 0
			&& Surface.Triangles.Num() >= 3;
	}

	int32 EstimateWallRepeatCount(const FEHBWallMeshData& Row, const AEHB_Wall* TargetWall)
	{
		if (!TargetWall)
		{
			return 1;
		}

		const float SourceWidth = FMath::Max(
			1.0f,
			Row.WallWidth > UE_SMALL_NUMBER
				? Row.WallWidth
				: Row.WallLocalBoundsMax.X - Row.WallLocalBoundsMin.X);
		const float WallLength = FVector::Dist2D(TargetWall->LocalStart, TargetWall->LocalEnd);
		return FMath::Max(1, FMath::CeilToInt(WallLength / SourceWidth));
	}

	bool IsLargeRelativeDelta(float CurrentValue, float NewValue)
	{
		const float SafeCurrent = FMath::Max(1.0f, FMath::Abs(CurrentValue));
		return FMath::Abs(CurrentValue - NewValue) / SafeCurrent > EHBLargeSizeDeltaRatio;
	}
}

bool FEHBMeshSampleValidationResult::HasErrors() const
{
	return Issues.ContainsByPredicate([](const FEHBMeshSampleValidationIssue& Issue)
	{
		return Issue.Severity == EEHBMeshSampleValidationSeverity::Error;
	});
}

void FEHBMeshSampleValidationResult::AddIssue(
	EEHBMeshSampleValidationSeverity Severity,
	const FText& Message)
{
	FEHBMeshSampleValidationIssue& Issue = Issues.AddDefaulted_GetRef();
	Issue.Severity = Severity;
	Issue.Message = Message;
	if (Severity == EEHBMeshSampleValidationSeverity::Error)
	{
		bValid = false;
	}
}

FText FEHBMeshSampleValidationResult::MakeSummaryText() const
{
	int32 ErrorCount = 0;
	int32 WarningCount = 0;
	for (const FEHBMeshSampleValidationIssue& Issue : Issues)
	{
		if (Issue.Severity == EEHBMeshSampleValidationSeverity::Error)
		{
			++ErrorCount;
		}
		else if (Issue.Severity == EEHBMeshSampleValidationSeverity::Warning)
		{
			++WarningCount;
		}
	}

	if (ErrorCount > 0)
	{
		return FText::Format(
			LOCTEXT("ValidationFailedSummary", "Sample is invalid: {0} errors, {1} warnings."),
			FText::AsNumber(ErrorCount),
			FText::AsNumber(WarningCount));
	}

	if (WarningCount > 0)
	{
		return FText::Format(
			LOCTEXT("ValidationWarningSummary", "Sample can be applied with {0} warnings."),
			FText::AsNumber(WarningCount));
	}

	return LOCTEXT("ValidationCleanSummary", "Sample is ready to apply.");
}

EEHBMeshSamplePerformanceClass FEHBMeshSampleValidation::ClassifyCost(
	int32 EstimatedTriangleCost,
	int32 DisconnectedComponentCount)
{
	if (EstimatedTriangleCost <= 0)
	{
		return EEHBMeshSamplePerformanceClass::Unknown;
	}

	const bool bManyDisconnectedComponents = DisconnectedComponentCount > 32;
	if (EstimatedTriangleCost >= EHBSampleHeavyTriangleCost || bManyDisconnectedComponents)
	{
		return EEHBMeshSamplePerformanceClass::Critical;
	}
	if (EstimatedTriangleCost >= EHBSampleModerateTriangleCost || DisconnectedComponentCount > 12)
	{
		return EEHBMeshSamplePerformanceClass::Heavy;
	}
	if (EstimatedTriangleCost >= EHBSampleLightTriangleCost || DisconnectedComponentCount > 4)
	{
		return EEHBMeshSamplePerformanceClass::Moderate;
	}
	return EEHBMeshSamplePerformanceClass::Light;
}

void FEHBMeshSampleValidation::InitializeMetadata(
	FEHBMeshSampleTemplateMetadata& Metadata,
	EEHBMeshSampleTemplateKind TemplateKind,
	FName TemplateName,
	UObject* SourceAsset,
	int32 EstimatedApplyCost,
	int32 DisconnectedComponentCount)
{
	if (!Metadata.TemplateGuid.IsValid())
	{
		Metadata.TemplateGuid = FGuid::NewGuid();
	}
	Metadata.SchemaVersion = FMath::Max(1, Metadata.SchemaVersion);
	Metadata.TemplateKind = TemplateKind;
	Metadata.TemplateName = TemplateName;
	Metadata.SourceAssetPath = SourceAsset ? SourceAsset->GetPathName() : FString();
	Metadata.EstimatedApplyCost = FMath::Max(0, EstimatedApplyCost);
	Metadata.PerformanceClass = ClassifyCost(Metadata.EstimatedApplyCost, DisconnectedComponentCount);
}

FEHBMeshSampleValidationResult FEHBMeshSampleValidation::ValidateWallSurfaceForTarget(
	const FEHBWallMeshData& Row,
	const AEHB_Wall* TargetWall,
	bool bCoverBothSides)
{
	FEHBMeshSampleValidationResult Result;
	const int32 RepeatCount = EstimateWallRepeatCount(Row, TargetWall);
	const int32 TriangleCost = FMath::Max(0, Row.SourceTriangleCount) * RepeatCount * (bCoverBothSides ? 2 : 1);
	Result.EstimatedApplyCost = TriangleCost;
	Result.PerformanceClass = ClassifyCost(TriangleCost, Row.DisconnectedComponentCount);

	if (Row.TemplateMetadata.SchemaVersion <= 0)
	{
		Result.AddIssue(EEHBMeshSampleValidationSeverity::Warning, LOCTEXT("WallLegacySchema", "Wall sample has no valid schema version; it may be an older row."));
	}
	if (Row.WallWidth <= UE_SMALL_NUMBER || Row.WallHeight <= UE_SMALL_NUMBER || Row.WallThickness <= UE_SMALL_NUMBER)
	{
		Result.AddIssue(EEHBMeshSampleValidationSeverity::Error, LOCTEXT("WallInvalidSize", "Wall sample has invalid width, height or thickness."));
	}
	if (!HasUsableWallSurface(Row.FrontSurface) || !HasUsableWallSurface(Row.BackSurface))
	{
		Result.AddIssue(EEHBMeshSampleValidationSeverity::Error, LOCTEXT("WallMissingSide", "Wall sample must contain usable front and back surfaces."));
	}
	if (RepeatCount > EHBWallMaxRepeatCount)
	{
		Result.AddIssue(EEHBMeshSampleValidationSeverity::Error, LOCTEXT("WallRepeatTooHigh", "Wall sample would repeat too many times on this wall."));
	}
	else if (RepeatCount > 256)
	{
		Result.AddIssue(EEHBMeshSampleValidationSeverity::Warning, LOCTEXT("WallRepeatHigh", "Wall sample repeats many times on this wall and may be expensive."));
	}
	if (Result.PerformanceClass >= EEHBMeshSamplePerformanceClass::Heavy)
	{
		Result.AddIssue(EEHBMeshSampleValidationSeverity::Warning, LOCTEXT("WallHeavySample", "Wall sample has a high estimated mesh cost for this target."));
	}
	if (TargetWall && TargetWall->DoorWindowConnections.Num() > 0 && Row.SourceTriangleCount > EHBSampleModerateTriangleCost)
	{
		Result.AddIssue(EEHBMeshSampleValidationSeverity::Warning, LOCTEXT("WallOpeningsHeavy", "This wall has openings; a dense sampled wall surface will make clipping more expensive."));
	}
	return Result;
}

FEHBMeshSampleValidationResult FEHBMeshSampleValidation::ValidatePillarForTarget(
	const FEHBPillarMeshData& Row,
	const AEHB_Pillar* TargetPillar)
{
	FEHBMeshSampleValidationResult Result;
	Result.EstimatedApplyCost = FMath::Max(0, Row.SourceTriangleCount);
	Result.PerformanceClass = ClassifyCost(Result.EstimatedApplyCost, Row.DisconnectedComponentCount);

	if (Row.TemplateMetadata.SchemaVersion <= 0)
	{
		Result.AddIssue(EEHBMeshSampleValidationSeverity::Warning, LOCTEXT("PillarLegacySchema", "Pillar sample has no valid schema version; it may be an older row."));
	}
	if (Row.Vertices.IsEmpty() || Row.Triangles.Num() < 3)
	{
		Result.AddIssue(EEHBMeshSampleValidationSeverity::Error, LOCTEXT("PillarNoGeometry", "Pillar sample has no usable geometry."));
	}
	if (Row.Width <= UE_SMALL_NUMBER || Row.Depth <= UE_SMALL_NUMBER || Row.Height <= UE_SMALL_NUMBER)
	{
		Result.AddIssue(EEHBMeshSampleValidationSeverity::Error, LOCTEXT("PillarInvalidSize", "Pillar sample has invalid dimensions."));
	}
	if (TargetPillar
		&& (IsLargeRelativeDelta(TargetPillar->Width, Row.Width)
			|| IsLargeRelativeDelta(TargetPillar->Depth, Row.Depth)
			|| IsLargeRelativeDelta(TargetPillar->Height, Row.Height)))
	{
		Result.AddIssue(EEHBMeshSampleValidationSeverity::Warning, LOCTEXT("PillarSizeChange", "Applying this sample will change the pillar dimensions and refresh connected walls."));
	}
	if (TargetPillar && TargetPillar->ConnectedWallGuids.Num() > 4)
	{
		Result.AddIssue(EEHBMeshSampleValidationSeverity::Warning, LOCTEXT("PillarManyConnections", "This pillar drives many connected walls; applying a sampled pillar may trigger a broad rebuild."));
	}
	if (Result.PerformanceClass >= EEHBMeshSamplePerformanceClass::Heavy)
	{
		Result.AddIssue(EEHBMeshSampleValidationSeverity::Warning, LOCTEXT("PillarHeavySample", "Pillar sample has a high estimated mesh cost."));
	}
	return Result;
}

FEHBMeshSampleValidationResult FEHBMeshSampleValidation::ValidateRailingForTarget(
	const FEHBRailingMeshData& Row,
	const AEHB_Railing* TargetRailing)
{
	FEHBMeshSampleValidationResult Result;
	const int32 TriangleCost =
		FMath::Max(0, Row.PostMesh.SourceTriangleCount)
		+ FMath::Max(0, Row.RailMesh.SourceTriangleCount)
		+ FMath::Max(0, Row.PanelMaterialSourceMesh.SourceTriangleCount);
	Result.EstimatedApplyCost = TriangleCost;
	Result.PerformanceClass = ClassifyCost(TriangleCost);

	if (Row.TemplateMetadata.SchemaVersion <= 0)
	{
		Result.AddIssue(EEHBMeshSampleValidationSeverity::Warning, LOCTEXT("RailingLegacySchema", "扶手采样没有有效的结构版本，可能来自旧数据。"));
	}
	const bool bHasPostMesh = !Row.PostMesh.SourceStaticMesh.IsNull();
	const bool bHasRailMesh = !Row.RailMesh.SourceStaticMesh.IsNull();
	if (!bHasPostMesh && !bHasRailMesh)
	{
		Result.AddIssue(EEHBMeshSampleValidationSeverity::Error, LOCTEXT("RailingMissingRequiredMeshPart", "扶手采样至少需要立柱网格体或横杆网格体中的一个。"));
	}
	if (false && Row.PostMesh.SourceStaticMesh.IsNull())
	{
		Result.AddIssue(EEHBMeshSampleValidationSeverity::Error, LOCTEXT("RailingMissingPostMesh", "扶手采样缺少立柱网格体。"));
	}
	if (false && Row.RailMesh.SourceStaticMesh.IsNull())
	{
		Result.AddIssue(EEHBMeshSampleValidationSeverity::Error, LOCTEXT("RailingMissingRailMesh", "扶手采样缺少横杆网格体。"));
	}
	if ((bHasPostMesh
			&& (Row.RecommendedPostWidth <= UE_SMALL_NUMBER
				|| Row.RecommendedPostHeight <= UE_SMALL_NUMBER
				|| Row.RecommendedPostSpacing <= UE_SMALL_NUMBER))
		|| (bHasRailMesh
			&& (Row.RecommendedRailHeight <= UE_SMALL_NUMBER
				|| Row.RecommendedRailThickness <= UE_SMALL_NUMBER
				|| Row.RecommendedMaxRailSegmentLength <= UE_SMALL_NUMBER)))
	{
		Result.AddIssue(EEHBMeshSampleValidationSeverity::Error, LOCTEXT("RailingInvalidDimensions", "扶手采样的推荐尺寸无效。"));
	}
	if (TargetRailing && TargetRailing->GateConnections.Num() > 0)
	{
		Result.AddIssue(EEHBMeshSampleValidationSeverity::Warning, LOCTEXT("RailingHasGates", "目标扶手包含围栏门，应用采样后会按新尺寸重新裁剪门洞。"));
	}
	if (Result.PerformanceClass >= EEHBMeshSamplePerformanceClass::Heavy)
	{
		Result.AddIssue(EEHBMeshSampleValidationSeverity::Warning, LOCTEXT("RailingHeavySample", "扶手采样的估算网格成本较高。"));
	}
	return Result;
}

FEHBMeshSampleValidationResult FEHBMeshSampleValidation::ValidateDoorWindowForTarget(
	const FEHBDoorWindowMeshData& Row,
	const AEHB_Wall* TargetWall)
{
	FEHBMeshSampleValidationResult Result;
	Result.EstimatedApplyCost = FMath::Max(0, Row.SourceTriangleCount);
	Result.PerformanceClass = ClassifyCost(Result.EstimatedApplyCost);

	if (Row.TemplateMetadata.SchemaVersion <= 0)
	{
		Result.AddIssue(EEHBMeshSampleValidationSeverity::Warning, LOCTEXT("DoorWindowLegacySchema", "Door/window sample has no valid schema version; it may be an older row."));
	}
	if (Row.OpeningWidth <= UE_SMALL_NUMBER || Row.OpeningHeight <= UE_SMALL_NUMBER || Row.OpeningThickness <= UE_SMALL_NUMBER)
	{
		Result.AddIssue(EEHBMeshSampleValidationSeverity::Error, LOCTEXT("DoorWindowInvalidOpening", "Door/window sample has invalid opening dimensions."));
	}
	if (Row.DoorWindowClass.IsNull())
	{
		Result.AddIssue(EEHBMeshSampleValidationSeverity::Error, LOCTEXT("DoorWindowMissingClass", "Door/window sample must reference a generated actor class."));
	}
	if (TargetWall)
	{
		const float WallLength = FVector::Dist2D(TargetWall->LocalStart, TargetWall->LocalEnd);
		if (Row.OpeningWidth >= WallLength)
		{
			Result.AddIssue(EEHBMeshSampleValidationSeverity::Error, LOCTEXT("DoorWindowTooWide", "Door/window opening is wider than the target wall."));
		}
		if (Row.OpeningHeight > TargetWall->Height + UE_SMALL_NUMBER)
		{
			Result.AddIssue(EEHBMeshSampleValidationSeverity::Warning, LOCTEXT("DoorWindowTallerThanWall", "Door/window opening is taller than the target wall."));
		}
	}
	return Result;
}

#undef LOCTEXT_NAMESPACE
