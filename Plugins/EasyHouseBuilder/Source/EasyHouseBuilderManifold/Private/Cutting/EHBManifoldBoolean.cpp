// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cutting/EHBManifoldBoolean.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/Operations/MergeCoincidentMeshEdges.h"

THIRD_PARTY_INCLUDES_START
#include "manifold/manifold.h"
THIRD_PARTY_INCLUDES_END

namespace
{
	const TCHAR* GetManifoldErrorText(manifold::Manifold::Error Error)
	{
		switch (Error)
		{
		case manifold::Manifold::Error::NoError:
			return TEXT("NoError");
		case manifold::Manifold::Error::NonFiniteVertex:
			return TEXT("NonFiniteVertex");
		case manifold::Manifold::Error::NotManifold:
			return TEXT("NotManifold");
		case manifold::Manifold::Error::VertexOutOfBounds:
			return TEXT("VertexOutOfBounds");
		case manifold::Manifold::Error::PropertiesWrongLength:
			return TEXT("PropertiesWrongLength");
		case manifold::Manifold::Error::MissingPositionProperties:
			return TEXT("MissingPositionProperties");
		case manifold::Manifold::Error::MergeVectorsDifferentLengths:
			return TEXT("MergeVectorsDifferentLengths");
		case manifold::Manifold::Error::MergeIndexOutOfBounds:
			return TEXT("MergeIndexOutOfBounds");
		case manifold::Manifold::Error::TransformWrongLength:
			return TEXT("TransformWrongLength");
		case manifold::Manifold::Error::RunIndexWrongLength:
			return TEXT("RunIndexWrongLength");
		case manifold::Manifold::Error::FaceIDWrongLength:
			return TEXT("FaceIDWrongLength");
		case manifold::Manifold::Error::InvalidConstruction:
			return TEXT("InvalidConstruction");
		case manifold::Manifold::Error::ResultTooLarge:
			return TEXT("ResultTooLarge");
		case manifold::Manifold::Error::InvalidTangents:
			return TEXT("InvalidTangents");
		case manifold::Manifold::Error::Cancelled:
			return TEXT("Cancelled");
		default:
			return TEXT("Unknown");
		}
	}

	void SetFailure(FString* OutFailureReason, const FString& Reason)
	{
		if (OutFailureReason)
		{
			*OutFailureReason = Reason;
		}
	}

	int32 CountBoundaryEdges(const UE::Geometry::FDynamicMesh3& Mesh)
	{
		int32 BoundaryEdgeCount = 0;
		for (const int32 EdgeID : Mesh.EdgeIndicesItr())
		{
			if (Mesh.IsBoundaryEdge(EdgeID))
			{
				++BoundaryEdgeCount;
			}
		}
		return BoundaryEdgeCount;
	}

	void EnsureOutwardOrientation(UE::Geometry::FDynamicMesh3& Mesh)
	{
		using namespace UE::Geometry;

		if (Mesh.TriangleCount() == 0)
		{
			return;
		}

		const FVector3d MeshCenter = Mesh.GetBounds().Center();
		double OrientationScore = 0.0;
		for (const int32 TriangleID : Mesh.TriangleIndicesItr())
		{
			FVector3d A;
			FVector3d B;
			FVector3d C;
			Mesh.GetTriVertices(TriangleID, A, B, C);
			const FVector3d Cross = FVector3d::CrossProduct(B - A, C - A);
			const double TwiceArea = Cross.Length();
			if (TwiceArea <= UE_DOUBLE_SMALL_NUMBER)
			{
				continue;
			}

			const FVector3d Normal = Cross / TwiceArea;
			const FVector3d Centroid = (A + B + C) / 3.0;
			OrientationScore += TwiceArea * Normal.Dot(Centroid - MeshCenter);
		}

		if (OrientationScore < 0.0)
		{
			Mesh.ReverseOrientation(false);
		}
	}

	struct FManifoldGroupMap
	{
		TMap<uint32, int32> OriginalIDToGroupID;

		uint32 AddGroup(int32 GroupID)
		{
			const uint32 OriginalID = static_cast<uint32>(manifold::Manifold::ReserveIDs(1));
			OriginalIDToGroupID.Add(OriginalID, GroupID);
			return OriginalID;
		}
	};

	struct FManifoldTriangleRecord
	{
		uint64 A = 0;
		uint64 B = 0;
		uint64 C = 0;
		uint64 FaceID = 0;
		int32 GroupID = 0;
	};

	bool ConvertDynamicMeshToManifoldMesh(
		const UE::Geometry::FDynamicMesh3& SourceMesh,
		double MergeTolerance,
		manifold::MeshGL64& OutMesh,
		const TCHAR* DebugLabel,
		int32 FallbackGroupID,
		FManifoldGroupMap& OutGroupMap,
		FString* OutFailureReason)
	{
		using namespace UE::Geometry;

		OutMesh = manifold::MeshGL64();
		OutMesh.numProp = 3;
		OutMesh.tolerance = MergeTolerance;
		if (SourceMesh.TriangleCount() <= 0 || SourceMesh.VertexCount() <= 0)
		{
			SetFailure(OutFailureReason, FString::Printf(TEXT("%s mesh is empty"), DebugLabel ? DebugLabel : TEXT("Input")));
			return false;
		}

		TMap<int32, uint64> VertexIDToManifoldIndex;
		VertexIDToManifoldIndex.Reserve(SourceMesh.VertexCount());
		for (const int32 VertexID : SourceMesh.VertexIndicesItr())
		{
			const FVector3d Position = SourceMesh.GetVertex(VertexID);
			if (!FMath::IsFinite(Position.X)
				|| !FMath::IsFinite(Position.Y)
				|| !FMath::IsFinite(Position.Z))
			{
				SetFailure(
					OutFailureReason,
					FString::Printf(TEXT("%s mesh has non-finite vertex %d"), DebugLabel ? DebugLabel : TEXT("Input"), VertexID));
				return false;
			}

			const uint64 ManifoldIndex = static_cast<uint64>(OutMesh.vertProperties.size() / OutMesh.numProp);
			VertexIDToManifoldIndex.Add(VertexID, ManifoldIndex);
			OutMesh.vertProperties.push_back(Position.X);
			OutMesh.vertProperties.push_back(Position.Y);
			OutMesh.vertProperties.push_back(Position.Z);
		}

		TArray<FManifoldTriangleRecord> TriangleRecords;
		TriangleRecords.Reserve(SourceMesh.TriangleCount());
		const bool bUseTriangleGroups = SourceMesh.HasTriangleGroups();
		for (const int32 TriangleID : SourceMesh.TriangleIndicesItr())
		{
			const FIndex3i Triangle = SourceMesh.GetTriangle(TriangleID);
			const uint64* A = VertexIDToManifoldIndex.Find(Triangle.A);
			const uint64* B = VertexIDToManifoldIndex.Find(Triangle.B);
			const uint64* C = VertexIDToManifoldIndex.Find(Triangle.C);
			if (!A || !B || !C)
			{
				continue;
			}

			FVector3d VA;
			FVector3d VB;
			FVector3d VC;
			SourceMesh.GetTriVertices(TriangleID, VA, VB, VC);
			if (VectorUtil::Normal(VA, VB, VC).IsZero())
			{
				continue;
			}

			FManifoldTriangleRecord& Record = TriangleRecords.AddDefaulted_GetRef();
			Record.A = *A;
			Record.B = *B;
			Record.C = *C;
			Record.FaceID = static_cast<uint64>(TriangleID);
			Record.GroupID = bUseTriangleGroups ? SourceMesh.GetTriangleGroup(TriangleID) : FallbackGroupID;
		}
		if (TriangleRecords.IsEmpty())
		{
			SetFailure(OutFailureReason, FString::Printf(TEXT("%s mesh has no valid triangles"), DebugLabel ? DebugLabel : TEXT("Input")));
			return false;
		}

		TriangleRecords.Sort(
			[](const FManifoldTriangleRecord& A, const FManifoldTriangleRecord& B)
			{
				if (A.GroupID != B.GroupID)
				{
					return A.GroupID < B.GroupID;
				}
				return A.FaceID < B.FaceID;
			});

		TMap<int32, uint32> GroupIDToOriginalID;
		OutMesh.triVerts.reserve(TriangleRecords.Num() * 3);
		OutMesh.faceID.reserve(TriangleRecords.Num());
		OutMesh.runIndex.push_back(0);
		int32 ActiveGroupID = TriangleRecords[0].GroupID;
		uint32 ActiveOriginalID = OutGroupMap.AddGroup(ActiveGroupID);
		GroupIDToOriginalID.Add(ActiveGroupID, ActiveOriginalID);
		OutMesh.runOriginalID.push_back(ActiveOriginalID);

		for (const FManifoldTriangleRecord& Record : TriangleRecords)
		{
			if (Record.GroupID != ActiveGroupID)
			{
				ActiveGroupID = Record.GroupID;
				if (const uint32* ExistingOriginalID = GroupIDToOriginalID.Find(ActiveGroupID))
				{
					ActiveOriginalID = *ExistingOriginalID;
				}
				else
				{
					ActiveOriginalID = OutGroupMap.AddGroup(ActiveGroupID);
					GroupIDToOriginalID.Add(ActiveGroupID, ActiveOriginalID);
				}
				OutMesh.runIndex.push_back(static_cast<uint64>(OutMesh.triVerts.size()));
				OutMesh.runOriginalID.push_back(ActiveOriginalID);
			}

			OutMesh.triVerts.push_back(Record.A);
			OutMesh.triVerts.push_back(Record.B);
			OutMesh.triVerts.push_back(Record.C);
			OutMesh.faceID.push_back(Record.FaceID);
		}
		OutMesh.runIndex.push_back(static_cast<uint64>(OutMesh.triVerts.size()));
		OutMesh.Merge();
		return OutMesh.NumTri() > 0;
	}

	bool ConvertManifoldMeshToDynamicMesh(
		const manifold::MeshGL64& SourceMesh,
		UE::Geometry::FDynamicMesh3& OutMesh,
		double MergeTolerance,
		int32 SlopeGroupID,
		int32 SideGroupID,
		const FManifoldGroupMap& GroupMap,
		FString* OutFailureReason)
	{
		using namespace UE::Geometry;

		OutMesh.Clear();
		OutMesh.EnableTriangleGroups(SlopeGroupID);
		OutMesh.EnableVertexNormals(FVector3f::UnitZ());
		OutMesh.EnableVertexUVs(FVector2f::Zero());
		if (SourceMesh.NumVert() <= 0 || SourceMesh.NumTri() <= 0 || SourceMesh.numProp < 3)
		{
			SetFailure(OutFailureReason, TEXT("Manifold result mesh is empty"));
			return false;
		}

		TArray<int32> VertexIDs;
		VertexIDs.Reserve(static_cast<int32>(SourceMesh.NumVert()));
		for (uint64 VertexIndex = 0; VertexIndex < SourceMesh.NumVert(); ++VertexIndex)
		{
			const uint64 PropertyIndex = VertexIndex * SourceMesh.numProp;
			if (PropertyIndex + 2 >= SourceMesh.vertProperties.size())
			{
				SetFailure(OutFailureReason, TEXT("Manifold result has invalid vertex property range"));
				return false;
			}

			const FVector3d Position(
				SourceMesh.vertProperties[PropertyIndex],
				SourceMesh.vertProperties[PropertyIndex + 1],
				SourceMesh.vertProperties[PropertyIndex + 2]);
			VertexIDs.Add(OutMesh.AppendVertex(Position));
		}

		const auto ResolveRunGroupID = [&GroupMap, SlopeGroupID, SideGroupID](uint32 OriginalID, const FVector3d& Normal)
		{
			if (const int32* GroupID = GroupMap.OriginalIDToGroupID.Find(OriginalID))
			{
				return *GroupID;
			}
			return Normal.Z <= 0.05 ? SideGroupID : SlopeGroupID;
		};

		const int32 RunCount = static_cast<int32>(SourceMesh.runOriginalID.size());
		if (RunCount > 0 && SourceMesh.runIndex.size() >= static_cast<size_t>(RunCount + 1))
		{
			for (int32 RunIndex = 0; RunIndex < RunCount; ++RunIndex)
			{
				const uint64 StartIndex = SourceMesh.runIndex[RunIndex];
				const uint64 EndIndex = SourceMesh.runIndex[RunIndex + 1];
				const uint32 OriginalID = SourceMesh.runOriginalID[RunIndex];
				for (uint64 Index = StartIndex; Index + 2 < EndIndex && Index + 2 < SourceMesh.triVerts.size(); Index += 3)
				{
					const uint64 SourceA = SourceMesh.triVerts[Index];
					const uint64 SourceB = SourceMesh.triVerts[Index + 1];
					const uint64 SourceC = SourceMesh.triVerts[Index + 2];
					const int32 AIndex = static_cast<int32>(SourceA);
					const int32 BIndex = static_cast<int32>(SourceB);
					const int32 CIndex = static_cast<int32>(SourceC);
					if (!VertexIDs.IsValidIndex(AIndex)
						|| !VertexIDs.IsValidIndex(BIndex)
						|| !VertexIDs.IsValidIndex(CIndex))
					{
						continue;
					}

					const FVector3d A = OutMesh.GetVertex(VertexIDs[AIndex]);
					const FVector3d B = OutMesh.GetVertex(VertexIDs[BIndex]);
					const FVector3d C = OutMesh.GetVertex(VertexIDs[CIndex]);
					const FVector3d Normal = VectorUtil::Normal(A, B, C);
					if (Normal.IsZero())
					{
						continue;
					}

					OutMesh.AppendTriangle(
						VertexIDs[AIndex],
						VertexIDs[BIndex],
						VertexIDs[CIndex],
						ResolveRunGroupID(OriginalID, Normal));
				}
			}
		}

		FMergeCoincidentMeshEdges MergeEdges(&OutMesh);
		MergeEdges.MergeVertexTolerance = MergeTolerance;
		MergeEdges.MergeSearchTolerance = MergeTolerance * 2.0;
		MergeEdges.OnlyUniquePairs = false;
		MergeEdges.Apply();
		EnsureOutwardOrientation(OutMesh);
		if (OutMesh.TriangleCount() <= 0)
		{
			SetFailure(OutFailureReason, TEXT("Converted Manifold result has no triangles"));
			return false;
		}
		return true;
	}
}

bool FEHBManifoldBoolean::ApplyDifference(
	const UE::Geometry::FDynamicMesh3& TargetMesh,
	const UE::Geometry::FDynamicMesh3& SourceMesh,
	UE::Geometry::FDynamicMesh3& OutDifferenceMesh,
	double MergeTolerance,
	int32 SlopeGroupID,
	int32 SideGroupID,
	FString* OutFailureReason)
{
	OutDifferenceMesh.Clear();

	manifold::MeshGL64 TargetManifoldMesh;
	manifold::MeshGL64 SourceManifoldMesh;
	FManifoldGroupMap GroupMap;
	if (!ConvertDynamicMeshToManifoldMesh(TargetMesh, MergeTolerance, TargetManifoldMesh, TEXT("Target"), SlopeGroupID, GroupMap, OutFailureReason)
		|| !ConvertDynamicMeshToManifoldMesh(SourceMesh, MergeTolerance, SourceManifoldMesh, TEXT("Source"), SideGroupID, GroupMap, OutFailureReason))
	{
		return false;
	}

	const manifold::Manifold TargetManifold(TargetManifoldMesh);
	const manifold::Manifold SourceManifold(SourceManifoldMesh);
	const manifold::Manifold::Error TargetStatus = TargetManifold.Status();
	const manifold::Manifold::Error SourceStatus = SourceManifold.Status();
	if (TargetStatus != manifold::Manifold::Error::NoError
		|| SourceStatus != manifold::Manifold::Error::NoError)
	{
		SetFailure(
			OutFailureReason,
			FString::Printf(
				TEXT("Manifold input status failed target=%s source=%s"),
				GetManifoldErrorText(TargetStatus),
				GetManifoldErrorText(SourceStatus)));
		return false;
	}

	const manifold::Manifold DifferenceManifold =
		TargetManifold.Boolean(SourceManifold, manifold::OpType::Subtract);
	const manifold::Manifold::Error DifferenceStatus = DifferenceManifold.Status();
	if (DifferenceStatus != manifold::Manifold::Error::NoError || DifferenceManifold.IsEmpty())
	{
		SetFailure(
			OutFailureReason,
			FString::Printf(
				TEXT("Manifold difference failed status=%s empty=%d"),
				GetManifoldErrorText(DifferenceStatus),
				DifferenceManifold.IsEmpty() ? 1 : 0));
		return false;
	}

	const manifold::MeshGL64 ResultMesh = DifferenceManifold.GetMeshGL64();
	return ConvertManifoldMeshToDynamicMesh(
		ResultMesh,
		OutDifferenceMesh,
		MergeTolerance,
		SlopeGroupID,
		SideGroupID,
		GroupMap,
		OutFailureReason);
}

bool FEHBManifoldBoolean::ApplyUnion(
	const UE::Geometry::FDynamicMesh3& MeshA,
	const UE::Geometry::FDynamicMesh3& MeshB,
	UE::Geometry::FDynamicMesh3& OutUnionMesh,
	double MergeTolerance,
	int32 SlopeGroupID,
	int32 SideGroupID,
	FString* OutFailureReason)
{
	OutUnionMesh.Clear();

	manifold::MeshGL64 ManifoldMeshA;
	manifold::MeshGL64 ManifoldMeshB;
	FManifoldGroupMap GroupMap;
	if (!ConvertDynamicMeshToManifoldMesh(MeshA, MergeTolerance, ManifoldMeshA, TEXT("UnionA"), SlopeGroupID, GroupMap, OutFailureReason)
		|| !ConvertDynamicMeshToManifoldMesh(MeshB, MergeTolerance, ManifoldMeshB, TEXT("UnionB"), SlopeGroupID, GroupMap, OutFailureReason))
	{
		return false;
	}

	const manifold::Manifold ManifoldA(ManifoldMeshA);
	const manifold::Manifold ManifoldB(ManifoldMeshB);
	const manifold::Manifold::Error StatusA = ManifoldA.Status();
	const manifold::Manifold::Error StatusB = ManifoldB.Status();
	if (StatusA != manifold::Manifold::Error::NoError
		|| StatusB != manifold::Manifold::Error::NoError)
	{
		SetFailure(
			OutFailureReason,
			FString::Printf(
				TEXT("Manifold union input status failed a=%s b=%s"),
				GetManifoldErrorText(StatusA),
				GetManifoldErrorText(StatusB)));
		return false;
	}

	const manifold::Manifold UnionManifold =
		ManifoldA.Boolean(ManifoldB, manifold::OpType::Add);
	const manifold::Manifold::Error UnionStatus = UnionManifold.Status();
	if (UnionStatus != manifold::Manifold::Error::NoError || UnionManifold.IsEmpty())
	{
		SetFailure(
			OutFailureReason,
			FString::Printf(
				TEXT("Manifold union failed status=%s empty=%d"),
				GetManifoldErrorText(UnionStatus),
				UnionManifold.IsEmpty() ? 1 : 0));
		return false;
	}

	const manifold::MeshGL64 ResultMesh = UnionManifold.GetMeshGL64();
	return ConvertManifoldMeshToDynamicMesh(
		ResultMesh,
		OutUnionMesh,
		MergeTolerance,
		SlopeGroupID,
		SideGroupID,
		GroupMap,
		OutFailureReason);
}
