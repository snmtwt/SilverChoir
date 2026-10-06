// Copyright Epic Games, Inc. All Rights Reserved.

#include "Diagnostics/EHBBuildingPerformanceAnalyzer.h"

#include "Actors/EHBElementActorBase.h"
#include "Actors/EHB_DoorWindow.h"
#include "Actors/EHB_Pillar.h"
#include "Actors/EHB_Railing.h"
#include "Actors/EHB_Wall.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/EHBBuildingActorBase.h"
#include "Engine/DataTable.h"
#include "Sampling/EHBPillarMeshData.h"
#include "Sampling/EHBWallMeshData.h"

namespace
{
	int32 GetPerformanceClassRank(EEHBMeshSamplePerformanceClass PerformanceClass)
	{
		switch (PerformanceClass)
		{
		case EEHBMeshSamplePerformanceClass::Light:
			return 1;
		case EEHBMeshSamplePerformanceClass::Moderate:
			return 2;
		case EEHBMeshSamplePerformanceClass::Heavy:
			return 3;
		case EEHBMeshSamplePerformanceClass::Critical:
			return 4;
		default:
			return 0;
		}
	}

	FString GetActorDisplayName(const AActor* Actor)
	{
		return Actor ? Actor->GetName() : FString();
	}

	void AccumulateMeshStats(FEHBGeneratedMeshStats& Target, const FEHBGeneratedMeshStats& Source)
	{
		Target.SectionCount += Source.SectionCount;
		Target.VisibleSectionCount += Source.VisibleSectionCount;
		Target.VertexCount += Source.VertexCount;
		Target.TriangleCount += Source.TriangleCount;
		Target.CollisionSectionCount += Source.CollisionSectionCount;
		Target.CollisionVertexCount += Source.CollisionVertexCount;
		Target.CollisionTriangleCount += Source.CollisionTriangleCount;
		Target.EstimatedVertexBufferBytes += Source.EstimatedVertexBufferBytes;
		Target.EstimatedIndexBufferBytes += Source.EstimatedIndexBufferBytes;
		Target.SubmittedSectionUpdateCount += Source.SubmittedSectionUpdateCount;
		Target.SkippedIdenticalSectionUpdateCount += Source.SkippedIdenticalSectionUpdateCount;
		Target.MeshRevision = FMath::Max(Target.MeshRevision, Source.MeshRevision);
	}

	void AddIssue(
		FEHBBuildingPerformanceReport& Report,
		EEHBPerformanceBudgetSeverity Severity,
		const FString& Message,
		const FString& ActorName = FString(),
		const FGuid& ElementGuid = FGuid())
	{
		FEHBBuildingPerformanceIssue& Issue = Report.Issues.AddDefaulted_GetRef();
		Issue.Severity = Severity;
		Issue.Message = Message;
		Issue.ActorName = ActorName;
		Issue.ElementGuid = ElementGuid;

		if (Severity != EEHBPerformanceBudgetSeverity::Info)
		{
			Report.bWithinBudget = false;
		}
	}

	void AddBudgetIssueIfExceeded(
		FEHBBuildingPerformanceReport& Report,
		const TCHAR* Label,
		int64 Value,
		int64 Limit)
	{
		if (Limit <= 0 || Value <= Limit)
		{
			return;
		}

		const EEHBPerformanceBudgetSeverity Severity = Value > Limit * 2
			? EEHBPerformanceBudgetSeverity::Critical
			: EEHBPerformanceBudgetSeverity::Warning;
		AddIssue(
			Report,
			Severity,
			FString::Printf(TEXT("%s 超出预算：%lld / %lld。"), Label, Value, Limit));
	}

	void AddElementBudgetIssueIfExceeded(
		FEHBBuildingPerformanceReport& Report,
		const FEHBElementPerformanceEntry& Entry,
		const TCHAR* Label,
		int64 Value,
		int64 Limit)
	{
		if (Limit <= 0 || Value <= Limit)
		{
			return;
		}

		const EEHBPerformanceBudgetSeverity Severity = Value > Limit * 2
			? EEHBPerformanceBudgetSeverity::Critical
			: EEHBPerformanceBudgetSeverity::Warning;
		AddIssue(
			Report,
			Severity,
			FString::Printf(TEXT("%s 超出单元素预算：%lld / %lld。"), Label, Value, Limit),
			Entry.ActorName,
			Entry.ElementGuid);
	}

	void AddSampleMetadata(
		FEHBElementPerformanceEntry& Entry,
		const FEHBMeshSampleTemplateMetadata& Metadata)
	{
		++Entry.SampledTemplateCount;
		Entry.SampleEstimatedCost += FMath::Max(0, Metadata.EstimatedApplyCost);
		if (GetPerformanceClassRank(Metadata.PerformanceClass) > GetPerformanceClassRank(Entry.HighestSamplePerformanceClass))
		{
			Entry.HighestSamplePerformanceClass = Metadata.PerformanceClass;
		}
	}

	void AddWallSurfaceSample(
		FEHBElementPerformanceEntry& Entry,
		const FEHBWallSurfaceStyle& SurfaceStyle)
	{
		if (SurfaceStyle.SourceType != EEHBWallSurfaceSourceType::SampledMesh)
		{
			return;
		}

		const UDataTable* Table = SurfaceStyle.SampledWallRow.DataTable;
		if (!Table || SurfaceStyle.SampledWallRow.RowName.IsNone() || Table->GetRowStruct() != FEHBWallMeshData::StaticStruct())
		{
			++Entry.SampledTemplateCount;
			Entry.HighestSamplePerformanceClass = EEHBMeshSamplePerformanceClass::Unknown;
			return;
		}

		const FEHBWallMeshData* Row = Table->FindRow<FEHBWallMeshData>(
			SurfaceStyle.SampledWallRow.RowName,
			TEXT("EHBBuildingPerformanceAnalyzer.WallSurface"),
			false);
		if (Row)
		{
			AddSampleMetadata(Entry, Row->TemplateMetadata);
		}
		else
		{
			++Entry.SampledTemplateCount;
		}
	}

	void AddRowSampleMetadata(
		FEHBElementPerformanceEntry& Entry,
		const FDataTableRowHandle& RowHandle,
		const UScriptStruct* ExpectedStruct)
	{
		if (!RowHandle.DataTable || RowHandle.RowName.IsNone() || RowHandle.DataTable->GetRowStruct() != ExpectedStruct)
		{
			return;
		}

		if (ExpectedStruct == FEHBPillarMeshData::StaticStruct())
		{
			const FEHBPillarMeshData* Row = RowHandle.DataTable->FindRow<FEHBPillarMeshData>(
				RowHandle.RowName,
				TEXT("EHBBuildingPerformanceAnalyzer.Pillar"),
				false);
			if (Row)
			{
				AddSampleMetadata(Entry, Row->TemplateMetadata);
			}
		}
	}

	void CollectSampleMetadata(AActor* Actor, FEHBElementPerformanceEntry& Entry)
	{
		if (const AEHB_Wall* Wall = Cast<AEHB_Wall>(Actor))
		{
			AddWallSurfaceSample(Entry, Wall->LeftSurfaceStyle);
			AddWallSurfaceSample(Entry, Wall->RightSurfaceStyle);
		}
		else if (const AEHB_Pillar* Pillar = Cast<AEHB_Pillar>(Actor))
		{
			AddRowSampleMetadata(Entry, Pillar->SampledPillarRow, FEHBPillarMeshData::StaticStruct());
		}
	}

	void AccumulateEntryIntoReport(
		FEHBBuildingPerformanceReport& Report,
		const FEHBElementPerformanceEntry& Entry)
	{
		Report.GeneratedMeshComponentCount += Entry.GeneratedMeshComponentCount;
		AccumulateMeshStats(Report.GeneratedMeshStats, Entry.GeneratedMeshStats);
		Report.StaticMeshComponentCount += Entry.StaticMeshComponentCount;
		Report.StaticMeshWithAssetCount += Entry.StaticMeshWithAssetCount;
		Report.InstancedMeshComponentCount += Entry.InstancedMeshComponentCount;
		Report.HierarchicalInstancedMeshComponentCount += Entry.HierarchicalInstancedMeshComponentCount;
		Report.InstancedMeshInstanceCount += Entry.InstancedMeshInstanceCount;
		Report.SampledTemplateCount += Entry.SampledTemplateCount;
		Report.SampleEstimatedCost += Entry.SampleEstimatedCost;

		switch (Entry.HighestSamplePerformanceClass)
		{
		case EEHBMeshSamplePerformanceClass::Moderate:
			++Report.ModerateSampleTemplateCount;
			break;
		case EEHBMeshSamplePerformanceClass::Heavy:
			++Report.HeavySampleTemplateCount;
			break;
		case EEHBMeshSamplePerformanceClass::Critical:
			++Report.CriticalSampleTemplateCount;
			break;
		default:
			break;
		}
	}
}

FEHBElementPerformanceEntry UEHBBuildingPerformanceAnalyzer::AnalyzeActor(AActor* Actor)
{
	FEHBElementPerformanceEntry Entry;
	if (!Actor)
	{
		return Entry;
	}

	Entry.ActorName = GetActorDisplayName(Actor);

	if (const AEHBElementActorBase* ElementActor = Cast<AEHBElementActorBase>(Actor))
	{
		Entry.ElementGuid = ElementActor->ElementGuid;
		Entry.ElementType = ElementActor->ElementType;
		Entry.FloorIndex = ElementActor->FloorIndex;
	}

	TArray<UEHBGeneratedMeshComponent*> GeneratedMeshComponents;
	Actor->GetComponents<UEHBGeneratedMeshComponent>(GeneratedMeshComponents);
	Entry.GeneratedMeshComponentCount = GeneratedMeshComponents.Num();
	for (const UEHBGeneratedMeshComponent* Component : GeneratedMeshComponents)
	{
		if (Component)
		{
			AccumulateMeshStats(Entry.GeneratedMeshStats, Component->GetMeshStats());
		}
	}

	TArray<UStaticMeshComponent*> StaticMeshComponents;
	Actor->GetComponents<UStaticMeshComponent>(StaticMeshComponents);
	Entry.StaticMeshComponentCount = StaticMeshComponents.Num();
	for (const UStaticMeshComponent* Component : StaticMeshComponents)
	{
		if (Component && Component->GetStaticMesh())
		{
			++Entry.StaticMeshWithAssetCount;
		}
	}

	TArray<UInstancedStaticMeshComponent*> InstancedMeshComponents;
	Actor->GetComponents<UInstancedStaticMeshComponent>(InstancedMeshComponents);
	Entry.InstancedMeshComponentCount = InstancedMeshComponents.Num();
	for (const UInstancedStaticMeshComponent* Component : InstancedMeshComponents)
	{
		if (Component)
		{
			Entry.InstancedMeshInstanceCount += Component->GetInstanceCount();
		}
	}

	TArray<UHierarchicalInstancedStaticMeshComponent*> HismComponents;
	Actor->GetComponents<UHierarchicalInstancedStaticMeshComponent>(HismComponents);
	Entry.HierarchicalInstancedMeshComponentCount = HismComponents.Num();

	CollectSampleMetadata(Actor, Entry);

	return Entry;
}

FEHBBuildingPerformanceReport UEHBBuildingPerformanceAnalyzer::AnalyzeBuilding(
	AEHBBuildingActorBase* Building,
	const FEHBBuildingPerformanceBudget& Budget)
{
	FEHBBuildingPerformanceReport Report;
	if (!Building)
	{
		Report.bValid = false;
		AddIssue(Report, EEHBPerformanceBudgetSeverity::Critical, TEXT("建筑为空。"));
		return Report;
	}

	Report.bValid = true;
	Report.BuildingName = GetActorDisplayName(Building);

	TArray<AActor*> ActorsToInspect;
	ActorsToInspect.Add(Building);
	TArray<AActor*> AttachedActors;
	Building->GetAttachedActors(AttachedActors, true, true);
	for (AActor* AttachedActor : AttachedActors)
	{
		if (AttachedActor)
		{
			ActorsToInspect.Add(AttachedActor);
		}
	}
	Report.ActorCount = ActorsToInspect.Num();

	FEHBElementQuery ElementQuery;
	Report.ElementCount = Building->QueryElements(ElementQuery).Num();

	FEHBRelationQuery RelationQuery;
	RelationQuery.bIncludeDisabled = true;
	RelationQuery.bIncludeStale = true;
	const TArray<FEHBElementRelation> Relations = Building->QueryElementRelations(RelationQuery);
	Report.RelationCount = Relations.Num();
	for (const FEHBElementRelation& Relation : Relations)
	{
		if (Building->IsElementRelationStale(Relation.RelationGuid))
		{
			++Report.StaleRelationCount;
		}
		if (!Relation.bEnabled)
		{
			++Report.DisabledRelationCount;
		}
	}

	for (AActor* Actor : ActorsToInspect)
	{
		if (!Actor)
		{
			continue;
		}

		FEHBElementPerformanceEntry Entry = AnalyzeActor(Actor);
		AccumulateEntryIntoReport(Report, Entry);

		if (const AEHB_Railing* Railing = Cast<AEHB_Railing>(Actor))
		{
			Report.RailingPostCount += Railing->GeneratedPosts.Num();
			Report.RailingGateCount += Railing->GateConnections.Num();
		}

		if (Cast<AEHBElementActorBase>(Actor))
		{
			AddElementBudgetIssueIfExceeded(
				Report,
				Entry,
				TEXT("生成网格三角面数"),
				Entry.GeneratedMeshStats.TriangleCount,
				Budget.MaxTrianglesPerElement);
			Report.ElementEntries.Add(MoveTemp(Entry));
		}
	}

	Report.ElementEntries.Sort([](const FEHBElementPerformanceEntry& A, const FEHBElementPerformanceEntry& B)
	{
		return A.GeneratedMeshStats.TriangleCount > B.GeneratedMeshStats.TriangleCount;
	});

	AddBudgetIssueIfExceeded(Report, TEXT("生成网格三角面数"), Report.GeneratedMeshStats.TriangleCount, Budget.MaxGeneratedMeshTriangles);
	AddBudgetIssueIfExceeded(Report, TEXT("生成网格顶点数"), Report.GeneratedMeshStats.VertexCount, Budget.MaxGeneratedMeshVertices);
	AddBudgetIssueIfExceeded(Report, TEXT("碰撞三角面数"), Report.GeneratedMeshStats.CollisionTriangleCount, Budget.MaxCollisionTriangles);
	AddBudgetIssueIfExceeded(Report, TEXT("生成网格组件数"), Report.GeneratedMeshComponentCount, Budget.MaxGeneratedMeshComponents);
	AddBudgetIssueIfExceeded(Report, TEXT("静态网格组件数"), Report.StaticMeshComponentCount, Budget.MaxStaticMeshComponents);
	AddBudgetIssueIfExceeded(Report, TEXT("实例化网格实例数"), Report.InstancedMeshInstanceCount + Report.RoofTileInstanceCount, Budget.MaxInstancedMeshInstances);
	AddBudgetIssueIfExceeded(Report, TEXT("采样估算开销"), Report.SampleEstimatedCost, Budget.MaxSampleEstimatedCost);

	if (Report.StaleRelationCount > Budget.MaxStaleRelations)
	{
		AddIssue(
			Report,
			EEHBPerformanceBudgetSeverity::Warning,
			FString::Printf(TEXT("失效关系数量超出预算：%d / %d。"), Report.StaleRelationCount, Budget.MaxStaleRelations));
	}
	if (Report.CriticalSampleTemplateCount > 0)
	{
		AddIssue(
			Report,
			EEHBPerformanceBudgetSeverity::Critical,
			FString::Printf(TEXT("存在严重性能等级的采样模板：%d。"), Report.CriticalSampleTemplateCount));
	}
	else if (Report.HeavySampleTemplateCount > 0)
	{
		AddIssue(
			Report,
			EEHBPerformanceBudgetSeverity::Warning,
			FString::Printf(TEXT("存在较重性能等级的采样模板：%d。"), Report.HeavySampleTemplateCount));
	}

	return Report;
}

FString UEHBBuildingPerformanceAnalyzer::FormatPerformanceReportSummary(const FEHBBuildingPerformanceReport& Report)
{
	if (!Report.bValid)
	{
		return TEXT("性能：报告无效");
	}

	const double BufferKB = static_cast<double>(
		Report.GeneratedMeshStats.EstimatedVertexBufferBytes + Report.GeneratedMeshStats.EstimatedIndexBufferBytes) / 1024.0;

	return FString::Printf(
		TEXT("性能：元素 %d，Actor %d，生成组件 %d，分段 %d，三角面 %d，碰撞三角面 %d，静态组件 %d，实例 %d，采样模板 %d，估算开销 %d，缓冲区 %.1f KB，问题 %d"),
		Report.ElementCount,
		Report.ActorCount,
		Report.GeneratedMeshComponentCount,
		Report.GeneratedMeshStats.SectionCount,
		Report.GeneratedMeshStats.TriangleCount,
		Report.GeneratedMeshStats.CollisionTriangleCount,
		Report.StaticMeshComponentCount,
		Report.InstancedMeshInstanceCount + Report.RoofTileInstanceCount,
		Report.SampledTemplateCount,
		Report.SampleEstimatedCost,
		BufferKB,
		Report.Issues.Num());
}

bool UEHBBuildingPerformanceAnalyzer::HasBlockingPerformanceIssues(const FEHBBuildingPerformanceReport& Report)
{
	return Report.Issues.ContainsByPredicate([](const FEHBBuildingPerformanceIssue& Issue)
	{
		return Issue.Severity == EEHBPerformanceBudgetSeverity::Critical;
	});
}
