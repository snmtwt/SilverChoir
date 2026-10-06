#include "GridStrategyMapSystem/Display3D/GSMBoardMeshComponent.h"

#include "GridStrategyMapSystem/Display3D/GSMMap3D.h"
#include "Materials/MaterialInterface.h"

UGSMBoardMeshComponent::UGSMBoardMeshComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;

	bUseAsyncCooking = true;
	SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	SetCastShadow(true);

	UpdateCachedGrooveMetrics();
}

void UGSMBoardMeshComponent::OnRegister()
{
	Super::OnRegister();
	RebuildBoardMesh();
}

#if WITH_EDITOR
void UGSMBoardMeshComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (bAutoRebuildInEditor)
	{
		RebuildBoardMesh();
	}
}
#endif

void UGSMBoardMeshComponent::RebuildBoardMesh()
{
	UpdateCachedGrooveMetrics();

	ClearAllMeshSections();
	SetCollisionEnabled(bGenerateCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	bUseComplexAsSimpleCollision = bBoardUseComplexAsSimpleCollision;

	if (BoardMaterial)
	{
		SetMaterial(0, BoardMaterial);
	}
	if (BoardSideMaterial || BoardMaterial)
	{
		SetMaterial(1, BoardSideMaterial ? BoardSideMaterial : BoardMaterial);
	}
	if (GrooveFloorMaterial || BoardMaterial)
	{
		SetMaterial(2, GrooveFloorMaterial ? GrooveFloorMaterial : BoardMaterial);
	}

	struct FBoardMeshSectionData
	{
		TArray<FVector> Vertices;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		TArray<FVector2D> UV0;
		TArray<FLinearColor> VertexColors;
	};

	FBoardMeshSectionData TopSection;
	FBoardMeshSectionData SideSection;
	FBoardMeshSectionData GrooveFloorSection;

	const float HalfInteriorWidth = GeneratedGrooveSize.X * 0.5f;
	const float HalfInteriorHeight = GeneratedGrooveSize.Y * 0.5f;

	const float InnerMinX = -HalfInteriorWidth;
	const float InnerMaxX = HalfInteriorWidth;
	const float InnerMinY = -HalfInteriorHeight;
	const float InnerMaxY = HalfInteriorHeight;

	const float OuterMinX = InnerMinX - FMath::Max(0.0f, LeftBorderWidth);
	const float OuterMaxX = InnerMaxX + FMath::Max(0.0f, RightBorderWidth);
	const float OuterMinY = InnerMinY - FMath::Max(0.0f, BottomBorderWidth);
	const float OuterMaxY = InnerMaxY + FMath::Max(0.0f, TopBorderWidth);

	const float TopZ = 0.0f;
	const float GrooveFloorZ = -GeneratedGrooveDepth;
	const float BottomZ = -FMath::Max(0.1f, BoardThickness);

	// 顶部四条边框。上下边框覆盖完整宽度，左右边框只覆盖凹槽两侧，避免角落重叠。
	AddQuad(TopSection.Vertices, TopSection.Triangles, TopSection.Normals, TopSection.UV0, TopSection.VertexColors,
		FVector(OuterMinX, OuterMinY, TopZ),
		FVector(OuterMaxX, OuterMinY, TopZ),
		FVector(OuterMaxX, InnerMinY, TopZ),
		FVector(OuterMinX, InnerMinY, TopZ),
		FVector::UpVector);

	AddQuad(TopSection.Vertices, TopSection.Triangles, TopSection.Normals, TopSection.UV0, TopSection.VertexColors,
		FVector(OuterMinX, InnerMaxY, TopZ),
		FVector(OuterMaxX, InnerMaxY, TopZ),
		FVector(OuterMaxX, OuterMaxY, TopZ),
		FVector(OuterMinX, OuterMaxY, TopZ),
		FVector::UpVector);

	AddQuad(TopSection.Vertices, TopSection.Triangles, TopSection.Normals, TopSection.UV0, TopSection.VertexColors,
		FVector(OuterMinX, InnerMinY, TopZ),
		FVector(InnerMinX, InnerMinY, TopZ),
		FVector(InnerMinX, InnerMaxY, TopZ),
		FVector(OuterMinX, InnerMaxY, TopZ),
		FVector::UpVector);

	AddQuad(TopSection.Vertices, TopSection.Triangles, TopSection.Normals, TopSection.UV0, TopSection.VertexColors,
		FVector(InnerMaxX, InnerMinY, TopZ),
		FVector(OuterMaxX, InnerMinY, TopZ),
		FVector(OuterMaxX, InnerMaxY, TopZ),
		FVector(InnerMaxX, InnerMaxY, TopZ),
		FVector::UpVector);

	// 凹槽槽底。瓦片后续可以根据这个凹槽尺寸和槽底高度进行放置。
	AddQuad(GrooveFloorSection.Vertices, GrooveFloorSection.Triangles, GrooveFloorSection.Normals, GrooveFloorSection.UV0, GrooveFloorSection.VertexColors,
		FVector(InnerMinX, InnerMinY, GrooveFloorZ),
		FVector(InnerMaxX, InnerMinY, GrooveFloorZ),
		FVector(InnerMaxX, InnerMaxY, GrooveFloorZ),
		FVector(InnerMinX, InnerMaxY, GrooveFloorZ),
		FVector::UpVector);

	// 凹槽内侧壁。
	AddQuad(SideSection.Vertices, SideSection.Triangles, SideSection.Normals, SideSection.UV0, SideSection.VertexColors,
		FVector(InnerMinX, InnerMinY, TopZ),
		FVector(InnerMinX, InnerMaxY, TopZ),
		FVector(InnerMinX, InnerMaxY, GrooveFloorZ),
		FVector(InnerMinX, InnerMinY, GrooveFloorZ),
		FVector::XAxisVector);

	AddQuad(SideSection.Vertices, SideSection.Triangles, SideSection.Normals, SideSection.UV0, SideSection.VertexColors,
		FVector(InnerMaxX, InnerMaxY, TopZ),
		FVector(InnerMaxX, InnerMinY, TopZ),
		FVector(InnerMaxX, InnerMinY, GrooveFloorZ),
		FVector(InnerMaxX, InnerMaxY, GrooveFloorZ),
		-FVector::XAxisVector);

	AddQuad(SideSection.Vertices, SideSection.Triangles, SideSection.Normals, SideSection.UV0, SideSection.VertexColors,
		FVector(InnerMaxX, InnerMinY, TopZ),
		FVector(InnerMinX, InnerMinY, TopZ),
		FVector(InnerMinX, InnerMinY, GrooveFloorZ),
		FVector(InnerMaxX, InnerMinY, GrooveFloorZ),
		FVector::YAxisVector);

	AddQuad(SideSection.Vertices, SideSection.Triangles, SideSection.Normals, SideSection.UV0, SideSection.VertexColors,
		FVector(InnerMinX, InnerMaxY, TopZ),
		FVector(InnerMaxX, InnerMaxY, TopZ),
		FVector(InnerMaxX, InnerMaxY, GrooveFloorZ),
		FVector(InnerMinX, InnerMaxY, GrooveFloorZ),
		-FVector::YAxisVector);

	// 棋盘外侧壁。
	AddQuad(SideSection.Vertices, SideSection.Triangles, SideSection.Normals, SideSection.UV0, SideSection.VertexColors,
		FVector(OuterMinX, OuterMaxY, TopZ),
		FVector(OuterMinX, OuterMinY, TopZ),
		FVector(OuterMinX, OuterMinY, BottomZ),
		FVector(OuterMinX, OuterMaxY, BottomZ),
		-FVector::XAxisVector);

	AddQuad(SideSection.Vertices, SideSection.Triangles, SideSection.Normals, SideSection.UV0, SideSection.VertexColors,
		FVector(OuterMaxX, OuterMinY, TopZ),
		FVector(OuterMaxX, OuterMaxY, TopZ),
		FVector(OuterMaxX, OuterMaxY, BottomZ),
		FVector(OuterMaxX, OuterMinY, BottomZ),
		FVector::XAxisVector);

	AddQuad(SideSection.Vertices, SideSection.Triangles, SideSection.Normals, SideSection.UV0, SideSection.VertexColors,
		FVector(OuterMinX, OuterMinY, TopZ),
		FVector(OuterMaxX, OuterMinY, TopZ),
		FVector(OuterMaxX, OuterMinY, BottomZ),
		FVector(OuterMinX, OuterMinY, BottomZ),
		-FVector::YAxisVector);

	AddQuad(SideSection.Vertices, SideSection.Triangles, SideSection.Normals, SideSection.UV0, SideSection.VertexColors,
		FVector(OuterMaxX, OuterMaxY, TopZ),
		FVector(OuterMinX, OuterMaxY, TopZ),
		FVector(OuterMinX, OuterMaxY, BottomZ),
		FVector(OuterMaxX, OuterMaxY, BottomZ),
		FVector::YAxisVector);

	// 底面主要用于让网格封闭，方便碰撞和后续检查。
	AddQuad(SideSection.Vertices, SideSection.Triangles, SideSection.Normals, SideSection.UV0, SideSection.VertexColors,
		FVector(OuterMinX, OuterMaxY, BottomZ),
		FVector(OuterMaxX, OuterMaxY, BottomZ),
		FVector(OuterMaxX, OuterMinY, BottomZ),
		FVector(OuterMinX, OuterMinY, BottomZ),
		-FVector::UpVector);

	const auto CreateBoardMeshSection = [this](int32 SectionIndex, const FBoardMeshSectionData& SectionData)
	{
		TArray<FProcMeshTangent> Tangents;
		CreateMeshSection_LinearColor(
			SectionIndex,
			SectionData.Vertices,
			SectionData.Triangles,
			SectionData.Normals,
			SectionData.UV0,
			SectionData.VertexColors,
			Tangents,
			bGenerateCollision
		);
	};

	CreateBoardMeshSection(0, TopSection);
	CreateBoardMeshSection(1, SideSection);
	CreateBoardMeshSection(2, GrooveFloorSection);
	NotifyOwnerBoardMeshChanged();
	OnBoardMeshRebuilt.Broadcast(this);
}

void UGSMBoardMeshComponent::ClearBoardMesh()
{
	ClearAllMeshSections();
	NotifyOwnerBoardMeshChanged();
	OnBoardMeshRebuilt.Broadcast(this);
}

bool UGSMBoardMeshComponent::IsLocalLocationInsideGroove(const FVector& LocalLocation) const
{
	return GeneratedLocalGrooveBounds.ContainsPoint(FVector2D(LocalLocation.X, LocalLocation.Y));
}

FVector UGSMBoardMeshComponent::GetLocalGrooveFloorCenter() const
{
	return FVector(0.0, 0.0, -GeneratedGrooveDepth);
}

void UGSMBoardMeshComponent::UpdateCachedGrooveMetrics()
{
	const float SafeInteriorWidth = FMath::Max(1.0f, InteriorWidth);
	const float SafeInteriorHeight = FMath::Max(1.0f, InteriorHeight);
	const float SafeBoardThickness = FMath::Max(0.1f, BoardThickness);

	GeneratedGrooveSize = FVector2D(SafeInteriorWidth, SafeInteriorHeight);
	GeneratedBoardSize = FVector2D(
		SafeInteriorWidth + FMath::Max(0.0f, LeftBorderWidth) + FMath::Max(0.0f, RightBorderWidth),
		SafeInteriorHeight + FMath::Max(0.0f, BottomBorderWidth) + FMath::Max(0.0f, TopBorderWidth)
	);
	GeneratedGrooveDepth = FMath::Clamp(GrooveDepth, 0.0f, SafeBoardThickness);

	const float HalfInteriorWidth = SafeInteriorWidth * 0.5f;
	const float HalfInteriorHeight = SafeInteriorHeight * 0.5f;
	const float InnerMinX = -HalfInteriorWidth;
	const float InnerMaxX = HalfInteriorWidth;
	const float InnerMinY = -HalfInteriorHeight;
	const float InnerMaxY = HalfInteriorHeight;
	const float OuterMinX = InnerMinX - FMath::Max(0.0f, LeftBorderWidth);
	const float OuterMaxX = InnerMaxX + FMath::Max(0.0f, RightBorderWidth);
	const float OuterMinY = InnerMinY - FMath::Max(0.0f, BottomBorderWidth);
	const float OuterMaxY = InnerMaxY + FMath::Max(0.0f, TopBorderWidth);

	GeneratedLocalGrooveBounds.CornerA = FVector2D(InnerMinX, InnerMinY);
	GeneratedLocalGrooveBounds.CornerB = FVector2D(InnerMaxX, InnerMinY);
	GeneratedLocalGrooveBounds.CornerC = FVector2D(InnerMaxX, InnerMaxY);
	GeneratedLocalGrooveBounds.CornerD = FVector2D(InnerMinX, InnerMaxY);

	GeneratedLocalBoardBounds.CornerA = FVector2D(OuterMinX, OuterMinY);
	GeneratedLocalBoardBounds.CornerB = FVector2D(OuterMaxX, OuterMinY);
	GeneratedLocalBoardBounds.CornerC = FVector2D(OuterMaxX, OuterMaxY);
	GeneratedLocalBoardBounds.CornerD = FVector2D(OuterMinX, OuterMaxY);
}

void UGSMBoardMeshComponent::NotifyOwnerBoardMeshChanged()
{
	if (AGSMMap3D* OwningMap = Cast<AGSMMap3D>(GetOwner()))
	{
		OwningMap->HandleBoardMeshChanged();
	}
}

void UGSMBoardMeshComponent::AddQuad(
	TArray<FVector>& Vertices,
	TArray<int32>& Triangles,
	TArray<FVector>& Normals,
	TArray<FVector2D>& UV0,
	TArray<FLinearColor>& VertexColors,
	const FVector& A,
	const FVector& B,
	const FVector& C,
	const FVector& D,
	const FVector& DesiredNormal
)
{
	const FVector CalculatedNormal = FVector::CrossProduct(B - A, C - A);
	if (CalculatedNormal.SizeSquared() <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const FVector SafeDesiredNormal = DesiredNormal.GetSafeNormal();
	const bool bNeedsFlip = FVector::DotProduct(CalculatedNormal.GetSafeNormal(), SafeDesiredNormal) < 0.0;
	const FVector QuadVertices[4] = { A, B, C, D };

	const int32 BaseIndex = Vertices.Num();
	if (bNeedsFlip)
	{
		Vertices.Add(QuadVertices[0]);
		Vertices.Add(QuadVertices[3]);
		Vertices.Add(QuadVertices[2]);
		Vertices.Add(QuadVertices[1]);
	}
	else
	{
		Vertices.Add(QuadVertices[0]);
		Vertices.Add(QuadVertices[1]);
		Vertices.Add(QuadVertices[2]);
		Vertices.Add(QuadVertices[3]);
	}

	Triangles.Add(BaseIndex);
	Triangles.Add(BaseIndex + 2);
	Triangles.Add(BaseIndex + 1);
	Triangles.Add(BaseIndex);
	Triangles.Add(BaseIndex + 3);
	Triangles.Add(BaseIndex + 2);

	for (int32 VertexIndex = 0; VertexIndex < 4; ++VertexIndex)
	{
		Normals.Add(SafeDesiredNormal);
		VertexColors.Add(FLinearColor::White);
	}

	UV0.Add(FVector2D(0.0, 0.0));
	UV0.Add(FVector2D(1.0, 0.0));
	UV0.Add(FVector2D(1.0, 1.0));
	UV0.Add(FVector2D(0.0, 1.0));
}
