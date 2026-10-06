// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sampling/EHBWallMeshSampler.h"

#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "Sampling/EHBMeshSampleValidation.h"
#include "ScopedTransaction.h"
#include "StaticMeshAttributes.h"

#define LOCTEXT_NAMESPACE "EHBWallMeshSampler"

namespace EHBWallMeshSampler
{
	/** 按采样器记录的源轴向从向量中取出对应分量。 */
	float GetAxisValue(const FVector& Vector, EEHBWallMeshSampleAxis Axis)
	{
		switch (Axis)
		{
		case EEHBWallMeshSampleAxis::X:
			return Vector.X;
		case EEHBWallMeshSampleAxis::Y:
			return Vector.Y;
		case EEHBWallMeshSampleAxis::Z:
			return Vector.Z;
		default:
			return Vector.X;
		}
	}

	/** 将源网格体向量重排为统一墙体本地坐标，并减去指定轴向偏移。 */
	FVector MakeWallLocalVector(
		const FVector& SourceVector,
		EEHBWallMeshSampleAxis WidthAxis,
		EEHBWallMeshSampleAxis ThicknessAxis,
		EEHBWallMeshSampleAxis HeightAxis,
		float WidthOffset,
		float ThicknessOffset,
		float HeightOffset)
	{
		return FVector(
			GetAxisValue(SourceVector, WidthAxis) - WidthOffset,
			GetAxisValue(SourceVector, ThicknessAxis) - ThicknessOffset,
			GetAxisValue(SourceVector, HeightAxis) - HeightOffset);
	}

	/** 将源网格体顶点位置转换为墙体本地坐标：X 从宽度最小值开始，Y 以厚度中心为 0，Z 从底部开始。 */
	FVector MakeWallLocalPosition(
		const FVector& SourcePosition,
		EEHBWallMeshSampleAxis WidthAxis,
		EEHBWallMeshSampleAxis ThicknessAxis,
		const FBox& SourceBounds)
	{
		const float WidthMin = GetAxisValue(SourceBounds.Min, WidthAxis);
		const float HeightMin = SourceBounds.Min.Z;
		const float ThicknessMin = GetAxisValue(SourceBounds.Min, ThicknessAxis);
		const float ThicknessMax = GetAxisValue(SourceBounds.Max, ThicknessAxis);
		const float ThicknessCenter = (ThicknessMin + ThicknessMax) * 0.5f;

		return MakeWallLocalVector(
			SourcePosition,
			WidthAxis,
			ThicknessAxis,
			EEHBWallMeshSampleAxis::Z,
			WidthMin,
			ThicknessCenter,
			HeightMin);
	}

	/** 将采样轴枚举转换为 UE 坐标系中的单位方向向量。 */
	FVector GetAxisUnitVector(EEHBWallMeshSampleAxis Axis)
	{
		switch (Axis)
		{
		case EEHBWallMeshSampleAxis::X:
			return FVector::ForwardVector;
		case EEHBWallMeshSampleAxis::Y:
			return FVector::RightVector;
		case EEHBWallMeshSampleAxis::Z:
			return FVector::UpVector;
		default:
			return FVector::ForwardVector;
		}
	}

	/** 判断源轴向重排后是否改变了坐标系手性；改变时需要翻转三角形绕序。 */
	bool DoesAxisMappingFlipHandedness(
		EEHBWallMeshSampleAxis WidthAxis,
		EEHBWallMeshSampleAxis ThicknessAxis,
		EEHBWallMeshSampleAxis HeightAxis)
	{
		const FVector WidthSourceAxis = GetAxisUnitVector(WidthAxis);
		const FVector ThicknessSourceAxis = GetAxisUnitVector(ThicknessAxis);
		const FVector HeightSourceAxis = GetAxisUnitVector(HeightAxis);
		return FVector::DotProduct(FVector::CrossProduct(WidthSourceAxis, ThicknessSourceAxis), HeightSourceAxis) < 0.0f;
	}

	/** 在 XY 平面中选择较长轴作为墙宽、较短轴作为墙厚，Z 轴固定作为墙高。 */
	void DetectWallAxes(const FVector& BoundsSize, EEHBWallMeshSampleAxis& OutWidthAxis, EEHBWallMeshSampleAxis& OutThicknessAxis)
	{
		if (FMath::Abs(BoundsSize.X) >= FMath::Abs(BoundsSize.Y))
		{
			OutWidthAxis = EEHBWallMeshSampleAxis::X;
			OutThicknessAxis = EEHBWallMeshSampleAxis::Y;
		}
		else
		{
			OutWidthAxis = EEHBWallMeshSampleAxis::Y;
			OutThicknessAxis = EEHBWallMeshSampleAxis::X;
		}
	}

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

	/** 遍历源顶点计算本地包围盒，用于推导墙面宽度、厚度和高度。 */
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

	/** 通过三角面共享顶点关系统计分离网格块数量，用于过滤过碎或过复杂的墙面模板。 */
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

	/** 判断三角面是否属于墙体正反两个侧面；顶面、底面和左右端面会被排除。 */
	bool IsFrontOrBackSideTriangle(
		const FVector& WallPositionA,
		const FVector& WallPositionB,
		const FVector& WallPositionC,
		float MinSideFaceNormalAlignment)
	{
		const FVector FaceNormal = FVector::CrossProduct(WallPositionB - WallPositionA, WallPositionC - WallPositionA).GetSafeNormal();
		if (FaceNormal.IsNearlyZero())
		{
			return false;
		}

		const float AbsX = FMath::Abs(FaceNormal.X);
		const float AbsY = FMath::Abs(FaceNormal.Y);
		const float AbsZ = FMath::Abs(FaceNormal.Z);

		// 只保留主要朝向厚度轴 Y 的三角面。主要朝 Z 的顶/底面、主要朝 X 的左右端面都会被排除。
		return AbsY >= MinSideFaceNormalAlignment && AbsY >= AbsX && AbsY >= AbsZ;
	}

	/** 在正式采样前预估正面和反面可采样三角面数量，便于 UI 提前显示检测结果。 */
	bool IsTopOrBottomTriangle(
		const FVector& WallPositionA,
		const FVector& WallPositionB,
		const FVector& WallPositionC,
		float MinSideFaceNormalAlignment)
	{
		const FVector FaceNormal = FVector::CrossProduct(WallPositionB - WallPositionA, WallPositionC - WallPositionA).GetSafeNormal();
		if (FaceNormal.IsNearlyZero())
		{
			return false;
		}

		const float AbsX = FMath::Abs(FaceNormal.X);
		const float AbsY = FMath::Abs(FaceNormal.Y);
		const float AbsZ = FMath::Abs(FaceNormal.Z);
		return AbsZ >= MinSideFaceNormalAlignment && AbsZ >= AbsX && AbsZ >= AbsY;
	}

	bool IsTopOrBottomBoundaryTriangle(
		const FVector& WallPositionA,
		const FVector& WallPositionB,
		const FVector& WallPositionC,
		float WallHeight,
		float MinSideFaceNormalAlignment)
	{
		if (!IsTopOrBottomTriangle(WallPositionA, WallPositionB, WallPositionC, MinSideFaceNormalAlignment))
		{
			return false;
		}

		constexpr float BoundaryTolerance = 0.5f;
		const float CenterZ = (WallPositionA.Z + WallPositionB.Z + WallPositionC.Z) / 3.0f;
		return CenterZ <= BoundaryTolerance
			|| CenterZ >= FMath::Max(0.0f, WallHeight) - BoundaryTolerance;
	}

	void CountFrontBackSideTriangles(
		const FMeshDescription& MeshDescription,
		const FBox& SourceBounds,
		float WallHeight,
		EEHBWallMeshSampleAxis WidthAxis,
		EEHBWallMeshSampleAxis ThicknessAxis,
		float MinSideFaceNormalAlignment,
		int32& OutFrontTriangleCount,
		int32& OutBackTriangleCount)
	{
		OutFrontTriangleCount = 0;
		OutBackTriangleCount = 0;

		FStaticMeshConstAttributes Attributes(MeshDescription);
		const TVertexAttributesConstRef<FVector3f> VertexPositions = Attributes.GetVertexPositions();

		for (const FTriangleID TriangleID : MeshDescription.Triangles().GetElementIDs())
		{
			const TArrayView<const FVertexInstanceID> VertexInstanceIDs = MeshDescription.GetTriangleVertexInstances(TriangleID);
			if (VertexInstanceIDs.Num() != 3)
			{
				continue;
			}

			FVector WallPositions[3];
			for (int32 Index = 0; Index < 3; ++Index)
			{
				const FVertexID VertexID = MeshDescription.GetVertexInstanceVertex(VertexInstanceIDs[Index]);
				WallPositions[Index] = MakeWallLocalPosition(FVector(VertexPositions[VertexID]), WidthAxis, ThicknessAxis, SourceBounds);
			}

			if (IsTopOrBottomBoundaryTriangle(
				WallPositions[0],
				WallPositions[1],
				WallPositions[2],
				WallHeight,
				MinSideFaceNormalAlignment))
			{
				continue;
			}

			const float TriangleThicknessCenter = (WallPositions[0].Y + WallPositions[1].Y + WallPositions[2].Y) / 3.0f;
			if (TriangleThicknessCenter >= 0.0f)
			{
				OutFrontTriangleCount++;
			}
			else
			{
				OutBackTriangleCount++;
			}
		}
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

	/** 初始化数据表行中的源资产、尺寸、坐标轴、包围盒、统计信息和正反面默认侧别。 */
	void InitializeSampleRow(
		UStaticMesh* StaticMesh,
		const FEHBWallMeshAnalysis& Analysis,
		const FBox& SourceBounds,
		const FEHBWallMeshSamplingOptions& Options,
		bool bFlipWinding,
		FEHBWallMeshData& OutRow)
	{
		OutRow = FEHBWallMeshData();
		OutRow.SourceStaticMesh = StaticMesh;
		OutRow.SourceMeshName = StaticMesh ? StaticMesh->GetFName() : NAME_None;
		OutRow.LODIndex = Options.LODIndex;
		OutRow.WallWidth = Analysis.WallWidth;
		OutRow.WallThickness = Analysis.WallThickness;
		OutRow.WallHeight = Analysis.WallHeight;
		OutRow.SourceWidthAxis = Analysis.WidthAxis;
		OutRow.SourceThicknessAxis = Analysis.ThicknessAxis;
		OutRow.SourceHeightAxis = EEHBWallMeshSampleAxis::Z;
		OutRow.SourceBoundsMin = SourceBounds.Min;
		OutRow.SourceBoundsMax = SourceBounds.Max;
		OutRow.WallLocalBoundsMin = FVector(0.0f, -Analysis.WallThickness * 0.5f, 0.0f);
		OutRow.WallLocalBoundsMax = FVector(Analysis.WallWidth, Analysis.WallThickness * 0.5f, Analysis.WallHeight);
		OutRow.SourceTriangleCount = Analysis.SourceTriangleCount;
		OutRow.DisconnectedComponentCount = Analysis.DisconnectedComponentCount;
		OutRow.bTriangleWindingFlipped = bFlipWinding;

		if (StaticMesh)
		{
			OutRow.Materials.Reserve(StaticMesh->GetStaticMaterials().Num());
			for (const FStaticMaterial& StaticMaterial : StaticMesh->GetStaticMaterials())
			{
				OutRow.Materials.Add(StaticMaterial.MaterialInterface);
			}
		}

		OutRow.FrontSurface.SampleSide = EEHBWallMeshSampleSide::Front;
		OutRow.BackSurface.SampleSide = EEHBWallMeshSampleSide::Back;
	}

	/** 逐三角形交换后两个索引，用于修正轴向重排导致的反向绕序。 */
	void FlipTriangleWinding(FEHBWallMeshSampleSurface& Surface)
	{
		for (int32 TriangleIndex = 0; TriangleIndex + 2 < Surface.Triangles.Num(); TriangleIndex += 3)
		{
			Swap(Surface.Triangles[TriangleIndex + 1], Surface.Triangles[TriangleIndex + 2]);
		}
	}
}

bool FEHBWallMeshSampler::AnalyzeMesh(UStaticMesh* StaticMesh, const FEHBWallMeshSamplingOptions& Options, FEHBWallMeshAnalysis& OutAnalysis)
{
	OutAnalysis = FEHBWallMeshAnalysis();

	const FMeshDescription* MeshDescription = nullptr;
	FText ErrorMessage;
	if (!EHBWallMeshSampler::GetMeshDescription(StaticMesh, Options.LODIndex, MeshDescription, ErrorMessage))
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

	if (TriangleCount > Options.MaxTriangleCount)
	{
		OutAnalysis.SourceTriangleCount = TriangleCount;
		OutAnalysis.SourceVertexCount = VertexCount;
		OutAnalysis.Message = FText::Format(
			LOCTEXT("TooManyTriangles", "检测失败：源三角面数量过多。当前 {0} 个，允许上限 {1} 个。可以简化模型或在高级设置中调高上限。"),
			FText::AsNumber(TriangleCount),
			FText::AsNumber(Options.MaxTriangleCount));
		return false;
	}

	const FBox Bounds = EHBWallMeshSampler::CalculateBounds(*MeshDescription);
	if (!Bounds.IsValid)
	{
		OutAnalysis.Message = LOCTEXT("InvalidBounds", "检测失败：无法从静态网格体中计算有效包围盒。");
		return false;
	}

	const FVector BoundsSize = Bounds.GetSize();
	EEHBWallMeshSampleAxis WidthAxis = EEHBWallMeshSampleAxis::X;
	EEHBWallMeshSampleAxis ThicknessAxis = EEHBWallMeshSampleAxis::Y;
	EHBWallMeshSampler::DetectWallAxes(BoundsSize, WidthAxis, ThicknessAxis);

	const float WallWidth = FMath::Abs(EHBWallMeshSampler::GetAxisValue(BoundsSize, WidthAxis));
	const float WallThickness = FMath::Abs(EHBWallMeshSampler::GetAxisValue(BoundsSize, ThicknessAxis));
	const float WallHeight = FMath::Abs(BoundsSize.Z);
	const int32 ComponentCount = EHBWallMeshSampler::CountDisconnectedComponents(*MeshDescription);

	OutAnalysis.WallWidth = WallWidth;
	OutAnalysis.WallThickness = WallThickness;
	OutAnalysis.WallHeight = WallHeight;
	OutAnalysis.SourceTriangleCount = TriangleCount;
	OutAnalysis.SourceVertexCount = VertexCount;
	OutAnalysis.DisconnectedComponentCount = ComponentCount;
	OutAnalysis.WidthAxis = WidthAxis;
	OutAnalysis.ThicknessAxis = ThicknessAxis;

	EHBWallMeshSampler::CountFrontBackSideTriangles(
		*MeshDescription,
		Bounds,
		WallHeight,
		WidthAxis,
		ThicknessAxis,
		Options.MinSideFaceNormalAlignment,
		OutAnalysis.EstimatedFrontSideTriangleCount,
		OutAnalysis.EstimatedBackSideTriangleCount);

	if (WallWidth < Options.MinWallWidth)
	{
		OutAnalysis.Message = FText::Format(
			LOCTEXT("WidthTooSmall", "检测失败：墙面宽度太小。当前 {0} cm，最小要求 {1} cm。"),
			FText::AsNumber(WallWidth),
			FText::AsNumber(Options.MinWallWidth));
		return false;
	}

	if (WallHeight < Options.MinWallHeight)
	{
		OutAnalysis.Message = FText::Format(
			LOCTEXT("HeightTooSmall", "检测失败：墙面高度太小。当前 {0} cm，最小要求 {1} cm。"),
			FText::AsNumber(WallHeight),
			FText::AsNumber(Options.MinWallHeight));
		return false;
	}

	if (WallThickness < Options.MinWallThickness)
	{
		OutAnalysis.Message = FText::Format(
			LOCTEXT("ThicknessTooSmall", "检测失败：墙面厚度太小。当前 {0} cm，最小要求 {1} cm。需要有可拆分的厚度，才能分别保存正面和反面。"),
			FText::AsNumber(WallThickness),
			FText::AsNumber(Options.MinWallThickness));
		return false;
	}

	const float ThicknessToWidthRatio = WallWidth > KINDA_SMALL_NUMBER ? WallThickness / WallWidth : 1.0f;
	if (ThicknessToWidthRatio > Options.MaxThicknessToWidthRatio)
	{
		OutAnalysis.Message = FText::Format(
			LOCTEXT("NotThinWall", "检测失败：模型不像薄墙。当前厚宽比 {0}，允许上限 {1}。请确认模型整体轮廓是较薄的矩形墙面。"),
			FText::AsNumber(ThicknessToWidthRatio),
			FText::AsNumber(Options.MaxThicknessToWidthRatio));
		return false;
	}

	if (ComponentCount > Options.MaxDisconnectedComponentCount)
	{
		OutAnalysis.Message = FText::Format(
			LOCTEXT("TooManyComponents", "检测失败：模型中分离网格块过多。当前 {0} 个，允许上限 {1} 个。请尽量使用单段墙面或适当简化装饰碎件。"),
			FText::AsNumber(ComponentCount),
			FText::AsNumber(Options.MaxDisconnectedComponentCount));
		return false;
	}

	if (OutAnalysis.EstimatedFrontSideTriangleCount == 0 || OutAnalysis.EstimatedBackSideTriangleCount == 0)
	{
		OutAnalysis.Message = FText::Format(
			LOCTEXT("NoFrontBackSideFaces", "检测失败：没有同时识别到可采样的正面和反面侧面。当前正面侧面三角面 {0} 个，反面侧面三角面 {1} 个。请确认模型有前后两个主要墙面，或在高级设置中降低“侧面法线阈值”。"),
			FText::AsNumber(OutAnalysis.EstimatedFrontSideTriangleCount),
			FText::AsNumber(OutAnalysis.EstimatedBackSideTriangleCount));
		return false;
	}

	OutAnalysis.bCanSample = true;
	OutAnalysis.Message = FText::Format(
		LOCTEXT("AnalysisSucceeded", "检测通过：宽 {0} cm，厚 {1} cm，高 {2} cm，源三角面 {3} 个，正面侧面 {4} 个，反面侧面 {5} 个，分离网格块 {6} 个。"),
		FText::AsNumber(WallWidth),
		FText::AsNumber(WallThickness),
		FText::AsNumber(WallHeight),
		FText::AsNumber(TriangleCount),
		FText::AsNumber(OutAnalysis.EstimatedFrontSideTriangleCount),
		FText::AsNumber(OutAnalysis.EstimatedBackSideTriangleCount),
		FText::AsNumber(ComponentCount));
	return true;
}

bool FEHBWallMeshSampler::SampleToDataTable(
	UStaticMesh* StaticMesh,
	UDataTable* TargetDataTable,
	FName BaseRowName,
	const FEHBWallMeshSamplingOptions& Options,
	FEHBWallMeshSamplingResult& OutResult,
	FText& OutError)
{
	OutResult = FEHBWallMeshSamplingResult();
	OutError = FText::GetEmpty();

	if (!TargetDataTable)
	{
		OutError = LOCTEXT("NoDataTable", "采样失败：请先选择墙面数据表。");
		return false;
	}

	if (TargetDataTable->GetRowStruct() != FEHBWallMeshData::StaticStruct())
	{
		OutError = LOCTEXT("WrongDataTableRowStruct", "采样失败：墙面数据表的行结构必须是“墙面网格体数据 / FEHBWallMeshData”。");
		return false;
	}

	if (BaseRowName.IsNone())
	{
		OutError = LOCTEXT("NoBaseRowName", "采样失败：请填写名称。名称会直接作为数据表行名，正面和反面会保存在这一行内部。");
		return false;
	}

	FEHBWallMeshAnalysis Analysis;
	if (!AnalyzeMesh(StaticMesh, Options, Analysis))
	{
		OutError = Analysis.Message;
		OutResult.Analysis = Analysis;
		return false;
	}

	if (!Options.bOverwriteExistingRows)
	{
		const bool bRowExists = TargetDataTable->FindRow<FEHBWallMeshData>(BaseRowName, TEXT("FEHBWallMeshSampler::SampleToDataTable"), false) != nullptr;
		if (bRowExists)
		{
			OutError = LOCTEXT("RowAlreadyExists", "采样失败：目标数据表中已经存在同名墙面模板行。请改名，或在高级设置中允许覆盖已有行。");
			return false;
		}
	}

	const FMeshDescription* MeshDescription = nullptr;
	if (!EHBWallMeshSampler::GetMeshDescription(StaticMesh, Options.LODIndex, MeshDescription, OutError))
	{
		return false;
	}

	const FBox SourceBounds = EHBWallMeshSampler::CalculateBounds(*MeshDescription);
	const bool bFlipWinding = EHBWallMeshSampler::DoesAxisMappingFlipHandedness(Analysis.WidthAxis, Analysis.ThicknessAxis, EEHBWallMeshSampleAxis::Z);

	FEHBWallMeshData SampleRow;
	EHBWallMeshSampler::InitializeSampleRow(StaticMesh, Analysis, SourceBounds, Options, bFlipWinding, SampleRow);

	FStaticMeshConstAttributes Attributes(*MeshDescription);
	const TVertexAttributesConstRef<FVector3f> VertexPositions = Attributes.GetVertexPositions();
	const TVertexInstanceAttributesConstRef<FVector3f> VertexNormals = Attributes.GetVertexInstanceNormals();
	const TVertexInstanceAttributesConstRef<FVector3f> VertexTangents = Attributes.GetVertexInstanceTangents();
	const TVertexInstanceAttributesConstRef<FVector2f> VertexUVs = Attributes.GetVertexInstanceUVs();

	for (const FTriangleID TriangleID : MeshDescription->Triangles().GetElementIDs())
	{
		const TArrayView<const FVertexInstanceID> VertexInstanceIDs = MeshDescription->GetTriangleVertexInstances(TriangleID);
		if (VertexInstanceIDs.Num() != 3)
		{
			continue;
		}

		FVector WallPositions[3];
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const FVertexID VertexID = MeshDescription->GetVertexInstanceVertex(VertexInstanceIDs[Index]);
			WallPositions[Index] = EHBWallMeshSampler::MakeWallLocalPosition(
				FVector(VertexPositions[VertexID]),
				Analysis.WidthAxis,
				Analysis.ThicknessAxis,
				SourceBounds);
		}

		if (EHBWallMeshSampler::IsTopOrBottomBoundaryTriangle(
			WallPositions[0],
			WallPositions[1],
			WallPositions[2],
			Analysis.WallHeight,
			Options.MinSideFaceNormalAlignment))
		{
			continue;
		}

		float TriangleThicknessCenter = 0.0f;
		for (const FVector& WallPosition : WallPositions)
		{
			TriangleThicknessCenter += WallPosition.Y;
		}
		TriangleThicknessCenter /= 3.0f;

		float TriangleThicknessMin = TNumericLimits<float>::Max();
		float TriangleThicknessMax = TNumericLimits<float>::Lowest();
		for (const FVector& WallPosition : WallPositions)
		{
			TriangleThicknessMin = FMath::Min(TriangleThicknessMin, WallPosition.Y);
			TriangleThicknessMax = FMath::Max(TriangleThicknessMax, WallPosition.Y);
		}

		FEHBWallMeshSampleVertex SampleVertices[3];
		int32 SampleVertexIndex = 0;

		for (const FVertexInstanceID VertexInstanceID : VertexInstanceIDs)
		{
			const FVertexID VertexID = MeshDescription->GetVertexInstanceVertex(VertexInstanceID);
			const FVector SourcePosition(VertexPositions[VertexID]);
			const FVector SourceNormal(VertexNormals.IsValid() ? FVector(VertexNormals[VertexInstanceID]) : FVector::UpVector);
			const FVector SourceTangent(VertexTangents.IsValid() ? FVector(VertexTangents[VertexInstanceID]) : FVector::ForwardVector);
			const FVector2D SourceUV0(VertexUVs.IsValid() && VertexUVs.GetNumChannels() > 0 ? FVector2D(VertexUVs.Get(VertexInstanceID, 0)) : FVector2D::ZeroVector);

			FEHBWallMeshSampleVertex SampleVertex;
			SampleVertex.Position = EHBWallMeshSampler::MakeWallLocalPosition(
				SourcePosition,
				Analysis.WidthAxis,
				Analysis.ThicknessAxis,
				SourceBounds);
			SampleVertex.Normal = EHBWallMeshSampler::MakeWallLocalVector(
				SourceNormal,
				Analysis.WidthAxis,
				Analysis.ThicknessAxis,
				EEHBWallMeshSampleAxis::Z,
				0.0f,
				0.0f,
				0.0f).GetSafeNormal();
			SampleVertex.Tangent = EHBWallMeshSampler::MakeWallLocalVector(
				SourceTangent,
				Analysis.WidthAxis,
				Analysis.ThicknessAxis,
				EEHBWallMeshSampleAxis::Z,
				0.0f,
				0.0f,
				0.0f).GetSafeNormal();
			SampleVertex.UV0 = SourceUV0;

			SampleVertices[SampleVertexIndex++] = SampleVertex;
		}

		const int32 TriangleMaterialIndex =
			EHBWallMeshSampler::GetTriangleMaterialIndex(StaticMesh, *MeshDescription, Attributes, TriangleID);
		auto AddTriangleToSurface = [&SampleVertices, TriangleMaterialIndex](FEHBWallMeshSampleSurface& TargetSurface)
		{
			TargetSurface.TriangleMaterialIndices.Add(TriangleMaterialIndex);
			for (const FEHBWallMeshSampleVertex& SampleVertex : SampleVertices)
			{
				const int32 NewVertexIndex = TargetSurface.Vertices.Add(SampleVertex);
				TargetSurface.Triangles.Add(NewVertexIndex);
			}
		};

		if (TriangleThicknessMin <= 0.0f && TriangleThicknessMax >= 0.0f)
		{
			AddTriangleToSurface(SampleRow.FrontSurface);
			AddTriangleToSurface(SampleRow.BackSurface);
		}
		else if (TriangleThicknessCenter >= 0.0f)
		{
			AddTriangleToSurface(SampleRow.FrontSurface);
		}
		else
		{
			AddTriangleToSurface(SampleRow.BackSurface);
		}
	}

	if (bFlipWinding)
	{
		EHBWallMeshSampler::FlipTriangleWinding(SampleRow.FrontSurface);
		EHBWallMeshSampler::FlipTriangleWinding(SampleRow.BackSurface);
	}

	SampleRow.FrontSurface.SampleTriangleCount = SampleRow.FrontSurface.Triangles.Num() / 3;
	SampleRow.BackSurface.SampleTriangleCount = SampleRow.BackSurface.Triangles.Num() / 3;
	FEHBMeshSampleValidation::InitializeMetadata(
		SampleRow.TemplateMetadata,
		EEHBMeshSampleTemplateKind::WallSurface,
		BaseRowName,
		StaticMesh,
		Analysis.SourceTriangleCount,
		Analysis.DisconnectedComponentCount);

	if (SampleRow.FrontSurface.SampleTriangleCount == 0 || SampleRow.BackSurface.SampleTriangleCount == 0)
	{
		OutError = LOCTEXT("SplitProducedEmptySide", "采样失败：沿厚度中心拆分后，正面或反面没有得到有效三角面。请确认模型有真实厚度，并且墙体两侧都有可采样几何。");
		return false;
	}

	const FScopedTransaction Transaction(LOCTEXT("SampleWallMeshTransaction", "采样墙面网格体"));
	TargetDataTable->Modify();
	TargetDataTable->AddRow(BaseRowName, SampleRow);
	TargetDataTable->MarkPackageDirty();

	OutResult.Analysis = Analysis;
	OutResult.RowName = BaseRowName;
	OutResult.FrontTriangleCount = SampleRow.FrontSurface.SampleTriangleCount;
	OutResult.BackTriangleCount = SampleRow.BackSurface.SampleTriangleCount;
	return true;
}

#undef LOCTEXT_NAMESPACE
