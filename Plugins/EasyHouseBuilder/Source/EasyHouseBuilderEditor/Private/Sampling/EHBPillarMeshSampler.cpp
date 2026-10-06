// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sampling/EHBPillarMeshSampler.h"

#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "Sampling/EHBMeshSampleValidation.h"
#include "ScopedTransaction.h"
#include "StaticMeshAttributes.h"

#define LOCTEXT_NAMESPACE "EHBPillarMeshSampler"

namespace EHBPillarMeshSampler
{
	/** 安全读取静态网格体指定 LOD 的源 MeshDescription，并把失败原因转换成中文状态文本。 */
	bool GetMeshDescription(UStaticMesh* StaticMesh, int32 LODIndex, const FMeshDescription*& OutMeshDescription, FText& OutError)
	{
		OutMeshDescription = nullptr;

		if (!StaticMesh)
		{
			OutError = LOCTEXT("NoStaticMesh", "采样失败：请先选择一个静态网格体。");
			return false;
		}

		if (LODIndex < 0 || LODIndex >= StaticMesh->GetNumSourceModels())
		{
			OutError = FText::Format(
				LOCTEXT("InvalidLOD", "采样失败：LOD 索引 {0} 无效。当前静态网格体可用 LOD 范围是 0 到 {1}。"),
				FText::AsNumber(LODIndex),
				FText::AsNumber(FMath::Max(StaticMesh->GetNumSourceModels() - 1, 0)));
			return false;
		}

		OutMeshDescription = StaticMesh->GetMeshDescription(LODIndex);
		if (!OutMeshDescription)
		{
			OutError = LOCTEXT("NoMeshDescription", "采样失败：这个静态网格体没有可读取的源网格数据。请确认资产保留了源模型数据，或尝试使用 LOD0。");
			return false;
		}

		return true;
	}

	/** 遍历源顶点计算本地包围盒，用于记录柱体模板尺寸。 */
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

	/** 通过三角面共享顶点关系统计分离网格块数量，用于帮助判断模型是否由很多碎件组成。 */
	int32 CountDisconnectedComponents(const FMeshDescription& MeshDescription)
	{
		TMap<FVertexID, TArray<FTriangleID>> VertexToTriangles;
		for (const FTriangleID TriangleID : MeshDescription.Triangles().GetElementIDs())
		{
			const TArrayView<const FVertexID> VertexIDs = MeshDescription.GetTriangleVertices(TriangleID);
			for (const FVertexID VertexID : VertexIDs)
			{
				VertexToTriangles.FindOrAdd(VertexID).Add(TriangleID);
			}
		}

		int32 ComponentCount = 0;
		TSet<FTriangleID> VisitedTriangles;
		TArray<FTriangleID> Stack;

		for (const FTriangleID StartTriangleID : MeshDescription.Triangles().GetElementIDs())
		{
			if (VisitedTriangles.Contains(StartTriangleID))
			{
				continue;
			}

			ComponentCount++;
			Stack.Reset();
			Stack.Add(StartTriangleID);
			VisitedTriangles.Add(StartTriangleID);

			while (Stack.Num() > 0)
			{
				const FTriangleID TriangleID = Stack.Pop(EAllowShrinking::No);
				const TArrayView<const FVertexID> VertexIDs = MeshDescription.GetTriangleVertices(TriangleID);
				for (const FVertexID VertexID : VertexIDs)
				{
					if (const TArray<FTriangleID>* NeighborTriangles = VertexToTriangles.Find(VertexID))
					{
						for (const FTriangleID NeighborTriangleID : *NeighborTriangles)
						{
							if (!VisitedTriangles.Contains(NeighborTriangleID))
							{
								VisitedTriangles.Add(NeighborTriangleID);
								Stack.Add(NeighborTriangleID);
							}
						}
					}
				}
			}
		}

		return ComponentCount;
	}

	/** 从 MeshDescription 的 PolygonGroup 读取三角面材质槽索引，失败时回退到 PolygonGroup 序号。 */
	int32 GetTriangleMaterialIndex(
		UStaticMesh* StaticMesh,
		const FMeshDescription& MeshDescription,
		const FStaticMeshConstAttributes& Attributes,
		FTriangleID TriangleID)
	{
		const TTriangleAttributesConstRef<FPolygonGroupID> TrianglePolygonGroups = Attributes.GetTrianglePolygonGroupIndices();
		const TPolygonGroupAttributesConstRef<FName> PolygonGroupMaterialSlotNames = Attributes.GetPolygonGroupMaterialSlotNames();

		if (!TrianglePolygonGroups.IsValid())
		{
			return 0;
		}

		const FPolygonGroupID PolygonGroupID = TrianglePolygonGroups[TriangleID];
		if (PolygonGroupMaterialSlotNames.IsValid() && MeshDescription.PolygonGroups().IsValid(PolygonGroupID))
		{
			const FName MaterialSlotName = PolygonGroupMaterialSlotNames[PolygonGroupID];
			const int32 StaticMeshMaterialIndex = StaticMesh ? StaticMesh->GetMaterialIndex(MaterialSlotName) : INDEX_NONE;
			return StaticMeshMaterialIndex != INDEX_NONE ? StaticMeshMaterialIndex : PolygonGroupID.GetValue();
		}

		return PolygonGroupID.GetValue();
	}

	/** 初始化柱体数据表行中的源资产、尺寸、统计信息和材质槽。 */
	void InitializeSampleRow(
		UStaticMesh* StaticMesh,
		const FEHBPillarMeshAnalysis& Analysis,
		const FBox& SourceBounds,
		const FEHBPillarMeshSamplingOptions& Options,
		FEHBPillarMeshData& OutRow)
	{
		OutRow = FEHBPillarMeshData();
		OutRow.SourceStaticMesh = StaticMesh;
		OutRow.SourceMeshName = StaticMesh ? StaticMesh->GetFName() : NAME_None;
		OutRow.LODIndex = Options.LODIndex;
		OutRow.SourceBoundsMin = SourceBounds.Min;
		OutRow.SourceBoundsMax = SourceBounds.Max;
		OutRow.Width = Analysis.BoundsSize.X;
		OutRow.Depth = Analysis.BoundsSize.Y;
		OutRow.Height = Analysis.BoundsSize.Z;
		OutRow.SourceTriangleCount = Analysis.SourceTriangleCount;
		OutRow.SourceVertexCount = Analysis.SourceVertexCount;
		OutRow.DisconnectedComponentCount = Analysis.DisconnectedComponentCount;
		FEHBMeshSampleValidation::InitializeMetadata(
			OutRow.TemplateMetadata,
			EEHBMeshSampleTemplateKind::Pillar,
			StaticMesh ? StaticMesh->GetFName() : NAME_None,
			StaticMesh,
			Analysis.SourceTriangleCount,
			Analysis.DisconnectedComponentCount);

		if (StaticMesh)
		{
			OutRow.Materials.Reserve(StaticMesh->GetStaticMaterials().Num());
			for (const FStaticMaterial& StaticMaterial : StaticMesh->GetStaticMaterials())
			{
				OutRow.Materials.Add(StaticMaterial.MaterialInterface);
			}
		}
	}
}

bool FEHBPillarMeshSampler::AnalyzeMesh(UStaticMesh* StaticMesh, const FEHBPillarMeshSamplingOptions& Options, FEHBPillarMeshAnalysis& OutAnalysis)
{
	OutAnalysis = FEHBPillarMeshAnalysis();

	const FMeshDescription* MeshDescription = nullptr;
	FText ErrorMessage;
	if (!EHBPillarMeshSampler::GetMeshDescription(StaticMesh, Options.LODIndex, MeshDescription, ErrorMessage))
	{
		OutAnalysis.Message = ErrorMessage;
		return false;
	}

	const int32 TriangleCount = MeshDescription->Triangles().Num();
	const int32 VertexCount = MeshDescription->Vertices().Num();
	if (TriangleCount <= 0 || VertexCount <= 0)
	{
		OutAnalysis.Message = LOCTEXT("EmptyMesh", "检测失败：静态网格体没有有效顶点或三角面。");
		return false;
	}

	OutAnalysis.SourceTriangleCount = TriangleCount;
	OutAnalysis.SourceVertexCount = VertexCount;

	if (TriangleCount > Options.MaxTriangleCount)
	{
		OutAnalysis.Message = FText::Format(
			LOCTEXT("TooManyTriangles", "检测失败：源三角面数量过多。当前 {0} 个，允许上限 {1} 个。可以简化模型或在高级设置中调高上限。"),
			FText::AsNumber(TriangleCount),
			FText::AsNumber(Options.MaxTriangleCount));
		return false;
	}

	const FBox Bounds = EHBPillarMeshSampler::CalculateBounds(*MeshDescription);
	if (!Bounds.IsValid)
	{
		OutAnalysis.Message = LOCTEXT("InvalidBounds", "检测失败：无法从静态网格体中计算有效包围盒。");
		return false;
	}

	OutAnalysis.BoundsSize = Bounds.GetSize();
	OutAnalysis.DisconnectedComponentCount = EHBPillarMeshSampler::CountDisconnectedComponents(*MeshDescription);
	OutAnalysis.bCanSample = true;
	OutAnalysis.Message = FText::Format(
		LOCTEXT("AnalysisSucceeded", "检测通过：宽 {0} cm，深 {1} cm，高 {2} cm，源顶点 {3} 个，源三角面 {4} 个，分离网格块 {5} 个。"),
		FText::AsNumber(OutAnalysis.BoundsSize.X),
		FText::AsNumber(OutAnalysis.BoundsSize.Y),
		FText::AsNumber(OutAnalysis.BoundsSize.Z),
		FText::AsNumber(VertexCount),
		FText::AsNumber(TriangleCount),
		FText::AsNumber(OutAnalysis.DisconnectedComponentCount));
	return true;
}

bool FEHBPillarMeshSampler::SampleToDataTable(
	UStaticMesh* StaticMesh,
	UDataTable* TargetDataTable,
	FName RowName,
	const FEHBPillarMeshSamplingOptions& Options,
	FEHBPillarMeshSamplingResult& OutResult,
	FText& OutError)
{
	OutResult = FEHBPillarMeshSamplingResult();
	OutError = FText::GetEmpty();

	if (!TargetDataTable)
	{
		OutError = LOCTEXT("NoDataTable", "采样失败：请先选择柱体数据表。");
		return false;
	}

	if (TargetDataTable->GetRowStruct() != FEHBPillarMeshData::StaticStruct())
	{
		OutError = LOCTEXT("WrongDataTableRowStruct", "采样失败：柱体数据表的行结构必须是“柱体网格体数据 / FEHBPillarMeshData”。");
		return false;
	}

	if (RowName.IsNone())
	{
		OutError = LOCTEXT("NoRowName", "采样失败：请填写名称。名称会直接作为柱体数据表行名。");
		return false;
	}

	FEHBPillarMeshAnalysis Analysis;
	if (!AnalyzeMesh(StaticMesh, Options, Analysis))
	{
		OutError = Analysis.Message;
		OutResult.Analysis = Analysis;
		return false;
	}

	if (!Options.bOverwriteExistingRows)
	{
		const bool bRowExists = TargetDataTable->FindRow<FEHBPillarMeshData>(RowName, TEXT("FEHBPillarMeshSampler::SampleToDataTable"), false) != nullptr;
		if (bRowExists)
		{
			OutError = LOCTEXT("RowAlreadyExists", "采样失败：目标数据表中已经存在同名柱体模板行。请改名，或在高级设置中允许覆盖已有行。");
			return false;
		}
	}

	const FMeshDescription* MeshDescription = nullptr;
	if (!EHBPillarMeshSampler::GetMeshDescription(StaticMesh, Options.LODIndex, MeshDescription, OutError))
	{
		return false;
	}

	const FBox SourceBounds = EHBPillarMeshSampler::CalculateBounds(*MeshDescription);
	FEHBPillarMeshData SampleRow;
	EHBPillarMeshSampler::InitializeSampleRow(StaticMesh, Analysis, SourceBounds, Options, SampleRow);
	SampleRow.TemplateMetadata.TemplateName = RowName;

	FStaticMeshConstAttributes Attributes(*MeshDescription);
	const TVertexAttributesConstRef<FVector3f> VertexPositions = Attributes.GetVertexPositions();
	const TVertexInstanceAttributesConstRef<FVector3f> VertexNormals = Attributes.GetVertexInstanceNormals();
	const TVertexInstanceAttributesConstRef<FVector3f> VertexTangents = Attributes.GetVertexInstanceTangents();
	const TVertexInstanceAttributesConstRef<FVector2f> VertexUVs = Attributes.GetVertexInstanceUVs();

	SampleRow.Vertices.Reserve(Analysis.SourceTriangleCount * 3);
	SampleRow.Triangles.Reserve(Analysis.SourceTriangleCount * 3);
	SampleRow.TriangleMaterialIndices.Reserve(Analysis.SourceTriangleCount);

	for (const FTriangleID TriangleID : MeshDescription->Triangles().GetElementIDs())
	{
		const TArrayView<const FVertexInstanceID> VertexInstanceIDs = MeshDescription->GetTriangleVertexInstances(TriangleID);
		if (VertexInstanceIDs.Num() != 3)
		{
			continue;
		}

		SampleRow.TriangleMaterialIndices.Add(EHBPillarMeshSampler::GetTriangleMaterialIndex(StaticMesh, *MeshDescription, Attributes, TriangleID));

		for (const FVertexInstanceID VertexInstanceID : VertexInstanceIDs)
		{
			const FVertexID VertexID = MeshDescription->GetVertexInstanceVertex(VertexInstanceID);

			FEHBPillarMeshSampleVertex SampleVertex;
			SampleVertex.Position = FVector(VertexPositions[VertexID]);
			SampleVertex.Normal = VertexNormals.IsValid() ? FVector(VertexNormals[VertexInstanceID]).GetSafeNormal() : FVector::UpVector;
			SampleVertex.Tangent = VertexTangents.IsValid() ? FVector(VertexTangents[VertexInstanceID]).GetSafeNormal() : FVector::ForwardVector;
			SampleVertex.UV0 = VertexUVs.IsValid() && VertexUVs.GetNumChannels() > 0 ? FVector2D(VertexUVs.Get(VertexInstanceID, 0)) : FVector2D::ZeroVector;

			const int32 NewVertexIndex = SampleRow.Vertices.Add(SampleVertex);
			SampleRow.Triangles.Add(NewVertexIndex);
		}
	}

	if (SampleRow.Triangles.Num() == 0)
	{
		OutError = LOCTEXT("NoSampledTriangles", "采样失败：没有得到有效三角面。请确认静态网格体源数据完整。");
		return false;
	}

	const FScopedTransaction Transaction(LOCTEXT("SamplePillarMeshTransaction", "采样柱体网格体"));
	TargetDataTable->Modify();
	TargetDataTable->AddRow(RowName, SampleRow);
	TargetDataTable->MarkPackageDirty();

	OutResult.Analysis = Analysis;
	OutResult.RowName = RowName;
	OutResult.TriangleCount = SampleRow.Triangles.Num() / 3;
	return true;
}

#undef LOCTEXT_NAMESPACE
