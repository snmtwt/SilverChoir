// Copyright Epic Games, Inc. All Rights Reserved.
#include "Core/EHBWallJunctionMesh.h"
#include "Algo/Reverse.h"
#include "IndexTypes.h"

bool FEHBWallJunctionMeshBuilder::BuildPrism(const TArray<FVector>& Input,float Height,FEHBWallJunctionMesh& OutMesh)
{
	OutMesh={};
	if(Input.Num()<3||!FMath::IsFinite(Height)||Height<=0)return false;
	constexpr double Epsilon=0.000001;
	auto Cross=[](FVector A,FVector B,FVector C){return (B.X-A.X)*(C.Y-A.Y)-(B.Y-A.Y)*(C.X-A.X);};
	auto OnSegment=[&](FVector A,FVector B,FVector P){return FMath::Abs(Cross(A,B,P))<=Epsilon&&P.X>=FMath::Min(A.X,B.X)-Epsilon&&P.X<=FMath::Max(A.X,B.X)+Epsilon&&P.Y>=FMath::Min(A.Y,B.Y)-Epsilon&&P.Y<=FMath::Max(A.Y,B.Y)+Epsilon;};
	double Area2=0;
	for(int32 I=0;I<Input.Num();++I)
	{
		const FVector A=Input[I],B=Input[(I+1)%Input.Num()];
		if(A.ContainsNaN()||!FMath::IsNearlyZero(A.Z,Epsilon)||FVector::DistSquared2D(A,B)<=Epsilon)return false;
		Area2+=A.X*B.Y-A.Y*B.X;
		for(int32 J=I+1;J<Input.Num();++J)
		{
			if(J==I+1||(I==0&&J==Input.Num()-1))continue;
			const FVector C=Input[J],D=Input[(J+1)%Input.Num()];
			if(OnSegment(A,B,C)||OnSegment(A,B,D)||OnSegment(C,D,A)||OnSegment(C,D,B))return false;
			if(Cross(A,B,C)*Cross(A,B,D)<0&&Cross(C,D,A)*Cross(C,D,B)<0)return false;
		}
	}
	if(!FMath::IsFinite(Area2)||FMath::Abs(Area2)<=Epsilon)return false;
	FEHBWallJunctionMesh Mesh;Mesh.Footprint=Input;if(Area2<0)Algo::Reverse(Mesh.Footprint);
	// Remove redundant collinear boundary vertices before cap triangulation; they otherwise
	// allow zero-area ears. Strictly convex legacy inputs retain their original ordering.
	bool Removed=true;
	while(Removed&&Mesh.Footprint.Num()>3){Removed=false;for(int32 I=0;I<Mesh.Footprint.Num();++I){const int32 Count=Mesh.Footprint.Num();if(OnSegment(Mesh.Footprint[(I+Count-1)%Count],Mesh.Footprint[(I+1)%Count],Mesh.Footprint[I])){Mesh.Footprint.RemoveAt(I);Removed=true;break;}}}
	const auto& P=Mesh.Footprint;bool StrictlyConvex=true;
	for(int32 I=0;I<P.Num();++I)StrictlyConvex&=Cross(P[I],P[(I+1)%P.Num()],P[(I+2)%P.Num()])>Epsilon;
	TArray<UE::Geometry::FIndex3i> Caps;
	if(StrictlyConvex){for(int32 I=1;I+1<P.Num();++I)Caps.Emplace(0,I,I+1);}
	else
	{
		// Boundary-inclusive ears avoid clipping across another vertex on a diagonal.
		// Reject if no valid ear exists; never force a fan through a concave boundary.
		TArray<int32> Remaining;for(int32 I=0;I<P.Num();++I)Remaining.Add(I);
		while(Remaining.Num()>3)
		{
			bool Clipped=false;
			for(int32 I=0;I<Remaining.Num();++I)
			{
				const int32 A=Remaining[(I+Remaining.Num()-1)%Remaining.Num()],B=Remaining[I],C=Remaining[(I+1)%Remaining.Num()];
				if(Cross(P[A],P[B],P[C])<=Epsilon)continue;
				bool ContainsVertex=false;
				for(const int32 V:Remaining)
				{
					if(V==A||V==B||V==C)continue;
					if(Cross(P[A],P[B],P[V])>=-Epsilon&&Cross(P[B],P[C],P[V])>=-Epsilon&&Cross(P[C],P[A],P[V])>=-Epsilon){ContainsVertex=true;break;}
				}
				if(ContainsVertex)continue;
				Caps.Emplace(A,B,C);Remaining.RemoveAt(I);Clipped=true;break;
			}
			if(!Clipped)return false;
		}
		Caps.Emplace(Remaining[0],Remaining[1],Remaining[2]);
	}
	if(Caps.Num()!=P.Num()-2)return false;
	double TriArea2=0;
	for(const auto T:Caps)
	{
		if(!P.IsValidIndex(T.A)||!P.IsValidIndex(T.B)||!P.IsValidIndex(T.C))return false;
		const double A=Cross(P[T.A],P[T.B],P[T.C]);
		if(A<=Epsilon)return false;
		TriArea2+=A;
	}
	if(!FMath::IsNearlyEqual(TriArea2,FMath::Abs(Area2),FMath::Max(0.0001,FMath::Abs(Area2)*0.000001)))return false;
	const float SafeHeight=FMath::Max(1.0f,Height);const FVector Top(0,0,SafeHeight);
	auto UV=[](FVector V){return FVector2D(V.X/100.0f,V.Y/100.0f);};
	auto Triangle=[&](FVector A,FVector B,FVector C,FVector Normal)
	{
		const int32 First=Mesh.Vertices.Num();Mesh.Vertices.Append({A,B,C});Mesh.Normals.Append({Normal,Normal,Normal});Mesh.UVs.Append({UV(A),UV(B),UV(C)});
		int32 IB=First+1,IC=First+2;if(FVector::DotProduct(FVector::CrossProduct(B-A,C-A).GetSafeNormal(),Normal)>0)Swap(IB,IC);
		Mesh.Triangles.Append({First,IB,IC});
	};
	for(const auto T:Caps)Triangle(P[T.A]+Top,P[T.B]+Top,P[T.C]+Top,FVector::UpVector);
	for(const auto T:Caps)Triangle(P[T.A],P[T.C],P[T.B],FVector::DownVector);
	for(int32 I=0;I<P.Num();++I)
	{
		const FVector A=P[I],D=P[(I+1)%P.Num()],B=A+Top,C=D+Top,Edge=D-A,Normal=FVector(Edge.Y,-Edge.X,0).GetSafeNormal();
		const int32 First=Mesh.Vertices.Num();Mesh.Vertices.Append({A,B,C,D});Mesh.Normals.Append({Normal,Normal,Normal,Normal});
		const float U=FMath::Max(1.0f,static_cast<float>(FVector::Dist2D(A,D)))/100.0f,V=SafeHeight/100.0f;
		Mesh.UVs.Append({FVector2D(0,V),FVector2D(0,0),FVector2D(U,0),FVector2D(U,V)});
		if(FVector::DotProduct(FVector::CrossProduct(B-A,C-A).GetSafeNormal(),Normal)<=0)Mesh.Triangles.Append({First,First+1,First+2,First,First+2,First+3});
		else Mesh.Triangles.Append({First,First+2,First+1,First,First+3,First+2});
	}
	OutMesh=MoveTemp(Mesh);return true;
}
