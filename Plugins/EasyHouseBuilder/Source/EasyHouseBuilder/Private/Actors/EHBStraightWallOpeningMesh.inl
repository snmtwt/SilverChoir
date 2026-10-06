#include "Core/EHBStraightWallOpeningMesh.h"

namespace EHBStraightWallMesh150
{
void Side(const FEHBWallResolvedGeometry& Geometry,float Height,float Thickness,bool bLeftSide,const TArray<TArray<FVector2d>>& ResolvedOpenings,TArray<FVector>& OutVertices, TArray<int32>& OutTriangles, TArray<FVector>& OutNormals, TArray<FVector2D>& OutUVs,bool* Succeeded,const TCHAR* DebugName)
{
	if(Succeeded)*Succeeded=false;
	OutVertices.Reset();
	OutTriangles.Reset();
	OutNormals.Reset();
	OutUVs.Reset();


	const float SafeHeight = FMath::Max(1.0f, Height);
	const float SafeThickness = FMath::Max(1.0f, Thickness);
	const float HalfThickness = SafeThickness * 0.5f;
	const float WallY = bLeftSide ? HalfThickness : -HalfThickness;
	const FVector SurfaceNormal = bLeftSide ? FVector(0.0f, 1.0f, 0.0f) : FVector(0.0f, -1.0f, 0.0f);
	const float StartX = bLeftSide ? Geometry.StartLeftX : Geometry.StartRightX;
	const float EndX = bLeftSide ? Geometry.EndLeftX : Geometry.EndRightX;
	const float SurfaceLength = EndX - StartX;
	if (SurfaceLength <= UE_SMALL_NUMBER)
	{
		return;
	}

	/**
	 * 无洞口时继续走原来的四边形路径。
	 *
	 * 除了减少计算量，这也保证没有门窗的墙面仍保持原有的四顶点拓扑，
	 * 不会因为引入三角化功能而让普通墙面的顶点数、UV 或碰撞复杂度发生变化。
	 */
	auto BuildUncutSurface = [&]()
	{
		const float LengthUV = GetWallUVLength(SurfaceLength);
		const float HeightUV = GetWallUVLength(SafeHeight);

		if (bLeftSide)
		{
			const FVector BottomStart(StartX, WallY, 0.0f);
			const FVector TopStart(StartX, WallY, SafeHeight);
			const FVector TopEnd(EndX, WallY, SafeHeight);
			const FVector BottomEnd(EndX, WallY, 0.0f);
			AddWallQuadFace(OutVertices, OutTriangles, OutNormals, OutUVs, BottomStart, TopStart, TopEnd, BottomEnd, SurfaceNormal, LengthUV, HeightUV);
		}
		else
		{
			const FVector BottomEnd(EndX, WallY, 0.0f);
			const FVector TopEnd(EndX, WallY, SafeHeight);
			const FVector TopStart(StartX, WallY, SafeHeight);
			const FVector BottomStart(StartX, WallY, 0.0f);
			AddWallQuadFace(OutVertices, OutTriangles, OutNormals, OutUVs, BottomEnd, TopEnd, TopStart, BottomStart, SurfaceNormal, LengthUV, HeightUV);
		}
	};

	/**
	 * 第一步：把墙体连接数据中的每个洞口转换为墙体局部 XZ 平面上的二维多边形。
	 *
	 * X 坐标不能只依赖门窗 Actor 的世界位置，因为墙体保存/加载后 Actor 的瞬时变换可能还没恢复。
	 * Connection.DistanceFromStart 是持久化的沿墙距离，因此先用
	 *     -墙参考长度 / 2 + 距墙起点距离
	 * 得到门窗原点在墙体局部 X 轴上的位置。
	 *
	 * 样条点自身的局部偏移使用 DoorWindowLocalToWall 的旋转和缩放转换。
	 * 我们故意不使用该变换的平移 X，因为平移 X 与 DistanceFromStart 表达的是同一信息，
	 * 以持久化距离为准可以避免两份数据在编辑器撤销、加载或柱子裁切后出现微小漂移。
	 *
	 * Z 坐标由 BottomHeight 加上样条点转换后的局部 Z 得到。
	 * 因此窗洞可以悬在墙中间，门洞的底边则通常落在 Z=0。
	 */
	const auto& OpeningPolygons=ResolvedOpenings;

	if (OpeningPolygons.IsEmpty())
	{
		BuildUncutSurface();
		if(Succeeded)*Succeeded=true;
		return;
	}

	/**
	 * 第二步：建立二维线段排列。
	 *
	 * 不能简单地把窗洞作为“孔”交给普通多边形三角化器，因为门洞会穿过墙底边，
	 * 此时它不是一个完全包含在外轮廓中的孔，而是从外轮廓切掉的一块凹槽。
	 *
	 * FArrangement2d 会把墙边与洞口边、洞口与洞口的所有交点拆成共享顶点，
	 * 得到一张没有边交叉的平面图。后面的约束三角化因此无需区分“窗洞”和“门洞”：
	 * 两者只是保留区域分类不同，拓扑求交过程完全相同。
	 */
	UE::Geometry::FArrangement2d Arrangement(
		FMath::Max(static_cast<double>(SurfaceLength), static_cast<double>(SafeHeight)) / 64.0);

	const TArray<FVector2d> WallBoundary = {
		FVector2d(StartX, 0.0),
		FVector2d(StartX, SafeHeight),
		FVector2d(EndX, SafeHeight),
		FVector2d(EndX, 0.0)
	};
	InsertClosedOpeningPolygon(Arrangement, WallBoundary);

	for (const TArray<FVector2d>& OpeningPolygon : OpeningPolygons)
	{
		InsertClosedOpeningPolygon(Arrangement, OpeningPolygon);
	}

	/**
	 * 第三步：对平面图进行约束 Delaunay 三角化，并在回调中执行真正的“墙面减洞”。
	 *
	 * 三角化器先生成被所有墙边和洞口边约束的候选三角形。每个候选三角形取重心：
	 * - 重心不在墙体矩形内：丢弃；
	 * - 重心位于任意洞口轮廓内：丢弃；
	 * - 其余三角形：保留为墙面。
	 *
	 * 由于所有轮廓边已经是约束边，任何三角形都不会跨过洞口边界，
	 * 所以用重心分类不会出现“一个三角形一半在墙里、一半在洞里”的情况。
	 * 多个洞口相交或重叠时，“位于任意洞口内”自然得到洞口并集。
	 */
	UE::Geometry::FConstrainedDelaunay2d Triangulator;
	Triangulator.FillRule = UE::Geometry::FConstrainedDelaunay2d::EFillRule::Odd;
	Triangulator.bOrientedEdges = false;
	Triangulator.bSplitBowties = true;
	Triangulator.Add(Arrangement.Graph);

	const double ClassificationTolerance = EHBWallOpeningPointTolerance;
	const bool bTriangulationSucceeded = Triangulator.Triangulate(
		[&OpeningPolygons, StartX, EndX, SafeHeight, ClassificationTolerance](
			const TArray<FVector2d>& Vertices,
			const UE::Geometry::FIndex3i& Triangle)
		{
			const FVector2d Centroid =
				(Vertices[Triangle.A] + Vertices[Triangle.B] + Vertices[Triangle.C]) / 3.0;

			const bool bInsideWall =
				Centroid.X >= static_cast<double>(StartX) - ClassificationTolerance
				&& Centroid.X <= static_cast<double>(EndX) + ClassificationTolerance
				&& Centroid.Y >= -ClassificationTolerance
				&& Centroid.Y <= static_cast<double>(SafeHeight) + ClassificationTolerance;
			if (!bInsideWall)
			{
				return false;
			}

			for (const TArray<FVector2d>& OpeningPolygon : OpeningPolygons)
			{
				if (IsPointInsideOpeningPolygon(Centroid, OpeningPolygon))
				{
					return false;
				}
			}
			return true;
		});

	if (!bTriangulationSucceeded)
	{
		// Prepared edits fail closed; legacy rebuild retains its historical fallback.
		if(Succeeded)return;
		// 三角化失败时即使数组中残留了部分三角形，也不能使用，因为它们可能只覆盖一部分墙面。
		// 输入轮廓异常时宁可恢复完整墙面，也不要让墙体因为一次无效样条编辑而局部消失。
		UE_LOG(
			LogEHBWallMesh,
			Warning,
			TEXT("墙面洞口约束三角化失败，已回退为未挖洞墙面。Wall=%s Side=%s OpeningCount=%d"),
			DebugName,
			bLeftSide ? TEXT("Left") : TEXT("Right"),
			OpeningPolygons.Num());
		BuildUncutSurface();
		return;
	}

	/**
	 * 第四步：把二维三角化结果还原为当前侧墙面的三维顶点。
	 *
	 * 二维点使用 (X,Z)，三维点使用 (X,WallY,Z)。左右墙面只改变固定 Y、法线和绕序。
	 * UV 继续使用每 100cm 一个单位的世界尺度投影，并保持原墙面“顶部 V=0”的方向。
	 */
	OutVertices.Reserve(Triangulator.Vertices.Num());
	OutNormals.Reserve(Triangulator.Vertices.Num());
	OutUVs.Reserve(Triangulator.Vertices.Num());
	for (const FVector2d& Vertex2D : Triangulator.Vertices)
	{
		OutVertices.Add(FVector(
			static_cast<float>(Vertex2D.X),
			WallY,
			static_cast<float>(Vertex2D.Y)));
		OutNormals.Add(SurfaceNormal);
		OutUVs.Add(FVector2D(
			static_cast<float>((Vertex2D.X - StartX) / EHBWallUVWorldSize),
			static_cast<float>((SafeHeight - Vertex2D.Y) / EHBWallUVWorldSize)));
	}

	OutTriangles.Reserve(Triangulator.Triangles.Num() * 3);
	for (const UE::Geometry::FIndex3i& Triangle : Triangulator.Triangles)
	{
		int32 A = Triangle.A;
		int32 B = Triangle.B;
		int32 C = Triangle.C;

		// 保持与 AddWallQuadFace 相同的 generated mesh 绕序约定：几何叉积方向与目标法线相反。
		const FVector TriangleNormal =
			FVector::CrossProduct(OutVertices[B] - OutVertices[A], OutVertices[C] - OutVertices[A]).GetSafeNormal();
		if (FVector::DotProduct(TriangleNormal, SurfaceNormal) > 0.0f)
		{
			Swap(B, C);
		}

		OutTriangles.Add(A);
		OutTriangles.Add(B);
		OutTriangles.Add(C);
	}
	if(Succeeded)*Succeeded=true;
}

void Caps(const FEHBWallResolvedGeometry& Geometry,float Height,float Thickness,bool bGenerateStartCap,bool bGenerateEndCap,const TArray<TArray<FVector2d>>& ResolvedOpenings,TArray<FVector>& OutVertices, TArray<int32>& OutTriangles, TArray<FVector>& OutNormals, TArray<FVector2D>& OutUVs,bool* Succeeded)
{
 if(Succeeded)*Succeeded=false;
	OutVertices.Reset();
	OutTriangles.Reset();
	OutNormals.Reset();
	OutUVs.Reset();


	const float SafeHeight = FMath::Max(1.0f, Height);
	const float SafeThickness = FMath::Max(1.0f, Thickness);
	const float HalfThickness = SafeThickness * 0.5f;
	const float HeightUV = GetWallUVLength(SafeHeight);

	const FVector StartLeftBottom(Geometry.StartLeftX, HalfThickness, 0.0f);
	const FVector StartRightBottom(Geometry.StartRightX, -HalfThickness, 0.0f);
	const FVector EndLeftBottom(Geometry.EndLeftX, HalfThickness, 0.0f);
	const FVector EndRightBottom(Geometry.EndRightX, -HalfThickness, 0.0f);
	const FVector StartLeftTop(Geometry.StartLeftX, HalfThickness, SafeHeight);
	const FVector StartRightTop(Geometry.StartRightX, -HalfThickness, SafeHeight);
	const FVector EndLeftTop(Geometry.EndLeftX, HalfThickness, SafeHeight);
	const FVector EndRightTop(Geometry.EndRightX, -HalfThickness, SafeHeight);

 // Structural top and bottom use the same opening cross-section subtraction.
 // In particular, a doorway must remove its base strip instead of retaining
 // an invisible support bridge beneath the opening.
 const auto& Openings=ResolvedOpenings;
 const double MinX=FMath::Min(FMath::Min(Geometry.StartLeftX,Geometry.StartRightX),FMath::Min(Geometry.EndLeftX,Geometry.EndRightX));
 const double MaxX=FMath::Max(FMath::Max(Geometry.StartLeftX,Geometry.StartRightX),FMath::Max(Geometry.EndLeftX,Geometry.EndRightX));
 auto ClipX=[](const TArray<FVector>& Points,double X,bool KeepGreater)
 {
  TArray<FVector> Result;
  for(int32 I=0;I<Points.Num();++I){const auto& P=Points[I];const auto& Q=Points[(I+1)%Points.Num()];const bool A=KeepGreater?P.X>=X:P.X<=X,B=KeepGreater?Q.X>=X:Q.X<=X;if(A)Result.Add(P);if(A!=B)Result.Add(FMath::Lerp(P,Q,(X-P.X)/(Q.X-P.X)));}return Result;
 };
 for(bool Top:{true,false})
 {
  TArray<TPair<double,double>> Visible{{MinX,MaxX}};bool CutPlane=false;
  const double PlaneZ=Top?SafeHeight:0,Z=PlaneZ+(Top?-1.e-7:1.e-7);
  for(const auto& Opening:Openings)
  {
   const bool Reaches=Opening.ContainsByPredicate([&](const auto& P){return Top?P.Y>=PlaneZ:P.Y<=PlaneZ;});
   const bool Enters=Opening.ContainsByPredicate([&](const auto& P){return Top?P.Y<PlaneZ:P.Y>PlaneZ;});if(!Reaches||!Enters)continue;
   TArray<double> Crossings;for(int32 I=0;I<Opening.Num();++I){const auto& P=Opening[I];const auto& Q=Opening[(I+1)%Opening.Num()];if((P.Y>Z)!=(Q.Y>Z))Crossings.Add(P.X+(Q.X-P.X)*(Z-P.Y)/(Q.Y-P.Y));}Crossings.Sort();
   for(int32 I=0;I+1<Crossings.Num();I+=2)
   {
    const double A=Crossings[I],B=Crossings[I+1];if(B<=A)continue;TArray<TPair<double,double>> Next;
    for(const auto& V:Visible){if(B<=V.Key||A>=V.Value){Next.Add(V);continue;}CutPlane=true;if(A>V.Key)Next.Emplace(V.Key,A);if(B<V.Value)Next.Emplace(B,V.Value);}Visible=MoveTemp(Next);
   }
  }
  const FVector A=Top?StartLeftTop:StartLeftBottom,B=Top?EndLeftTop:EndLeftBottom,C=Top?EndRightTop:EndRightBottom,D=Top?StartRightTop:StartRightBottom,Normal=Top?FVector::UpVector:-FVector::UpVector;
  if(!CutPlane)AddWallQuadFaceWithUVs(OutVertices,OutTriangles,OutNormals,OutUVs,A,B,C,D,Normal,MakeTopProjectedUV(A,Geometry.ReferenceLength,SafeThickness),MakeTopProjectedUV(B,Geometry.ReferenceLength,SafeThickness),MakeTopProjectedUV(C,Geometry.ReferenceLength,SafeThickness),MakeTopProjectedUV(D,Geometry.ReferenceLength,SafeThickness));
  else for(const auto& V:Visible)
  {
   const auto Polygon=ClipX(ClipX({A,B,C,D},V.Key,true),V.Value,false);
   for(int32 I=1;I+1<Polygon.Num();++I){FVector P=Polygon[0],Q=Polygon[I],R=Polygon[I+1];const auto Cross=FVector::CrossProduct(Q-P,R-P);if(Cross.SizeSquared()<1.e-12)continue;if((Cross.Z>0)!=Top)Swap(Q,R);const int32 Base=OutVertices.Num();for(const auto& Point:{P,Q,R}){OutVertices.Add(Point);OutNormals.Add(Normal);OutUVs.Add(MakeTopProjectedUV(Point,Geometry.ReferenceLength,SafeThickness));}OutTriangles.Append({Base,Base+1,Base+2});}
  }
 }

 // Parameterize either vertical end plane by distance along its actual miter
 // edge and height. Subtract the same XZ cutters used by the two wall sides.
 auto BuildEnd=[&](const FVector& A,const FVector& B,const FVector& Normal,bool Start)->bool
 {
  const double Width=FVector::Dist2D(A,B),DX=B.X-A.X;if(Width<=1.e-6)return false;
  bool Intersects=false;for(const auto& Opening:Openings){double Min=MAX_dbl,Max=-MAX_dbl;for(const auto& P:Opening){Min=FMath::Min(Min,P.X);Max=FMath::Max(Max,P.X);}Intersects|=Max>=FMath::Min(A.X,B.X)&&Min<=FMath::Max(A.X,B.X);}
  if(!Intersects){AddWallQuadFace(OutVertices,OutTriangles,OutNormals,OutUVs,A,A+FVector(0,0,SafeHeight),B+FVector(0,0,SafeHeight),B,Normal,GetWallUVLength(Width),HeightUV);return true;}
  TArray<TArray<FVector2d>> Cutters;
  if(FMath::Abs(DX)>1.e-6)
  {
   for(const auto& Opening:Openings){auto& Cut=Cutters.AddDefaulted_GetRef();for(const auto& P:Opening)Cut.Add(FVector2d((P.X-A.X)*Width/DX,P.Y));}
  }
  else
  {
   // A perpendicular end samples just inside the wall volume. A cutter merely
   // tangent on the outside must not erase a cap with no removed wall volume.
   const double X=A.X+(Start?1.e-7:-1.e-7);
   for(const auto& Opening:Openings)
   {
    TArray<double> Zs{0.0,static_cast<double>(SafeHeight)};
    for(int32 I=0;I<Opening.Num();++I){const auto& P=Opening[I];const auto& Q=Opening[(I+1)%Opening.Num()];if((P.X>X)!=(Q.X>X))Zs.Add(FMath::Clamp(P.Y+(Q.Y-P.Y)*(X-P.X)/(Q.X-P.X),0.0,static_cast<double>(SafeHeight)));}
    Zs.Sort();for(int32 I=0;I+1<Zs.Num();++I)if(Zs[I+1]-Zs[I]>1.e-7&&IsPointInsideOpeningPolygon(FVector2d(X,(Zs[I]+Zs[I+1])*0.5),Opening))Cutters.Add({{0,Zs[I]},{Width,Zs[I]},{Width,Zs[I+1]},{0,Zs[I+1]}});
   }
  }
  UE::Geometry::FArrangement2d Arrangement(FMath::Max(Width,static_cast<double>(SafeHeight))/64.0);
  InsertClosedOpeningPolygon(Arrangement,{{0,0},{Width,0},{Width,SafeHeight},{0,SafeHeight}});for(const auto& Cut:Cutters)InsertClosedOpeningPolygon(Arrangement,Cut);
  UE::Geometry::FConstrainedDelaunay2d Triangulator;Triangulator.FillRule=UE::Geometry::FConstrainedDelaunay2d::EFillRule::Odd;Triangulator.bOrientedEdges=false;Triangulator.bSplitBowties=true;Triangulator.Add(Arrangement.Graph);
  if(!Triangulator.Triangulate([&](const TArray<FVector2d>& Points,const UE::Geometry::FIndex3i& Triangle)
  {
   const auto P=(Points[Triangle.A]+Points[Triangle.B]+Points[Triangle.C])/3.0;
   if(P.X<0||P.X>Width||P.Y<0||P.Y>SafeHeight)return false;
   for(const auto& Cut:Cutters)if(IsPointInsideOpeningPolygon(P,Cut))return false;return true;
  }))return false;
  for(const auto& Triangle:Triangulator.Triangles)
  {
   const FVector2d UV[3]={Triangulator.Vertices[Triangle.A],Triangulator.Vertices[Triangle.B],Triangulator.Vertices[Triangle.C]};const int32 Base=OutVertices.Num();
   for(const auto& P:UV){auto V=FMath::Lerp(A,B,P.X/Width);V.Z=P.Y;OutVertices.Add(V);OutNormals.Add(Normal);OutUVs.Add(FVector2D(P.X/EHBWallUVWorldSize,(SafeHeight-P.Y)/EHBWallUVWorldSize));}
   const bool Flip=FVector::DotProduct(FVector::CrossProduct(OutVertices[Base+1]-OutVertices[Base],OutVertices[Base+2]-OutVertices[Base]),Normal)>0;
   OutTriangles.Append({Base,Base+(Flip?2:1),Base+(Flip?1:2)});
  }
  return true;
 };
 if((bGenerateStartCap&&!BuildEnd(StartRightBottom,StartLeftBottom,FVector::CrossProduct(FVector::UpVector,(StartLeftBottom-StartRightBottom).GetSafeNormal2D()).GetSafeNormal(),true)) ||
    (bGenerateEndCap&&!BuildEnd(EndLeftBottom,EndRightBottom,FVector::CrossProduct(FVector::UpVector,(EndRightBottom-EndLeftBottom).GetSafeNormal2D()).GetSafeNormal(),false)))
 {OutVertices.Reset();OutTriangles.Reset();OutNormals.Reset();OutUVs.Reset();return;}
 if(Succeeded)*Succeeded=true;
}
bool Reveals(const FEHBWallResolvedGeometry& Geometry,float Height,float Thickness,const TArray<TArray<FVector2d>>& OpeningPolygons,TArray<FVector>& OutVertices, TArray<int32>& OutTriangles, TArray<FVector>& OutNormals, TArray<FVector2D>& OutUVs)
{
 if(OpeningPolygons.IsEmpty())return true;
  TArray<TArray<FVector>> Inputs;
  for(const auto& Opening:OpeningPolygons){auto& Loop=Inputs.AddDefaulted_GetRef();for(const auto& P:Opening)Loop.Add(FVector(P.X,P.Y,0));}
  FEHBPolygonClipResult Union;
  if(!FEHBPolygonClipper::UnionXY(Inputs,Union))return false;
  const double T=FMath::Max(1.0f,Thickness),H=FMath::Max(1.0f,Height);
  auto StartX=[&](double Y){return FMath::Lerp(double(Geometry.StartRightX),double(Geometry.StartLeftX),Y/T+0.5);};
  auto EndX=[&](double Y){return FMath::Lerp(double(Geometry.EndRightX),double(Geometry.EndLeftX),Y/T+0.5);};
  auto Clip=[](const TArray<FVector>& Polygon,auto Distance)
  {
   TArray<FVector> Result;
   for(int32 I=0;I<Polygon.Num();++I)
   {
    const auto A=Polygon[I],B=Polygon[(I+1)%Polygon.Num()];const double DA=Distance(A),DB=Distance(B);
    if(DA>=0)Result.Add(A);
    if((DA>=0)!=(DB>=0))Result.Add(FMath::Lerp(A,B,DA/(DA-DB)));
   }
   return Result;
  };
  auto AppendLoop=[&](const TArray<FVector>& Loop)
  {
   for(int32 I=0;I<Loop.Num();++I)
   {
    const auto A=Loop[I],B=Loop[(I+1)%Loop.Num()];
    const FVector Origin(A.X,T*0.5,A.Y),Along(B.X-A.X,0,B.Y-A.Y);
    if(Along.IsNearlyZero())continue;
    auto Polygon=Clip(Clip(Clip(Clip(TArray<FVector>{Origin,Origin+Along,Origin+Along-FVector(0,T,0),Origin-FVector(0,T,0)},
     [&](const FVector& P){return P.X-StartX(P.Y);}),[&](const FVector& P){return EndX(P.Y)-P.X;}),
     [](const FVector& P){return P.Z;}),[&](const FVector& P){return H-P.Z;});
    if(Polygon.Num()<3)continue;
    // Faces coincident with the host boundary are exits, not reveal walls.
    auto AllOn=[&](auto Distance){for(const auto& P:Polygon)if(FMath::Abs(Distance(P))>1.e-6)return false;return true;};
    if(AllOn([](const FVector& P){return P.Z;})||AllOn([&](const FVector& P){return H-P.Z;})
     ||AllOn([&](const FVector& P){return P.X-StartX(P.Y);})||AllOn([&](const FVector& P){return EndX(P.Y)-P.X;}))continue;
    const FVector Direction=Along.GetSafeNormal(),Normal=FVector::CrossProduct(Along,FVector(0,-T,0)).GetSafeNormal();
    auto UV=[&](const FVector& P){return FVector2D(FVector::DotProduct(P-Origin,Direction)/EHBWallUVWorldSize,(T*0.5-P.Y)/EHBWallUVWorldSize);};
    for(int32 J=1;J+1<Polygon.Num();++J)AppendCapTriangle(OutVertices,OutTriangles,OutNormals,OutUVs,Polygon[0],Polygon[J],Polygon[J+1],Normal,UV(Polygon[0]),UV(Polygon[J]),UV(Polygon[J+1]));
   }
  };
  for(const auto& Region:Union.Regions){AppendLoop(Region.OuterLoop);for(const auto& Hole:Region.HoleLoops){TArray<FVector> Points;for(const auto& P:Hole.Points)Points.Add(P.LocalPosition);AppendLoop(Points);}}
 return true;
}
}

bool FEHBStraightWallOpeningMesh::Build(const FEHBStraightWallOpeningMeshSource& Source,
 FEHBStraightWallOpeningMeshes& Out,FName& Status)
{
 Out={};auto Fail=[&](FName Why){Status=Why;return false;};const auto& G=Source.Geometry;
 for(float Value:{G.ReferenceLength,G.StartLeftX,G.StartRightX,G.EndLeftX,G.EndRightX,Source.Height,Source.Thickness})
  if(!FMath::IsFinite(Value)||FMath::Abs(Value)>1.e8f)return Fail(TEXT("InvalidStraightWallMeshDimensions"));
 if(G.ReferenceLength<=0||G.EndLeftX<=G.StartLeftX||G.EndRightX<=G.StartRightX||Source.Height<1||Source.Thickness<1)return Fail(TEXT("InvalidStraightWallMeshDimensions"));
 for(const auto& Loop:Source.Openings)
 {
  if(Loop.Num()<3)return Fail(TEXT("InvalidStraightWallOpeningPolygon"));double TwiceArea=0;
  for(int32 I=0;I<Loop.Num();++I){const auto& P=Loop[I];const auto& Q=Loop[(I+1)%Loop.Num()];if(P.ContainsNaN()||FMath::Abs(P.X)>1.e8||FMath::Abs(P.Y)>1.e8)return Fail(TEXT("InvalidStraightWallOpeningPolygon"));TwiceArea+=P.X*Q.Y-Q.X*P.Y;}
  if(!FMath::IsFinite(TwiceArea)||FMath::Abs(TwiceArea)<=1.e-8)return Fail(TEXT("InvalidStraightWallOpeningPolygon"));
 }
 FEHBStraightWallOpeningMeshes Candidate;bool Ready=false;
 for(bool Left:{true,false})
 {
  auto& Mesh=Left?Candidate.Left:Candidate.Right;
  EHBStraightWallMesh150::Side(G,Source.Height,Source.Thickness,Left,Source.Openings,Mesh.Vertices,Mesh.Triangles,Mesh.Normals,Mesh.UVs,&Ready,TEXT("Candidate"));
  if(!Ready)return Fail(TEXT("StraightWallSideTriangulationFailed"));
 }
 auto& Caps=Candidate.Caps;
 EHBStraightWallMesh150::Caps(G,Source.Height,Source.Thickness,Source.bGenerateStartCap,Source.bGenerateEndCap,Source.Openings,Caps.Vertices,Caps.Triangles,Caps.Normals,Caps.UVs,&Ready);
 if(!Ready)return Fail(TEXT("StraightWallCapTriangulationFailed"));
 for(int32 I=0;I<Caps.Triangles.Num();I+=3)
 {
  if(I+2>=Caps.Triangles.Num()||!Caps.Vertices.IsValidIndex(Caps.Triangles[I])||!Caps.Vertices.IsValidIndex(Caps.Triangles[I+1])||!Caps.Vertices.IsValidIndex(Caps.Triangles[I+2]))return Fail(TEXT("InvalidStraightWallMeshOutput"));
  const auto& A=Caps.Vertices[Caps.Triangles[I]];const auto& B=Caps.Vertices[Caps.Triangles[I+1]];const auto& C=Caps.Vertices[Caps.Triangles[I+2]];
  Candidate.BoundaryCapArea+=FVector::CrossProduct(B-A,C-A).Size()*0.5;
 }
 if(!EHBStraightWallMesh150::Reveals(G,Source.Height,Source.Thickness,Source.Openings,Caps.Vertices,Caps.Triangles,Caps.Normals,Caps.UVs))return Fail(TEXT("StraightWallRevealUnionFailed"));
 for(const auto* Mesh:{&Candidate.Left,&Candidate.Right,&Candidate.Caps})
 {
  if(Mesh->Triangles.Num()%3||Mesh->Normals.Num()!=Mesh->Vertices.Num()||Mesh->UVs.Num()!=Mesh->Vertices.Num())return Fail(TEXT("InvalidStraightWallMeshOutput"));
  for(int32 I=0;I<Mesh->Vertices.Num();++I)if(Mesh->Vertices[I].ContainsNaN()||Mesh->Normals[I].ContainsNaN()||Mesh->UVs[I].ContainsNaN())return Fail(TEXT("InvalidStraightWallMeshOutput"));
  for(int32 I:Mesh->Triangles)if(!Mesh->Vertices.IsValidIndex(I))return Fail(TEXT("InvalidStraightWallMeshOutput"));
 }
 Out=MoveTemp(Candidate);Status=TEXT("Ready");return true;
}
