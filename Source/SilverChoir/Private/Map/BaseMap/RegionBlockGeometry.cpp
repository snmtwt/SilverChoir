#include "RegionBlockGeometry.h"
#include "Algo/Reverse.h"
#include "Geometry/EHBSurfaceGeometryTypes.h"

namespace RegionBlockGeometry
{
static double Cross(FVector2D A, FVector2D B, FVector2D C) { return FVector2D::CrossProduct(B-A, C-A); }
static bool OnSegment(FVector2D A, FVector2D B, FVector2D P)
{
	return FMath::Abs(Cross(A,B,P)) < .001 && P.X >= FMath::Min(A.X,B.X)-.001 && P.X <= FMath::Max(A.X,B.X)+.001 && P.Y >= FMath::Min(A.Y,B.Y)-.001 && P.Y <= FMath::Max(A.Y,B.Y)+.001;
}
static bool Intersects(FVector2D A, FVector2D B, FVector2D C, FVector2D D)
{
	return (Cross(A,B,C)*Cross(A,B,D)<0 && Cross(C,D,A)*Cross(C,D,B)<0) || OnSegment(A,B,C) || OnSegment(A,B,D) || OnSegment(C,D,A) || OnSegment(C,D,B);
}
bool Build(TArray<FVector2D> P, double Height, FMesh& Out, FText& Error)
{
	Out = FMesh(); Error = FText();
	auto Fail = [&](const TCHAR* Message) { Error = FText::FromString(Message); return false; };
	if (!FMath::IsFinite(Height) || Height <= 0 || P.Num()>512) { return Fail(TEXT("高度必须为正数，采样点不能超过 512 个。")); }
	for (const auto& V : P) { if (!FMath::IsFinite(V.X) || !FMath::IsFinite(V.Y)) { return Fail(TEXT("曲线坐标无效。")); } }
	for (int32 I=P.Num()-1; I>=0 && P.Num()>1; --I) { if (P[I].Equals(P[(I+1)%P.Num()], .01)) { P.RemoveAt(I); } }
	bool Changed=true;
	while (Changed && P.Num()>3)
	{
		Changed=false;
		for (int32 I=0; I<P.Num(); ++I)
		{
			if (OnSegment(P[(I+P.Num()-1)%P.Num()], P[(I+1)%P.Num()], P[I])) { P.RemoveAt(I); Changed=true; break; }
		}
	}
	if (P.Num()<3) { return Fail(TEXT("至少需要三个不同的曲线点。")); }
	for (int32 I=0; I<P.Num(); ++I) for (int32 J=I+1; J<P.Num(); ++J)
	{
		if (J==I+1 || (I==0 && J==P.Num()-1)) { continue; }
		if (Intersects(P[I],P[(I+1)%P.Num()],P[J],P[(J+1)%P.Num()])) { return Fail(TEXT("曲线存在自相交或相互接触，请调整控制点。")); }
	}
	double Area=0; for (int32 I=0; I<P.Num(); ++I) { Area+=FVector2D::CrossProduct(P[I],P[(I+1)%P.Num()]); }
	if (FMath::Abs(Area)<.01) { return Fail(TEXT("曲线面积为零或过小。")); }
	if (Area<0) { Algo::Reverse(P); }
	FEHBSlabSurfaceMeshBuildInput Input;
	for (const auto& Point : P) { Input.TopBoundaryLoop.Add(FVector(Point,Height)); }
	Input.TopZ=Height; Input.DefaultBottomZ=0; Input.UVWorldSize=1000;
	Input.SideSegmentLength=MAX_flt;
	FEHBSurfaceMeshBuildResult Result;
	if (!FEHBPlanarSurfaceGeometryBuilder::BuildSlabSurface(Input,Result)) { return Fail(TEXT("EasyHouseBuilder 体积生成失败。")); }
	// 验证上下盖完整性；插件的返回值允许部分表面成功，区域要求闭合体积。
	double TopArea=0, BottomArea=0, Largest=0;
	FVector Label=FVector::ZeroVector, Centroid=FVector::ZeroVector;
	for (int32 I=0; I<Result.Triangles.Num(); I+=3)
	{
		const int32 Index=Result.Triangles[I];
		const FVector A=Result.Vertices[Index],B=Result.Vertices[Result.Triangles[I+1]],C=Result.Vertices[Result.Triangles[I+2]];
		const double TriangleArea=FVector::CrossProduct(B-A,C-A).Size()*.5;
		if (Result.Normals[Index].Z>.9)
		{
			TopArea+=TriangleArea; Centroid+=(A+B+C)/3*TriangleArea;
			if (TriangleArea>Largest) { Largest=TriangleArea; Label=(A+B+C)/3; }
		}
		else if (Result.Normals[Index].Z<-.9) { BottomArea+=TriangleArea; }
	}
	const double ExpectedArea=FMath::Abs(Area)*.5;
	const double Tolerance=FMath::Max(.01,ExpectedArea*1.e-6);
	if (!FMath::IsNearlyEqual(TopArea,ExpectedArea,Tolerance) || !FMath::IsNearlyEqual(BottomArea,ExpectedArea,Tolerance))
	{ return Fail(TEXT("体积顶面或底面不完整，请检查过近的控制点。")); }
	Centroid/=TopArea;
	if (FEHBSurfaceGeometryUtil::IsPointInsidePolygon2D(FVector2D(Centroid.X,Centroid.Y),P)) { Label=Centroid; }
	Out.Vertices=MoveTemp(Result.Vertices); Out.Indices=MoveTemp(Result.Triangles);
	Out.Normals=MoveTemp(Result.Normals); Out.UVs=MoveTemp(Result.UV0);
	Out.LabelPosition=Label; return true;
}
}
