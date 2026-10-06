#include "Core/EHBWallJunctionGeometry.h"

namespace
{
	float Cross2D(const FVector& A,const FVector& B){return A.X*B.Y-A.Y*B.X;}

	struct FEHBPillarConnectionGeometry
	{
		FVector Direction = FVector::ForwardVector;
		FVector LeftNormal = FVector::RightVector;
		float HalfWallThickness = 10.0f;
		float FrontOffset = 10.0f;
		float Angle = 0.0f;
		FVector LeftStart = FVector::ZeroVector;
		FVector LeftEnd = FVector::ZeroVector;
		FVector RightStart = FVector::ZeroVector;
		FVector RightEnd = FVector::ZeroVector;
	};

	struct FEHBPillarGapResult
	{
		FVector CurrentRightPoint = FVector::ZeroVector;
		FVector NextLeftPoint = FVector::ZeroVector;
		TArray<FVector> ExtraPoints;
	};

	float NormalizeAngle360(float Angle)
	{
		float Result = FMath::Fmod(Angle, 360.0f);
		if (Result < 0.0f)
		{
			Result += 360.0f;
		}
		// Adding 360 to a tiny negative float can round to exactly 360.
		// Keep the sort key in [0,360) so undo roundoff cannot rotate the polygon start.
		return Result >= 360.0f ? 0.0f : Result;
	}

	float GetCounterClockwiseAngleGap(float FromAngle, float ToAngle)
	{
		return NormalizeAngle360(ToAngle - FromAngle);
	}

	bool CalculateLineIntersection2D(
		const FVector& A,
		const FVector& B,
		const FVector& C,
		const FVector& D,
		FVector& OutIntersection,
		bool& bOutSegmentsIntersect)
	{
		const FVector Line1 = B - A;
		const FVector Line2 = D - C;
		const float Denominator = Cross2D(Line1, Line2);
		if (FMath::Abs(Denominator) <= UE_SMALL_NUMBER)
		{
			OutIntersection = A;
			bOutSegmentsIntersect = false;
			return false;
		}

		const FVector Delta = C - A;
		const float T = Cross2D(Delta, Line2) / Denominator;
		const float U = Cross2D(Delta, Line1) / Denominator;
		OutIntersection = A + Line1 * T;
		OutIntersection.Z = 0.0f;
		constexpr float SegmentTolerance = 0.001f;
		bOutSegmentsIntersect =
			T >= -SegmentTolerance && T <= 1.0f + SegmentTolerance &&
			U >= -SegmentTolerance && U <= 1.0f + SegmentTolerance;
		return true;
	}

	FVector MirrorPointAcrossAxis2D(const FVector& AxisOrigin, const FVector& AxisDirection, const FVector& Point)
	{
		const FVector SafeDirection = AxisDirection.GetSafeNormal2D();
		if (SafeDirection.IsNearlyZero())
		{
			return Point;
		}

		FVector Delta = Point - AxisOrigin;
		Delta.Z = 0.0f;
		FVector Projection = AxisOrigin + SafeDirection * FVector::DotProduct(Delta, SafeDirection);
		FVector Mirrored = Projection + (Projection - Point);
		Mirrored.Z = Point.Z;
		return Mirrored;
	}

	TArray<FVector> BuildBalancedWallFacePoints(const FVector& LeftPoint, const FVector& RightPoint, const FVector& AxisDirection)
	{
		const float LeftDistance = LeftPoint.Size2D();
		const float RightDistance = RightPoint.Size2D();
		if (FMath::Abs(LeftDistance - RightDistance) < 0.1f)
		{
			return { LeftPoint, RightPoint };
		}

		if (LeftDistance > RightDistance)
		{
			return { LeftPoint, MirrorPointAcrossAxis2D(FVector::ZeroVector, AxisDirection, LeftPoint) };
		}

		return { MirrorPointAcrossAxis2D(FVector::ZeroVector, AxisDirection, RightPoint), RightPoint };
	}

	bool FindForwardLinePolygonIntersection2D(
		const TArray<FVector>& Polygon,
		const FVector& LineOrigin,
		const FVector& Direction,
		FVector& OutPoint)
	{
		const FVector SafeDirection = Direction.GetSafeNormal2D();
		if (Polygon.Num() < 3 || SafeDirection.IsNearlyZero())
		{
			return false;
		}

		bool bFound = false;
		float BestT = -TNumericLimits<float>::Max();
		constexpr float SegmentTolerance = 0.001f;
		constexpr float ColinearTolerance = 0.01f;

		auto TryCandidate = [&](const FVector& Candidate)
		{
			const float T = FVector::DotProduct(Candidate - LineOrigin, SafeDirection);
			if (!bFound || T > BestT)
			{
				BestT = T;
				OutPoint = FVector(Candidate.X, Candidate.Y, 0.0f);
				bFound = true;
			}
		};

		for (int32 Index = 0; Index < Polygon.Num(); ++Index)
		{
			const FVector A = Polygon[Index];
			const FVector B = Polygon[(Index + 1) % Polygon.Num()];
			const FVector Edge = B - A;
			if (Edge.SizeSquared2D() <= UE_SMALL_NUMBER)
			{
				continue;
			}

			const float Denominator = Cross2D(SafeDirection, Edge);
			if (FMath::Abs(Denominator) <= UE_SMALL_NUMBER)
			{
				if (FMath::Abs(Cross2D(SafeDirection, A - LineOrigin)) <= ColinearTolerance)
				{
					TryCandidate(A);
					TryCandidate(B);
				}
				continue;
			}

			const FVector Delta = A - LineOrigin;
			const float T = Cross2D(Delta, Edge) / Denominator;
			const float U = Cross2D(Delta, SafeDirection) / Denominator;
			if (U >= -SegmentTolerance && U <= 1.0f + SegmentTolerance)
			{
				TryCandidate(LineOrigin + SafeDirection * T);
			}
		}

		return bFound;
	}

	void AddUniqueFootprintPoint(TArray<FVector>& Points, const FVector& Point)
	{
		constexpr float PointToleranceSquared = 0.01f;
		FVector SanitizedPoint(Point.X, Point.Y, 0.0f);
		for (const FVector& ExistingPoint : Points)
		{
			if (FVector::DistSquared2D(ExistingPoint, SanitizedPoint) <= PointToleranceSquared)
			{
				return;
			}
		}
		Points.Add(SanitizedPoint);
	}

	float CalculateSignedArea2D(const TArray<FVector>& Points)
	{
		double Area = 0.0;
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			const FVector& Current = Points[Index];
			const FVector& Next = Points[(Index + 1) % Points.Num()];
			Area += static_cast<double>(Current.X) * Next.Y - static_cast<double>(Next.X) * Current.Y;
		}
		return static_cast<float>(Area * 0.5);
	}

	bool IsSimpleFootprint(const TArray<FVector>& Points)
	{
		if(Points.Num()<3||FMath::Abs(CalculateSignedArea2D(Points))<=KINDA_SMALL_NUMBER)return false;
		constexpr double Epsilon=0.000001;
		auto Cross=[](FVector A,FVector B,FVector C){return (B.X-A.X)*(C.Y-A.Y)-(B.Y-A.Y)*(C.X-A.X);};
		auto On=[&](FVector A,FVector B,FVector P){return FMath::Abs(Cross(A,B,P))<=Epsilon&&P.X>=FMath::Min(A.X,B.X)-Epsilon&&P.X<=FMath::Max(A.X,B.X)+Epsilon&&P.Y>=FMath::Min(A.Y,B.Y)-Epsilon&&P.Y<=FMath::Max(A.Y,B.Y)+Epsilon;};
		for(int32 I=0;I<Points.Num();++I)
		{
			const FVector A=Points[I],B=Points[(I+1)%Points.Num()];
			if(A.ContainsNaN()||FVector::DistSquared2D(A,B)<=Epsilon)return false;
			for(int32 J=I+1;J<Points.Num();++J)
			{
				if(J==I+1||(I==0&&J==Points.Num()-1))continue;
				const FVector C=Points[J],D=Points[(J+1)%Points.Num()];
				if(On(A,B,C)||On(A,B,D)||On(C,D,A)||On(C,D,B)
					||(Cross(A,B,C)*Cross(A,B,D)<0&&Cross(C,D,A)*Cross(C,D,B)<0))return false;
			}
		}
		return true;
	}
}

bool FEHBWallJunctionGeometry::BuildFootprint(float Width,float Depth,const TArray<FEHBWallJunctionLeg>& Legs,TArray<FVector>& OutLocalFootprint,bool bExactWallThickness)
{
	OutLocalFootprint.Reset();
	if(!FMath::IsFinite(Width)||!FMath::IsFinite(Depth))return false;

	const float SafeWidth = FMath::Max(1.0f, Width);
	const float SafeDepth = FMath::Max(1.0f, Depth);
	const float HalfWidth = SafeWidth * 0.5f;
	const float HalfDepth = SafeDepth * 0.5f;
	const float BaseRadius = FMath::Max(1.0f, FMath::Min(HalfWidth, HalfDepth));
	const float MaxMiterDistance = FMath::Max(BaseRadius * 4.0f, BaseRadius + 1.0f);
	const float LineExtension = MaxMiterDistance + BaseRadius;

	auto AddFallbackRectangle = [&OutLocalFootprint, HalfWidth, HalfDepth]()
	{
		OutLocalFootprint = {
			FVector(-HalfWidth, -HalfDepth, 0.0f),
			FVector(HalfWidth, -HalfDepth, 0.0f),
			FVector(HalfWidth, HalfDepth, 0.0f),
			FVector(-HalfWidth, HalfDepth, 0.0f)
		};
	};

	TArray<FEHBPillarConnectionGeometry> Connections;
	for(const auto& Leg:Legs)
	{
		const FVector Direction=Leg.Direction;
		// Unit XY inputs avoid hidden normalization differences between preview and live adapters.
		if(Direction.ContainsNaN()||!FMath::IsNearlyZero(Direction.Z,0.000001)||!FMath::IsNearlyEqual(Direction.SizeSquared2D(),1.0,0.0001)||!FMath::IsFinite(Leg.WallThickness))return false;
		FEHBPillarConnectionGeometry& Connection=Connections.AddDefaulted_GetRef();
		Connection.Direction=Direction;
		Connection.LeftNormal=Direction.RotateAngleAxis(-90.0f,FVector::UpVector).GetSafeNormal2D();
		if(Connection.LeftNormal.IsNearlyZero())Connection.LeftNormal=FVector::RightVector;
		Connection.HalfWallThickness=bExactWallThickness?FMath::Max(1.0f,Leg.WallThickness)*0.5f:FMath::Clamp(Leg.WallThickness*0.5f,1.0f,FMath::Max(1.0f,BaseRadius*0.98f));
		Connection.FrontOffset=BaseRadius;
		Connection.Angle=NormalizeAngle360(FMath::RadiansToDegrees(FMath::Atan2(Direction.Y,Direction.X)));
		Connection.LeftStart=Connection.LeftNormal*Connection.HalfWallThickness+Connection.Direction*Connection.FrontOffset;
		Connection.LeftEnd=Connection.LeftStart+Connection.Direction*LineExtension;
		Connection.RightStart=-Connection.LeftNormal*Connection.HalfWallThickness+Connection.Direction*Connection.FrontOffset;
		Connection.RightEnd=Connection.RightStart+Connection.Direction*LineExtension;
	}

	if (Connections.IsEmpty())
	{
		AddFallbackRectangle();
		return true;
	}

	Connections.Sort([](const FEHBPillarConnectionGeometry& A, const FEHBPillarConnectionGeometry& B)
	{
		return A.Angle < B.Angle;
	});

	if (Connections.Num() == 1)
	{
		const FEHBPillarConnectionGeometry& Connection = Connections[0];
		const FVector Direction = Connection.Direction;
		const FVector LeftNormal = Connection.LeftNormal;
		OutLocalFootprint = {
			-Direction * BaseRadius + LeftNormal * BaseRadius,
			Direction * BaseRadius + LeftNormal * BaseRadius,
			Direction * BaseRadius - LeftNormal * BaseRadius,
			-Direction * BaseRadius - LeftNormal * BaseRadius
		};
		return true;
	}

	TArray<FEHBPillarGapResult> Gaps;
	Gaps.SetNum(Connections.Num());
	for (int32 Index = 0; Index < Connections.Num(); ++Index)
	{
		const FEHBPillarConnectionGeometry& Current = Connections[Index];
		const FEHBPillarConnectionGeometry& Next = Connections[(Index + 1) % Connections.Num()];
		FEHBPillarGapResult& Gap = Gaps[Index];
		Gap.CurrentRightPoint = Current.RightStart;
		Gap.NextLeftPoint = Next.LeftStart;

		FVector Intersection = FVector::ZeroVector;
		bool bSegmentsIntersect = false;
		const bool bHasIntersection = CalculateLineIntersection2D(
			Current.RightStart,
			Current.RightEnd,
			Next.LeftStart,
			Next.LeftEnd,
			Intersection,
			bSegmentsIntersect);

		const float IncludedAngle = GetCounterClockwiseAngleGap(Current.Angle, Next.Angle);
		const bool bIntersectionIsNear = bHasIntersection && Intersection.Size2D() <= MaxMiterDistance;
		if (bSegmentsIntersect && bIntersectionIsNear)
		{
			Gap.CurrentRightPoint = Intersection;
			Gap.NextLeftPoint = Intersection;
			continue;
		}

		if (IncludedAngle > 270.0f)
		{
			Gap.ExtraPoints.Add(-Current.Direction * BaseRadius - Current.LeftNormal * BaseRadius);
			Gap.ExtraPoints.Add(-Next.Direction * BaseRadius + Next.LeftNormal * BaseRadius);
			continue;
		}

		if (IncludedAngle > 90.0f && bIntersectionIsNear)
		{
			Gap.ExtraPoints.Add(Intersection);
		}
	}

	OutLocalFootprint.Reserve(Connections.Num() * 3);
	for (int32 Index = 0; Index < Connections.Num(); ++Index)
	{
		const FEHBPillarConnectionGeometry& Connection = Connections[Index];
		const FEHBPillarGapResult& PreviousGap = Gaps[(Index + Connections.Num() - 1) % Connections.Num()];
		const FEHBPillarGapResult& CurrentGap = Gaps[Index];

		const TArray<FVector> WallFacePoints = BuildBalancedWallFacePoints(
			PreviousGap.NextLeftPoint,
			CurrentGap.CurrentRightPoint,
			Connection.Direction);
		for (const FVector& Point : WallFacePoints)
		{
			AddUniqueFootprintPoint(OutLocalFootprint, Point);
		}

		for (const FVector& ExtraPoint : CurrentGap.ExtraPoints)
		{
			AddUniqueFootprintPoint(OutLocalFootprint, ExtraPoint);
		}
	}

	if(bExactWallThickness&&!IsSimpleFootprint(OutLocalFootprint))
	{
		// Balancing a face can extend it through the neighboring leg at a tight
		// multi-wall corner. Keep the angular boundary's original side contacts
		// for that junction, rather than generating a self-intersecting fill.
		for(int32 I=0;I<Connections.Num();++I)
		{
			const auto& A=Connections[I];const auto& B=Connections[(I+1)%Connections.Num()];
			FVector Intersection;bool Segments=false;
			// A fixed 4-radius miter cutoff can stop inside two intersecting wall
			// strips. Their forward rays supply the actual contact. The complete
			// wall-side solve still rejects a miter that consumes an available span.
			if(CalculateLineIntersection2D(A.RightStart,A.RightEnd,B.LeftStart,B.LeftEnd,Intersection,Segments)
				&&!Intersection.ContainsNaN()
				&&FVector::DotProduct(Intersection-A.RightStart,A.Direction)>=-0.001
				&&FVector::DotProduct(Intersection-B.LeftStart,B.Direction)>=-0.001)
			{
				Gaps[I].CurrentRightPoint=Gaps[I].NextLeftPoint=Intersection;Gaps[I].ExtraPoints.Reset();
			}
		}
		OutLocalFootprint.Reset();
		for(int32 I=0;I<Connections.Num();++I)
		{
			AddUniqueFootprintPoint(OutLocalFootprint,Gaps[(I+Connections.Num()-1)%Connections.Num()].NextLeftPoint);
			AddUniqueFootprintPoint(OutLocalFootprint,Gaps[I].CurrentRightPoint);
			for(const FVector& P:Gaps[I].ExtraPoints)AddUniqueFootprintPoint(OutLocalFootprint,P);
		}
		if(!IsSimpleFootprint(OutLocalFootprint))return false;
	}
	if (OutLocalFootprint.Num() < 3 || FMath::Abs(CalculateSignedArea2D(OutLocalFootprint)) <= KINDA_SMALL_NUMBER)
	{
		AddFallbackRectangle();
	}

	return OutLocalFootprint.Num() >= 3;
}

bool FEHBWallJunctionGeometry::ResolveFace(float Width,float Depth,const TArray<FVector>& LocalFootprint,const FVector& Direction,float WallThickness,FVector& OutLeft,FVector& OutRight,bool bExactWallThickness)
{
	OutLeft=OutRight=FVector::ZeroVector;
	if(!FMath::IsFinite(Width)||!FMath::IsFinite(Depth)||!FMath::IsFinite(WallThickness)||Direction.ContainsNaN()
		||!FMath::IsNearlyZero(Direction.Z,0.000001)||!FMath::IsNearlyEqual(Direction.SizeSquared2D(),1.0,0.0001)||LocalFootprint.Num()<3)return false;
	for(const auto& P:LocalFootprint)if(P.ContainsNaN()||!FMath::IsNearlyZero(P.Z,0.000001))return false;
	const float SafeWidth = FMath::Max(1.0f, Width);
	const float SafeDepth = FMath::Max(1.0f, Depth);
	const float BaseRadius = FMath::Max(1.0f, FMath::Min(SafeWidth, SafeDepth) * 0.5f);
	const float HalfWallThickness = bExactWallThickness ? FMath::Max(1.0f,WallThickness)*0.5f : FMath::Clamp(
		WallThickness * 0.5f,
		1.0f,
		FMath::Max(1.0f, BaseRadius * 0.98f));
	const FVector LeftNormal = Direction.RotateAngleAxis(-90.0f, FVector::UpVector).GetSafeNormal2D();

	FVector LeftPoint = FVector::ZeroVector;
	FVector RightPoint = FVector::ZeroVector;
	const bool bHasLeftPoint = FindForwardLinePolygonIntersection2D(
		LocalFootprint,
		LeftNormal * HalfWallThickness,
		Direction,
		LeftPoint);
	const bool bHasRightPoint = FindForwardLinePolygonIntersection2D(
		LocalFootprint,
		-LeftNormal * HalfWallThickness,
		Direction,
		RightPoint);

	FVector ConnectionLeftLocalPoint = Direction * BaseRadius + LeftNormal * HalfWallThickness;
	FVector ConnectionRightLocalPoint = Direction * BaseRadius - LeftNormal * HalfWallThickness;
	if (bHasLeftPoint && bHasRightPoint)
	{
		const TArray<FVector> WallFacePoints = BuildBalancedWallFacePoints(LeftPoint, RightPoint, Direction);
		if (WallFacePoints.Num() >= 2)
		{
			ConnectionLeftLocalPoint = WallFacePoints[0];
			ConnectionRightLocalPoint = WallFacePoints[1];
		}
	}
	else if (bHasLeftPoint)
	{
		ConnectionLeftLocalPoint = LeftPoint;
		ConnectionRightLocalPoint = MirrorPointAcrossAxis2D(FVector::ZeroVector, Direction, LeftPoint);
	}
	else if (bHasRightPoint)
	{
		ConnectionLeftLocalPoint = MirrorPointAcrossAxis2D(FVector::ZeroVector, Direction, RightPoint);
		ConnectionRightLocalPoint = RightPoint;
	}

	OutLeft=ConnectionLeftLocalPoint;OutRight=ConnectionRightLocalPoint;
	if(bExactWallThickness)
	{
		auto OnBoundary=[&](const FVector& P)
		{
			for(int32 I=0;I<LocalFootprint.Num();++I)
				if(FVector::DistSquared(P,FMath::ClosestPointOnSegment(P,LocalFootprint[I],LocalFootprint[(I+1)%LocalFootprint.Num()]))<=0.000001)return true;
			return false;
		};
		// A physical column may overhang a balanced face. An unbound junction must
		// actually meet both full-thickness wall sides. Mirroring the longer side
		// can move the shorter side outside an asymmetric multi-leg footprint.
		// Retain existing valid faces; otherwise use the two real intersections.
		if(!OnBoundary(OutLeft)||!OnBoundary(OutRight))
		{
			if(!bHasLeftPoint||!bHasRightPoint||!OnBoundary(LeftPoint)||!OnBoundary(RightPoint))return false;
			OutLeft=LeftPoint;OutRight=RightPoint;
		}
	}
	return !OutLeft.ContainsNaN()&&!OutRight.ContainsNaN();
}


bool FEHBWallJunctionGeometry::BuildStraightWallSides(const TArray<FEHBWallJunctionNodeInput>& Nodes,const TArray<FEHBWallJunctionWallInput>& Walls,TArray<FEHBWallJunctionWallSides>& OutWalls,FName& OutReason,const TSet<FGuid>* RequestedWallGuids,FEHBWallJunctionSolveStats* OutStats,TMap<FGuid,TArray<FVector>>* OutNodeFootprints)
{
	OutWalls.Reset();OutReason=NAME_None;if(OutStats)*OutStats={};if(OutNodeFootprints)OutNodeFootprints->Reset();
	FEHBWallJunctionSolveStats Stats;
	auto Fail=[&](FName Reason){OutReason=Reason;return false;};
	TMap<FGuid,int32> NodeIndex;
	TArray<TArray<FEHBWallJunctionLeg>> Legs;Legs.SetNum(Nodes.Num());
	TArray<TArray<FVector>> Footprints;Footprints.SetNum(Nodes.Num());
	for(int32 I=0;I<Nodes.Num();++I)
	{
		const auto& N=Nodes[I];const auto& T=N.LocalTransform;
		if(!N.NodeGuid.IsValid()||NodeIndex.Contains(N.NodeGuid))return Fail(TEXT("InvalidNodeIdentity"));
		if(!T.IsValid()||!T.GetScale3D().Equals(FVector::OneVector,0.0001)
			||!T.GetRotation().GetUpVector().Equals(FVector::UpVector,0.000001)
			||!FMath::IsFinite(N.Width)||!FMath::IsFinite(N.Depth)||N.Width<=0||N.Depth<=0)return Fail(TEXT("UnsupportedNodeGeometry"));
		NodeIndex.Add(N.NodeGuid,I);
	}
	TSet<FGuid> WallIds;
	for(const auto& W:Walls)
	{
		if(!W.WallGuid.IsValid()||WallIds.Contains(W.WallGuid))return Fail(TEXT("InvalidWallIdentity"));
		WallIds.Add(W.WallGuid);
		const int32* A=NodeIndex.Find(W.StartNodeGuid);const int32* B=NodeIndex.Find(W.EndNodeGuid);
		if(!A||!B||*A==*B)return Fail(TEXT("InvalidWallEndpoints"));
		if(!FMath::IsFinite(W.Thickness)||!FMath::IsFinite(W.Height)||W.Thickness<=0||W.Height<=0)return Fail(TEXT("InvalidWallDimensions"));
		const auto& TA=Nodes[*A].LocalTransform;const auto& TB=Nodes[*B].LocalTransform;
		const FVector Delta=TB.GetLocation()-TA.GetLocation();
		if(Delta.ContainsNaN()||!FMath::IsNearlyZero(Delta.Z,0.001)||Delta.SizeSquared2D()<=1)return Fail(TEXT("UnsupportedWallSpan"));
		for(int32 Side=0;Side<2;++Side)
		{
			const int32 Index=Side==0?*A:*B;const auto& T=Nodes[Index].LocalTransform;
			const FVector Direction=T.InverseTransformVectorNoScale((Side==0?Delta:-Delta).GetSafeNormal2D()).GetSafeNormal2D();
			if(Legs[Index].ContainsByPredicate([&](const auto& Leg){return Leg.Direction.Equals(Direction,0.000001);}))return Fail(TEXT("DuplicateWallDirection"));
			auto& Leg=Legs[Index].AddDefaulted_GetRef();Leg.Direction=Direction;Leg.WallThickness=W.Thickness;
		}
	}
	if(RequestedWallGuids)for(const FGuid Id:*RequestedWallGuids)if(!WallIds.Contains(Id))return Fail(TEXT("UnknownRequestedWall"));
	TSet<int32> NeededNodes;
	for(const auto& W:Walls)if(!RequestedWallGuids||RequestedWallGuids->Contains(W.WallGuid))
	{NeededNodes.Add(NodeIndex.FindChecked(W.StartNodeGuid));NeededNodes.Add(NodeIndex.FindChecked(W.EndNodeGuid));}
	for(int32 I=0;I<Nodes.Num();++I)if(!RequestedWallGuids||NeededNodes.Contains(I))
	{
		if(!BuildFootprint(Nodes[I].Width,Nodes[I].Depth,Legs[I],Footprints[I],Nodes[I].bExactWallThickness))return Fail(TEXT("InvalidJunctionFootprint"));
		++Stats.NodeFootprintsSolved;
	}
	TArray<FEHBWallJunctionWallSides> Prepared;
	for(const auto& W:Walls)
	{
		if(RequestedWallGuids&&!RequestedWallGuids->Contains(W.WallGuid))continue;
		const int32 A=NodeIndex.FindChecked(W.StartNodeGuid),B=NodeIndex.FindChecked(W.EndNodeGuid);
		const auto& NA=Nodes[A];const auto& NB=Nodes[B];
		const FVector Delta=(NB.LocalTransform.GetLocation()-NA.LocalTransform.GetLocation()).GetSafeNormal2D();
		FVector Faces[4];
		for(int32 Side=0;Side<2;++Side)
		{
			const int32 I=Side==0?A:B;const auto& N=Nodes[I];
			const FVector Direction=N.LocalTransform.InverseTransformVectorNoScale(Side==0?Delta:-Delta).GetSafeNormal2D();
			if(!ResolveFace(N.Width,N.Depth,Footprints[I],Direction,W.Thickness,Faces[Side*2],Faces[Side*2+1],N.bExactWallThickness))return Fail(TEXT("InvalidJunctionFace"));
			Faces[Side*2]=N.LocalTransform.TransformPosition(Faces[Side*2]);Faces[Side*2+1]=N.LocalTransform.TransformPosition(Faces[Side*2+1]);
		}
		auto& Result=Prepared.AddDefaulted_GetRef();Result.WallGuid=W.WallGuid;
		Result.LocalStart=(Faces[0]+Faces[1])*0.5;Result.LocalEnd=(Faces[2]+Faces[3])*0.5;
		// Endpoint miters must leave a positive span in the authored direction.
		// Normalizing a reversed span would silently turn an overlap into a wall.
		if(FVector::DotProduct(Result.LocalEnd-Result.LocalStart,Delta)<=1)return Fail(TEXT("CollapsedConnectionSpan"));
		const FVector Direction=(Result.LocalEnd-Result.LocalStart).GetSafeNormal2D();
		if(Direction.IsNearlyZero())return Fail(TEXT("CollapsedConnectionSpan"));
		Result.LocalTransform=FTransform(FRotator(0,Direction.Rotation().Yaw,0),FMath::Lerp(Result.LocalStart,Result.LocalEnd,0.5f));
		for(auto& F:Faces)F=Result.LocalTransform.InverseTransformPosition(F);
		const float StartLeftX=Faces[0].Y>=Faces[1].Y?Faces[0].X:Faces[1].X;
		const float StartRightX=Faces[0].Y>=Faces[1].Y?Faces[1].X:Faces[0].X;
		const float EndLeftX=Faces[2].Y>=Faces[3].Y?Faces[2].X:Faces[3].X;
		const float EndRightX=Faces[2].Y>=Faces[3].Y?Faces[3].X:Faces[2].X;
		if(EndLeftX-StartLeftX<=1||EndRightX-StartRightX<=1)return Fail(TEXT("CollapsedWallSide"));
		const float Y=FMath::Max(1.0f,W.Thickness)*0.5f,Z=FMath::Max(1.0f,W.Height);
		Result.StartLeft=Result.LocalTransform.TransformPosition(FVector(StartLeftX,Y,Z));Result.EndLeft=Result.LocalTransform.TransformPosition(FVector(EndLeftX,Y,Z));
		Result.StartRight=Result.LocalTransform.TransformPosition(FVector(StartRightX,-Y,Z));Result.EndRight=Result.LocalTransform.TransformPosition(FVector(EndRightX,-Y,Z));
		// Unbound fill must meet the actual full-thickness wall mesh, not only an inset face.
		auto OnFootprint=[&](int32 I,FVector P)
		{
			P=Nodes[I].LocalTransform.InverseTransformPosition(P);P.Z=0;
			for(int32 K=0;K<Footprints[I].Num();++K)
				if(FVector::DistSquared(P,FMath::ClosestPointOnSegment(P,Footprints[I][K],Footprints[I][(K+1)%Footprints[I].Num()]))<=0.000001)return true;
			return false;
		};
		if((NA.bExactWallThickness&&(!OnFootprint(A,Result.StartLeft)||!OnFootprint(A,Result.StartRight)))
			||(NB.bExactWallThickness&&(!OnFootprint(B,Result.EndLeft)||!OnFootprint(B,Result.EndRight))))return Fail(TEXT("UnboundJunctionGap"));
		if(Result.StartLeft.ContainsNaN()||Result.EndLeft.ContainsNaN()||Result.StartRight.ContainsNaN()||Result.EndRight.ContainsNaN())return Fail(TEXT("NonfiniteWallSide"));
	}
	Prepared.Sort([](const auto& A,const auto& B){return A.WallGuid.ToString()<B.WallGuid.ToString();});
	Stats.WallSidesSolved=Prepared.Num();if(OutStats)*OutStats=Stats;
	if(OutNodeFootprints)for(int32 I=0;I<Nodes.Num();++I)if(!RequestedWallGuids||NeededNodes.Contains(I))
		OutNodeFootprints->Add(Nodes[I].NodeGuid,MoveTemp(Footprints[I]));
	OutWalls=MoveTemp(Prepared);OutReason=TEXT("Ready");return true;
}
