// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sampling/EHBRailingMeshSampler.h"

#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "Sampling/EHBMeshSampleValidation.h"
#include "ScopedTransaction.h"
#include "StaticMeshAttributes.h"

#define LOCTEXT_NAMESPACE "EHBRailingMeshSampler"

namespace EHBRailingMeshSampler
{
	bool GetMeshDescription(UStaticMesh* StaticMesh, int32 LODIndex, const FMeshDescription*& OutMeshDescription, FText& OutError)
	{
		OutMeshDescription = nullptr;
		if (!StaticMesh)
		{
			OutError = LOCTEXT("NoStaticMesh", "\u91c7\u6837\u5931\u8d25\uff1a\u7f3a\u5c11\u9759\u6001\u7f51\u683c\u4f53\u3002");
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
		const FEHBRailingMeshSamplingOptions& Options,
		bool bRequired,
		const FText& PartLabel,
		FEHBRailingMeshPartAnalysis& OutPart,
		FText& OutError)
	{
		OutPart = FEHBRailingMeshPartAnalysis();
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

	void FillSamplePart(UStaticMesh* StaticMesh, const FEHBRailingMeshSamplingOptions& Options, FEHBRailingSampleMeshPart& OutPart)
	{
		OutPart = FEHBRailingSampleMeshPart();
		if (!StaticMesh)
		{
			return;
		}

		OutPart.SourceStaticMesh = StaticMesh;
		OutPart.SourceMeshName = StaticMesh->GetFName();
		OutPart.LODIndex = Options.LODIndex;

		const FMeshDescription* MeshDescription = nullptr;
		FText Error;
		if (GetMeshDescription(StaticMesh, Options.LODIndex, MeshDescription, Error) && MeshDescription)
		{
			const FBox Bounds = CalculateBounds(*MeshDescription);
			OutPart.SourceBoundsMin = Bounds.Min;
			OutPart.SourceBoundsMax = Bounds.Max;
			OutPart.BoundsSize = Bounds.GetSize();
			OutPart.SourceTriangleCount = MeshDescription->Triangles().Num();
			OutPart.SourceVertexCount = MeshDescription->Vertices().Num();
		}

		OutPart.Materials.Reserve(StaticMesh->GetStaticMaterials().Num());
		for (const FStaticMaterial& StaticMaterial : StaticMesh->GetStaticMaterials())
		{
			OutPart.Materials.Add(StaticMaterial.MaterialInterface);
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

	float GetSafeDimension(float Value, float Fallback)
	{
		return FMath::Max(0.1f, Value > UE_SMALL_NUMBER ? Value : Fallback);
	}

	float GetPanelHorizontalWidth(const FVector& BoundsSize)
	{
		return FMath::Max(FMath::Abs(BoundsSize.X), FMath::Abs(BoundsSize.Y));
	}

	float GetPanelHorizontalThickness(const FVector& BoundsSize)
	{
		return FMath::Min(FMath::Abs(BoundsSize.X), FMath::Abs(BoundsSize.Y));
	}

	FName GetPanelWidthAxisName(const FVector& BoundsSize)
	{
		return FMath::Abs(BoundsSize.Y) > FMath::Abs(BoundsSize.X)
			? FName(TEXT("Y"))
			: FName(TEXT("X"));
	}

	FText DescribeMeshPart(const FEHBRailingMeshPartAnalysis& Part)
	{
		return Part.bHasMesh
			? FText::Format(
				LOCTEXT("PresentMeshPartDescription", "{0}x{1}x{2} cm"),
				FText::AsNumber(Part.BoundsSize.X),
				FText::AsNumber(Part.BoundsSize.Y),
				FText::AsNumber(Part.BoundsSize.Z))
			: LOCTEXT("DefaultMeshPartDescription", "\u672a\u9009\u62e9\uff0c\u66ff\u6362\u65f6\u4f7f\u7528\u9ed8\u8ba4");
	}
}

bool FEHBRailingMeshSampler::AnalyzeMeshes(
	UStaticMesh* PostMesh,
	UStaticMesh* RailMesh,
	UStaticMesh* PanelMaterialSourceMesh,
	const FEHBRailingMeshSamplingOptions& Options,
	FEHBRailingMeshAnalysis& OutAnalysis)
{
	OutAnalysis = FEHBRailingMeshAnalysis();

	FText Error;
	if (!EHBRailingMeshSampler::AnalyzeMeshPart(PostMesh, Options, false, LOCTEXT("PostPart", "\u7acb\u67f1"), OutAnalysis.Post, Error)
		|| !EHBRailingMeshSampler::AnalyzeMeshPart(RailMesh, Options, false, LOCTEXT("RailPart", "\u6a2a\u6746"), OutAnalysis.Rail, Error)
		|| !EHBRailingMeshSampler::AnalyzeMeshPart(PanelMaterialSourceMesh, Options, false, LOCTEXT("PanelPart", "\u56f4\u677f"), OutAnalysis.Panel, Error))
	{
		OutAnalysis.Message = Error;
		return false;
	}

	if (!OutAnalysis.Post.bHasMesh && !OutAnalysis.Rail.bHasMesh)
	{
		OutAnalysis.Message = LOCTEXT("NoRequiredRailingMeshPart", "\u91c7\u6837\u5931\u8d25\uff1a\u7acb\u67f1\u7f51\u683c\u4f53\u548c\u6a2a\u6746\u7f51\u683c\u4f53\u81f3\u5c11\u9700\u8981\u9009\u62e9\u4e00\u4e2a\u3002");
		return false;
	}

	OutAnalysis.RecommendedPostWidth = EHBRailingMeshSampler::GetSafeDimension(
		FMath::Max(OutAnalysis.Post.BoundsSize.X, OutAnalysis.Post.BoundsSize.Y),
		8.0f);
	OutAnalysis.RecommendedPostHeight = EHBRailingMeshSampler::GetSafeDimension(OutAnalysis.Post.BoundsSize.Z, 100.0f);
	OutAnalysis.RecommendedRailThickness = EHBRailingMeshSampler::GetSafeDimension(
		FMath::Max(OutAnalysis.Rail.BoundsSize.Y, OutAnalysis.Rail.BoundsSize.Z),
		OutAnalysis.RecommendedPostWidth);
	OutAnalysis.RecommendedRailHeight = OutAnalysis.RecommendedPostHeight;
	OutAnalysis.RecommendedMaxRailSegmentLength = EHBRailingMeshSampler::GetSafeDimension(
		OutAnalysis.Rail.BoundsSize.X,
		FMath::Max(Options.PostSpacing, OutAnalysis.RecommendedRailThickness));
	OutAnalysis.RecommendedPostSpacing = FMath::Max(1.0f, Options.PostSpacing);

	if (OutAnalysis.Panel.bHasMesh)
	{
		OutAnalysis.PanelWidthAxis = EHBRailingMeshSampler::GetPanelWidthAxisName(OutAnalysis.Panel.BoundsSize);
		OutAnalysis.RecommendedPanelWidth = EHBRailingMeshSampler::GetSafeDimension(
			EHBRailingMeshSampler::GetPanelHorizontalWidth(OutAnalysis.Panel.BoundsSize),
			OutAnalysis.RecommendedPostSpacing);
		OutAnalysis.RecommendedPanelThickness = EHBRailingMeshSampler::GetSafeDimension(
			EHBRailingMeshSampler::GetPanelHorizontalThickness(OutAnalysis.Panel.BoundsSize),
			1.0f);
		OutAnalysis.RecommendedPostSpacing = OutAnalysis.RecommendedPanelWidth;
		const float PanelHeight = FMath::Clamp(OutAnalysis.Panel.BoundsSize.Z, 1.0f, OutAnalysis.RecommendedPostHeight);
		OutAnalysis.RecommendedPanelBottomOffset = FMath::Max(0.0f, (OutAnalysis.RecommendedPostHeight - PanelHeight) * 0.5f);
		OutAnalysis.RecommendedPanelTopOffset = OutAnalysis.RecommendedPanelBottomOffset + PanelHeight;
	}
	else
	{
		OutAnalysis.RecommendedPanelBottomOffset = OutAnalysis.RecommendedPostHeight * 0.1f;
		OutAnalysis.RecommendedPanelTopOffset = OutAnalysis.RecommendedPostHeight * 0.9f;
	}
	OutAnalysis.RecommendedPanelTopOffset = FMath::Max(
		OutAnalysis.RecommendedPanelBottomOffset + 1.0f,
		OutAnalysis.RecommendedPanelTopOffset);

	OutAnalysis.bCanSample = true;
	OutAnalysis.Message = FText::Format(
		LOCTEXT("AnalysisSucceeded", "\u68c0\u6d4b\u901a\u8fc7\uff1a\u7acb\u67f1 {0}\uff0c\u6a2a\u6746 {1}\uff0c\u56f4\u677f {2}\u3002"),
		EHBRailingMeshSampler::DescribeMeshPart(OutAnalysis.Post),
		EHBRailingMeshSampler::DescribeMeshPart(OutAnalysis.Rail),
		OutAnalysis.Panel.bHasMesh ? LOCTEXT("PanelPresent", "\u5df2\u9009\u62e9") : LOCTEXT("PanelAbsent", "\u65e0"));
	return true;
}

bool FEHBRailingMeshSampler::SampleToDataTable(
	UStaticMesh* PostMesh,
	UStaticMesh* RailMesh,
	UStaticMesh* PanelMaterialSourceMesh,
	UDataTable* TargetDataTable,
	FName RowName,
	const FEHBRailingMeshSamplingOptions& Options,
	FEHBRailingMeshSamplingResult& OutResult,
	FText& OutError)
{
	OutResult = FEHBRailingMeshSamplingResult();
	OutError = FText::GetEmpty();

	if (!TargetDataTable)
	{
		OutError = LOCTEXT("NoDataTable", "\u91c7\u6837\u5931\u8d25\uff1a\u8bf7\u9009\u62e9\u6276\u624b\u91c7\u6837\u8868\u3002");
		return false;
	}

	if (TargetDataTable->GetRowStruct() != FEHBRailingMeshData::StaticStruct())
	{
		OutError = LOCTEXT("WrongDataTableRowStruct", "\u91c7\u6837\u5931\u8d25\uff1a\u6570\u636e\u8868\u884c\u7ed3\u6784\u5fc5\u987b\u662f FEHBRailingMeshData\u3002");
		return false;
	}

	if (RowName.IsNone())
	{
		OutError = LOCTEXT("NoRowName", "\u91c7\u6837\u5931\u8d25\uff1a\u8bf7\u8f93\u5165\u91c7\u6837\u9879\u540d\u79f0\u3002");
		return false;
	}

	FEHBRailingMeshAnalysis Analysis;
	if (!AnalyzeMeshes(PostMesh, RailMesh, PanelMaterialSourceMesh, Options, Analysis))
	{
		OutResult.Analysis = Analysis;
		OutError = Analysis.Message;
		return false;
	}

	if (!Options.bOverwriteExistingRows
		&& TargetDataTable->FindRow<FEHBRailingMeshData>(RowName, TEXT("FEHBRailingMeshSampler::SampleToDataTable"), false))
	{
		OutError = LOCTEXT("RowAlreadyExists", "\u91c7\u6837\u5931\u8d25\uff1a\u5df2\u5b58\u5728\u540c\u540d\u884c\u3002");
		return false;
	}

	FEHBRailingMeshData SampleRow;
	EHBRailingMeshSampler::FillSamplePart(PostMesh, Options, SampleRow.PostMesh);
	EHBRailingMeshSampler::FillSamplePart(RailMesh, Options, SampleRow.RailMesh);
	EHBRailingMeshSampler::FillSamplePart(PanelMaterialSourceMesh, Options, SampleRow.PanelMaterialSourceMesh);
	SampleRow.FillMode = Options.FillMode;
	SampleRow.RecommendedPostWidth = Analysis.RecommendedPostWidth;
	SampleRow.RecommendedPostHeight = Analysis.RecommendedPostHeight;
	SampleRow.RecommendedPostSpacing = FMath::Max(1.0f, Analysis.RecommendedPostSpacing);
	SampleRow.RecommendedRailHeight = Analysis.RecommendedRailHeight;
	SampleRow.RecommendedRailThickness = Analysis.RecommendedRailThickness;
	SampleRow.RecommendedMaxRailSegmentLength = Analysis.RecommendedMaxRailSegmentLength;
	SampleRow.RecommendedPanelBottomOffset = Analysis.RecommendedPanelBottomOffset;
	SampleRow.RecommendedPanelTopOffset = Analysis.RecommendedPanelTopOffset;
	SampleRow.PostMaterial = EHBRailingMeshSampler::GetFirstMaterial(PostMesh);
	SampleRow.RailMaterial = EHBRailingMeshSampler::GetFirstMaterial(RailMesh);
	SampleRow.PanelMaterial = EHBRailingMeshSampler::GetFirstMaterial(PanelMaterialSourceMesh);
	FEHBMeshSampleValidation::InitializeMetadata(
		SampleRow.TemplateMetadata,
		EEHBMeshSampleTemplateKind::Railing,
		RowName,
		PostMesh ? PostMesh : RailMesh,
		SampleRow.PostMesh.SourceTriangleCount + SampleRow.RailMesh.SourceTriangleCount + SampleRow.PanelMaterialSourceMesh.SourceTriangleCount);

	const FScopedTransaction Transaction(LOCTEXT("SampleRailingMeshTransaction", "\u751f\u6210\u6276\u624b\u91c7\u6837"));
	TargetDataTable->Modify();
	TargetDataTable->AddRow(RowName, SampleRow);
	TargetDataTable->MarkPackageDirty();

	OutResult.Analysis = Analysis;
	OutResult.RowName = RowName;
	return true;
}

#undef LOCTEXT_NAMESPACE
