// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cutting/EHBGeneratedMeshCollector.h"

#include "Actors/EHBElementActorBase.h"
#include "Components/EHBGeneratedMeshComponent.h"

bool FEHBGeneratedMeshCollector::CollectFromActor(
	const AActor* SourceActor,
	const FTransform& TargetLocalToWorld,
	FEHBMeshAggregateData& OutData)
{
	OutData.Reset();
	if (!SourceActor)
	{
		return false;
	}

	TArray<UEHBGeneratedMeshComponent*> Components;
	SourceActor->GetComponents(Components);

	const AEHBElementActorBase* SourceElement = Cast<AEHBElementActorBase>(SourceActor);
	return CollectFromComponents(
		Components,
		TargetLocalToWorld,
		OutData,
		SourceElement ? SourceElement->ElementGuid : FGuid());
}

bool FEHBGeneratedMeshCollector::CollectFromElement(
	const AEHBElementActorBase* SourceElement,
	const FTransform& TargetLocalToWorld,
	FEHBMeshAggregateData& OutData)
{
	OutData.Reset();
	return SourceElement && SourceElement->BuildMeshAggregateData(TargetLocalToWorld, OutData);
}

bool FEHBGeneratedMeshCollector::CollectFromComponents(
	const TArray<UEHBGeneratedMeshComponent*>& Components,
	const FTransform& TargetLocalToWorld,
	FEHBMeshAggregateData& OutData,
	const FGuid& SourceElementGuid,
	bool bIncludeHiddenComponents)
{
	OutData.Reset();
	OutData.SourceElementGuid = SourceElementGuid;

	const FTransform WorldToTarget = TargetLocalToWorld.Inverse();
	for (int32 ComponentIndex = 0; ComponentIndex < Components.Num(); ++ComponentIndex)
	{
		const UEHBGeneratedMeshComponent* Component = Components[ComponentIndex];
		if (!Component)
		{
			continue;
		}
		if (!bIncludeHiddenComponents && !Component->IsVisible())
		{
			continue;
		}

		const FTransform ComponentToTarget = Component->GetComponentTransform() * WorldToTarget;
		const TArray<FEHBMeshSection>& Sections = Component->GetMeshSections();
		for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
		{
			const FEHBMeshSection& Section = Sections[SectionIndex];
			if (!Section.bSectionVisible || Section.ProcVertexBuffer.IsEmpty() || Section.ProcIndexBuffer.Num() < 3)
			{
				continue;
			}

			const int32 VertexBase = OutData.Vertices.Num();
			OutData.Vertices.Reserve(OutData.Vertices.Num() + Section.ProcVertexBuffer.Num());
			OutData.Normals.Reserve(OutData.Normals.Num() + Section.ProcVertexBuffer.Num());
			OutData.UV0.Reserve(OutData.UV0.Num() + Section.ProcVertexBuffer.Num());
			for (const FEHBMeshVertex& Vertex : Section.ProcVertexBuffer)
			{
				const FVector TargetPosition = ComponentToTarget.TransformPosition(Vertex.Position);
				OutData.Vertices.Add(TargetPosition);
				OutData.Normals.Add(ComponentToTarget.TransformVectorNoScale(Vertex.Normal).GetSafeNormal());
				OutData.UV0.Add(Vertex.UV0);
				OutData.LocalBounds += TargetPosition;
			}

			const int32 TriangleCount = Section.ProcIndexBuffer.Num() / 3;
			OutData.Triangles.Reserve(OutData.Triangles.Num() + TriangleCount);
			for (int32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
			{
				const int32 LocalA = static_cast<int32>(Section.ProcIndexBuffer[TriangleIndex * 3 + 0]);
				const int32 LocalB = static_cast<int32>(Section.ProcIndexBuffer[TriangleIndex * 3 + 1]);
				const int32 LocalC = static_cast<int32>(Section.ProcIndexBuffer[TriangleIndex * 3 + 2]);
				if (!Section.ProcVertexBuffer.IsValidIndex(LocalA)
					|| !Section.ProcVertexBuffer.IsValidIndex(LocalB)
					|| !Section.ProcVertexBuffer.IsValidIndex(LocalC))
				{
					continue;
				}

				FEHBMeshTriangleRef& TriangleRef = OutData.Triangles.AddDefaulted_GetRef();
				TriangleRef.VertexA = VertexBase + LocalA;
				TriangleRef.VertexB = VertexBase + LocalB;
				TriangleRef.VertexC = VertexBase + LocalC;
				TriangleRef.SourceComponentIndex = ComponentIndex;
				TriangleRef.SourceSectionIndex = SectionIndex;
				TriangleRef.SourceTriangleIndex = TriangleIndex;
			}
		}
	}

	return OutData.Vertices.Num() > 0 && OutData.Triangles.Num() > 0;
}
