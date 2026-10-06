// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sampling/EHBRoofMeshSampler.h"

#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "Sampling/EHBMeshSampleValidation.h"
#include "ScopedTransaction.h"
#include "StaticMeshAttributes.h"

#define LOCTEXT_NAMESPACE "EHBRoofMeshSampler"

namespace EHBRoofMeshSampler
{
	bool GetMeshDescription(UStaticMesh* StaticMesh, int32 LODIndex, const FMeshDescription*& OutMeshDescription, FText& OutError)
	{
		OutMeshDescription = nullptr;
		if (!StaticMesh)
		{
			OutError = LOCTEXT("NoStaticMesh", "\u91c7\u6837\u5931\u8d25\uff1a\u7f3a\u5c11\u5c4b\u9876\u7f51\u683c\u4f53\u3002");
			return false;
		}

		if (LODIndex < 0 || LODIndex >= StaticMesh->GetNumSourceModels())
		{
			OutError = FText::Format(
				LOCTEXT("InvalidLOD", "\u91c7\u6837\u5931\u8d25\uff1aLOD \u7d22\u5f15 {0} \u65e0\u6548\uff0c\u53ef\u7528\u8303\u56f4\u4e3a 0 \u5230 {1}\u3002"),
				FText::AsNumber(LODIndex),
				FText::AsNumber(FMath::Max(StaticMesh->GetNumSourceModels() - 1, 0)));
			return false;
		}

		OutMeshDescription = StaticMesh->GetMeshDescription(LODIndex);
		if (!OutMeshDescription)
		{
			OutError = LOCTEXT("NoMeshDescription", "\u91c7\u6837\u5931\u8d25\uff1a\u9759\u6001\u7f51\u683c\u4f53\u6ca1\u6709\u53ef\u8bfb\u53d6\u7684\u6e90\u7f51\u683c\u6570\u636e\u3002");
			return false;
		}

		return true;
	}

	FBox CalculateBounds(const FMeshDescription& MeshDescription)
	{
		FStaticMeshConstAttributes Attributes(MeshDescription);
		const TVertexAttributesConstRef<FVector3f> VertexPositions = Attributes.GetVertexPositions();

		FBox Bounds(ForceInit);
		for (const FVertexID VertexID : MeshDescription.Vertices().GetElementIDs())
		{
			Bounds += FVector(VertexPositions[VertexID]);
		}
		return Bounds;
	}

	bool AnalyzeMeshPart(
		UStaticMesh* StaticMesh,
		const FEHBRoofMeshSamplingOptions& Options,
		bool bRequired,
		const FText& PartLabel,
		FEHBRoofMeshPartAnalysis& OutPart,
		FText& OutError)
	{
		OutPart = FEHBRoofMeshPartAnalysis();
		if (!StaticMesh)
		{
			if (bRequired)
			{
				OutError = FText::Format(LOCTEXT("RequiredPartMissing", "\u91c7\u6837\u5931\u8d25\uff1a\u8bf7\u9009\u62e9{0}\u7f51\u683c\u4f53\u3002"), PartLabel);
				return false;
			}
			return true;
		}

		const FMeshDescription* MeshDescription = nullptr;
		if (!GetMeshDescription(StaticMesh, Options.LODIndex, MeshDescription, OutError))
		{
			OutError = FText::Format(LOCTEXT("PartReadFailed", "{0}\u7f51\u683c\u4f53\uff1a{1}"), PartLabel, OutError);
			return false;
		}

		const int32 TriangleCount = MeshDescription->Triangles().Num();
		const int32 VertexCount = MeshDescription->Vertices().Num();
		if (TriangleCount <= 0 || VertexCount <= 0)
		{
			OutError = FText::Format(LOCTEXT("PartEmpty", "\u91c7\u6837\u5931\u8d25\uff1a{0}\u7f51\u683c\u4f53\u6ca1\u6709\u6709\u6548\u9876\u70b9\u6216\u4e09\u89d2\u9762\u3002"), PartLabel);
			return false;
		}

		if (TriangleCount > Options.MaxTriangleCountPerMesh)
		{
			OutError = FText::Format(
				LOCTEXT("PartTooManyTriangles", "\u91c7\u6837\u5931\u8d25\uff1a{0}\u7f51\u683c\u4f53\u6709 {1} \u4e2a\u4e09\u89d2\u9762\uff0c\u8d85\u8fc7\u4e0a\u9650 {2}\u3002"),
				PartLabel,
				FText::AsNumber(TriangleCount),
				FText::AsNumber(Options.MaxTriangleCountPerMesh));
			return false;
		}

		const FBox Bounds = CalculateBounds(*MeshDescription);
		if (!Bounds.IsValid)
		{
			OutError = FText::Format(LOCTEXT("PartInvalidBounds", "\u91c7\u6837\u5931\u8d25\uff1a{0}\u7f51\u683c\u4f53\u7684\u5305\u56f4\u65e0\u6548\u3002"), PartLabel);
			return false;
		}

		OutPart.bHasMesh = true;
		OutPart.BoundsSize = Bounds.GetSize();
		OutPart.SourceTriangleCount = TriangleCount;
		OutPart.SourceVertexCount = VertexCount;
		return true;
	}

	void FillPatch(UStaticMesh* StaticMesh, const FEHBRoofMeshSamplingOptions& Options, FEHBRoofSurfacePatch& OutPatch)
	{
		OutPatch = FEHBRoofSurfacePatch();
		if (!StaticMesh)
		{
			return;
		}

		OutPatch.SourceStaticMesh = StaticMesh;
		OutPatch.SourceMeshName = StaticMesh->GetFName();
		OutPatch.LODIndex = Options.LODIndex;

		const FMeshDescription* MeshDescription = nullptr;
		FText Error;
		if (GetMeshDescription(StaticMesh, Options.LODIndex, MeshDescription, Error) && MeshDescription)
		{
			const FBox Bounds = CalculateBounds(*MeshDescription);
			OutPatch.SourceBoundsMin = Bounds.Min;
			OutPatch.SourceBoundsMax = Bounds.Max;
			OutPatch.BoundsSize = Bounds.GetSize();
			OutPatch.SourceTriangleCount = MeshDescription->Triangles().Num();
			OutPatch.SourceVertexCount = MeshDescription->Vertices().Num();
		}

		OutPatch.Materials.Reserve(StaticMesh->GetStaticMaterials().Num());
		for (const FStaticMaterial& StaticMaterial : StaticMesh->GetStaticMaterials())
		{
			OutPatch.Materials.Add(StaticMaterial.MaterialInterface);
		}
	}

	TSoftObjectPtr<UMaterialInterface> GetFirstMaterial(UStaticMesh* StaticMesh)
	{
		if (!StaticMesh || StaticMesh->GetStaticMaterials().IsEmpty())
		{
			return nullptr;
		}
		return StaticMesh->GetStaticMaterials()[0].MaterialInterface;
	}
}

bool FEHBRoofMeshSampler::AnalyzeMeshes(
	UStaticMesh* SurfaceTileMesh,
	UStaticMesh* RidgeTileMesh,
	UStaticMesh* ValleyTileMesh,
	const FEHBRoofMeshSamplingOptions& Options,
	FEHBRoofMeshAnalysis& OutAnalysis)
{
	OutAnalysis = FEHBRoofMeshAnalysis();

	FText Error;
	if (!EHBRoofMeshSampler::AnalyzeMeshPart(SurfaceTileMesh, Options, true, LOCTEXT("SurfaceTilePart", "\u5c4b\u9762\u74e6\u7247"), OutAnalysis.SurfaceTile, Error)
		|| !EHBRoofMeshSampler::AnalyzeMeshPart(RidgeTileMesh, Options, false, LOCTEXT("RidgeTilePart", "\u5c4b\u810a\u74e6\u7247"), OutAnalysis.RidgeTile, Error)
		|| !EHBRoofMeshSampler::AnalyzeMeshPart(ValleyTileMesh, Options, false, LOCTEXT("ValleyTilePart", "\u8c37\u7ebf\u74e6\u7247"), OutAnalysis.ValleyTile, Error))
	{
		OutAnalysis.Message = Error;
		return false;
	}

	OutAnalysis.TotalTriangleCount = OutAnalysis.SurfaceTile.SourceTriangleCount
		+ OutAnalysis.RidgeTile.SourceTriangleCount
		+ OutAnalysis.ValleyTile.SourceTriangleCount;
	OutAnalysis.TotalVertexCount = OutAnalysis.SurfaceTile.SourceVertexCount
		+ OutAnalysis.RidgeTile.SourceVertexCount
		+ OutAnalysis.ValleyTile.SourceVertexCount;
	OutAnalysis.bCanSample = true;
	OutAnalysis.Message = FText::Format(
		LOCTEXT("AnalysisSucceeded", "\u68c0\u6d4b\u901a\u8fc7\uff1a\u5c4b\u9762 {0} \u4e2a\u4e09\u89d2\u9762\uff0c\u5c4b\u810a {1} \u4e2a\uff0c\u8c37\u7ebf {2} \u4e2a\u3002"),
		FText::AsNumber(OutAnalysis.SurfaceTile.SourceTriangleCount),
		FText::AsNumber(OutAnalysis.RidgeTile.SourceTriangleCount),
		FText::AsNumber(OutAnalysis.ValleyTile.SourceTriangleCount));
	return true;
}

bool FEHBRoofMeshSampler::SampleToDataTable(
	UStaticMesh* SurfaceTileMesh,
	UStaticMesh* RidgeTileMesh,
	UStaticMesh* ValleyTileMesh,
	UDataTable* TargetDataTable,
	FName RowName,
	const FEHBRoofMeshSamplingOptions& Options,
	FEHBRoofMeshSamplingResult& OutResult,
	FText& OutError)
{
	OutResult = FEHBRoofMeshSamplingResult();
	OutError = FText::GetEmpty();

	if (!TargetDataTable)
	{
		OutError = LOCTEXT("NoDataTable", "\u91c7\u6837\u5931\u8d25\uff1a\u8bf7\u9009\u62e9\u5c4b\u9876\u91c7\u6837\u8868\u3002");
		return false;
	}

	if (TargetDataTable->GetRowStruct() != FEHBRoofMeshData::StaticStruct())
	{
		OutError = LOCTEXT("WrongDataTableRowStruct", "\u91c7\u6837\u5931\u8d25\uff1a\u6570\u636e\u8868\u884c\u7ed3\u6784\u5fc5\u987b\u662f FEHBRoofMeshData\u3002");
		return false;
	}

	if (RowName.IsNone())
	{
		OutError = LOCTEXT("NoRowName", "\u91c7\u6837\u5931\u8d25\uff1a\u8bf7\u8f93\u5165\u91c7\u6837\u9879\u540d\u79f0\u3002");
		return false;
	}

	FEHBRoofMeshAnalysis Analysis;
	if (!AnalyzeMeshes(SurfaceTileMesh, RidgeTileMesh, ValleyTileMesh, Options, Analysis))
	{
		OutResult.Analysis = Analysis;
		OutError = Analysis.Message;
		return false;
	}

	if (!Options.bOverwriteExistingRows
		&& TargetDataTable->FindRow<FEHBRoofMeshData>(RowName, TEXT("FEHBRoofMeshSampler::SampleToDataTable"), false))
	{
		OutError = LOCTEXT("RowAlreadyExists", "\u91c7\u6837\u5931\u8d25\uff1a\u5df2\u5b58\u5728\u540c\u540d\u884c\u3002");
		return false;
	}

	FEHBRoofMeshData SampleRow;
	EHBRoofMeshSampler::FillPatch(SurfaceTileMesh, Options, SampleRow.SurfaceTileMesh);
	EHBRoofMeshSampler::FillPatch(RidgeTileMesh, Options, SampleRow.RidgeTileMesh);
	EHBRoofMeshSampler::FillPatch(ValleyTileMesh, Options, SampleRow.ValleyTileMesh);
	SampleRow.RecommendedLength = FMath::Max(1.0f, Options.RecommendedLength);
	SampleRow.RecommendedWidth = FMath::Max(1.0f, Options.RecommendedWidth);
	SampleRow.RecommendedPitchDegrees = FMath::Clamp(Options.RecommendedPitchDegrees, 1.0f, 89.0f);
	SampleRow.RecommendedThickness = FMath::Max(0.1f, Options.RecommendedThickness);
	SampleRow.RecommendedEaveOffset = FMath::Max(0.0f, Options.RecommendedEaveOffset);
	SampleRow.SurfaceMaterial = EHBRoofMeshSampler::GetFirstMaterial(SurfaceTileMesh);
	SampleRow.RidgeMaterial = EHBRoofMeshSampler::GetFirstMaterial(RidgeTileMesh);
	SampleRow.ValleyMaterial = EHBRoofMeshSampler::GetFirstMaterial(ValleyTileMesh);
	SampleRow.SourceTriangleCount = Analysis.TotalTriangleCount;
	SampleRow.SourceVertexCount = Analysis.TotalVertexCount;
	FEHBMeshSampleValidation::InitializeMetadata(
		SampleRow.TemplateMetadata,
		EEHBMeshSampleTemplateKind::Roof,
		RowName,
		SurfaceTileMesh,
		Analysis.TotalTriangleCount);

	const FScopedTransaction Transaction(LOCTEXT("SampleRoofMeshTransaction", "\u751f\u6210\u5c4b\u9876\u91c7\u6837"));
	TargetDataTable->Modify();
	TargetDataTable->AddRow(RowName, SampleRow);
	TargetDataTable->MarkPackageDirty();

	OutResult.Analysis = Analysis;
	OutResult.RowName = RowName;
	return true;
}

#undef LOCTEXT_NAMESPACE
