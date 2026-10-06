#include "Map/BaseMap/RegionBlock.h"
#include "Map/BaseMap/RegionNameWidget.h"
#include "Map/BaseMap/RegionInteraction.h"
#include "RegionBlockGeometry.h"
#include "Components/SplineComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/EHBGeneratedMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"
#include "InputCoreTypes.h"

ARegionBlock::ARegionBlock()
{
	PrimaryActorTick.bCanEverTick=false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Boundary=CreateDefaultSubobject<USplineComponent>(TEXT("Boundary")); Boundary->SetupAttachment(RootComponent);
	Boundary->SetClosedLoop(true); Boundary->SetHiddenInGame(true);
	Boundary->ClearSplinePoints(false);
	for (FVector P : {FVector(-500,-500,0),FVector(500,-500,0),FVector(500,500,0),FVector(-500,500,0)}) { Boundary->AddSplinePoint(P,ESplineCoordinateSpace::Local,false); }
	for (int32 I=0; I<4; ++I) { Boundary->SetSplinePointType(I,ESplinePointType::Linear,false); } Boundary->UpdateSpline();
	VolumeMesh=CreateDefaultSubobject<UEHBGeneratedMeshComponent>(TEXT("VolumeMesh")); VolumeMesh->SetupAttachment(RootComponent);
	VolumeMesh->bUseComplexAsSimpleCollision=true; VolumeMesh->bUseAsyncCooking=false;
	VolumeMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	VolumeMesh->SetCollisionResponseToAllChannels(ECR_Ignore); VolumeMesh->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
	VolumeMesh->SetCollisionResponseToChannel(RegionInteraction::TraceChannel,ECR_Block);
	VolumeMesh->SetGenerateOverlapEvents(false); VolumeMesh->SetCastShadow(false);
	NameWidget=CreateDefaultSubobject<UWidgetComponent>(TEXT("NameWidget")); NameWidget->SetupAttachment(RootComponent);
	NameWidget->SetWidgetSpace(EWidgetSpace::World); NameWidget->SetRelativeRotation(FRotator(90,180,0));
	NameWidget->SetBlendMode(EWidgetBlendMode::Transparent);
	NameWidget->SetTranslucentSortPriority(1);
	NameWidget->SetCollisionEnabled(ECollisionEnabled::NoCollision); NameWidget->SetGenerateOverlapEvents(false);
	NameWidget->SetCastShadow(false); NameWidget->SetTwoSided(false); NameWidget->SetPivot(FVector2D(.5,.5));
	NameWidgetClass=URegionNameWidget::StaticClass();
	NameMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/System/Map/BaseMap/Materials/M_3DText.M_3DText")));
	RegionName=FText::FromString(TEXT("未命名区域"));
	RegionMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/System/Map/BaseMap/Materials/M_RegionBlock.M_RegionBlock")));
}
void ARegionBlock::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (!HasActorBegunPlay()) { bShown=bInitiallyShown; }
	if (bAutoRebuild) { RebuildVolume(); }
	else { ConfigureAppearance(); SetRegionShown(bShown); }
}
void ARegionBlock::GenerateRectangle()
{
	if (!FMath::IsFinite(RectangleWidth) || !FMath::IsFinite(RectangleLength) || RectangleWidth<=0 || RectangleLength<0)
	{ LastBuildError=FText::FromString(TEXT("矩形宽度必须为正数，长度不能为负数。")); return; }
	Modify(); Boundary->Modify();
	const double X=RectangleWidth*.5, Y=(RectangleLength>0 ? RectangleLength : RectangleWidth)*.5;
	Boundary->ClearSplinePoints(false);
	for (FVector P : {FVector(-X,-Y,0),FVector(X,-Y,0),FVector(X,Y,0),FVector(-X,Y,0)}) { Boundary->AddSplinePoint(P,ESplineCoordinateSpace::Local,false); }
	for (int32 I=0; I<4; ++I) { Boundary->SetSplinePointType(I,ESplinePointType::Linear,false); }
	Boundary->SetClosedLoop(true,false); Boundary->UpdateSpline();
	RebuildVolume();
}
bool ARegionBlock::RebuildVolume()
{
	TArray<FVector2D> Points; LastBuildError=FText();
	if (!Boundary->IsClosedLoop()) { LastBuildError=FText::FromString(TEXT("请先将 Boundary 曲线设置为闭合。")); }
	else if (!FMath::IsFinite(CurveSampleSpacing) || CurveSampleSpacing<1) { LastBuildError=FText::FromString(TEXT("曲线采样间距必须至少为 1 cm。")); }
	else
	{
		const int32 Count=Boundary->GetNumberOfSplinePoints();
		for (int32 I=0; I<Count; ++I)
		{
			const double Start=Boundary->GetDistanceAlongSplineAtSplinePoint(I);
			const double End=I+1<Count ? Boundary->GetDistanceAlongSplineAtSplinePoint(I+1) : Boundary->GetSplineLength();
			const int32 Samples=Boundary->GetSplinePointType(I)==ESplinePointType::Linear ? 1 : FMath::Clamp(FMath::CeilToInt((End-Start)/CurveSampleSpacing),1,513);
			if (Points.Num()+Samples>512) { LastBuildError=FText::FromString(TEXT("采样点超过 512 个，请增大曲线采样间距。")); break; }
			for (int32 J=0; J<Samples; ++J)
			{
				const FVector P=(J==0 ? Boundary->GetLocationAtSplinePoint(I,ESplineCoordinateSpace::Local) : Boundary->GetLocationAtDistanceAlongSpline(Start+(End-Start)*J/Samples,ESplineCoordinateSpace::Local));
				// 曲线按局部 XY 平面投影；体积底面统一为局部 Z=0。
				Points.Add(FVector2D(P.X,P.Y));
			}
		}
	}
	RegionBlockGeometry::FMesh Mesh;
	bHasValidMesh=LastBuildError.IsEmpty() && RegionBlockGeometry::Build(Points,Height,Mesh,LastBuildError);
	if (bHasValidMesh)
	{
		VolumeMesh->CreateMeshSection_LinearColor(0,Mesh.Vertices,Mesh.Indices,Mesh.Normals,Mesh.UVs,TArray<FLinearColor>(),TArray<FEHBMeshTangent>(),true);
		NameWidget->SetRelativeLocation(Mesh.LabelPosition+FVector(0,0,FMath::Max(.1f,NameTopOffset)));
	}
	else { VolumeMesh->ClearAllMeshSections(); }
	ConfigureAppearance(); SetRegionShown(bShown); return bHasValidMesh;
}
void ARegionBlock::ConfigureAppearance()
{
	if (auto* Material=RegionMaterial.LoadSynchronous())
	{
		if (!DynamicMaterial || DynamicMaterial->Parent!=Material) { DynamicMaterial=UMaterialInstanceDynamic::Create(Material,this); }
		VolumeMesh->SetMaterial(0,DynamicMaterial);
	}
	NameWidget->SetWidgetClass(NameWidgetClass);
	// Only the exact native label is known to be static. Designer subclasses may run
	// animations or update their own content without passing through UpdateAppearance.
	const bool bStaticName = NameWidgetClass == URegionNameWidget::StaticClass() && !bContinuouslyUpdateName;
	NameWidget->SetManuallyRedraw(bStaticName);
	NameWidget->SetTickMode(bStaticName ? ETickMode::Disabled : ETickMode::Enabled);
	if (auto* Material=NameMaterial.LoadSynchronous()) { NameWidget->SetMaterial(0,Material); }
	NameWidget->SetDrawSize(FVector2D(FMath::Clamp(NameDrawSize.X,32.,4096.),FMath::Clamp(NameDrawSize.Y,32.,4096.)));
	NameWidget->SetRelativeScale3D(FVector(FMath::Max(.01f,NameScale)));
	NameWidget->InitWidget(); UpdateAppearance();
}
void ARegionBlock::BeginPlay()
{
	Super::BeginPlay();
	VolumeMesh->OnBeginCursorOver.AddDynamic(this,&ThisClass::CursorEntered);
	VolumeMesh->OnEndCursorOver.AddDynamic(this,&ThisClass::CursorLeft);
	VolumeMesh->OnClicked.AddDynamic(this,&ThisClass::Clicked);
	bShown=bInitiallyShown;
	RebuildVolume();
}
void ARegionBlock::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorldTimerManager().ClearTimer(ClickTimer);
	VolumeMesh->OnBeginCursorOver.RemoveAll(this); VolumeMesh->OnEndCursorOver.RemoveAll(this); VolumeMesh->OnClicked.RemoveAll(this);
	Super::EndPlay(Reason);
}
void ARegionBlock::SetRegionShown(bool bInShown)
{
	bShown=bInShown;
	const bool Visible=bShown && bHasValidMesh;
	SetActorHiddenInGame(!Visible);
	VolumeMesh->SetVisibility(Visible); NameWidget->SetVisibility(Visible);
	VolumeMesh->SetCollisionEnabled(Visible ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	// Restore for existing Blueprint/level instances with serialized collision settings.
	VolumeMesh->SetCollisionResponseToChannel(RegionInteraction::TraceChannel,ECR_Block);
	if (Visible)
	{
		if (!NameWidget->GetManuallyRedraw()) { NameWidget->SetComponentTickEnabled(true); }
		// RequestRenderUpdate also wakes a disabled tick for exactly one render.
		NameWidget->RequestRenderUpdate();
	}
	if (!Visible)
	{
		bClickPulse=false;
		if (GetWorld()) { GetWorldTimerManager().ClearTimer(ClickTimer); }
		const bool WasHovered=bHovered; bHovered=false; UpdateAppearance();
		NameWidget->SetComponentTickEnabled(false);
		if (WasHovered) { OnRegionHoverChanged.Broadcast(this,false); }
	}
}
void ARegionBlock::SetRegionName(const FText& Name) { RegionName=Name; UpdateAppearance(); }
void ARegionBlock::CursorEntered(UPrimitiveComponent* Component)
{
	if (!IsRegionShown() || bHovered) { return; }
	bHovered=true; UpdateAppearance(); OnRegionHoverChanged.Broadcast(this,true);
}
void ARegionBlock::CursorLeft(UPrimitiveComponent* Component)
{
	if (!bHovered) { return; }
	bHovered=false; UpdateAppearance(); OnRegionHoverChanged.Broadcast(this,false);
}
void ARegionBlock::Clicked(UPrimitiveComponent* Component,FKey Button)
{
	if (!IsRegionShown() || Button!=EKeys::LeftMouseButton) { return; }
	bClickPulse=true; UpdateAppearance();
	GetWorldTimerManager().SetTimer(ClickTimer,this,&ThisClass::EndClickPulse,.15f,false);
	ReceiveRegionClicked();
	OnRegionClicked.Broadcast(this);
}
void ARegionBlock::ReceiveRegionClicked_Implementation() {}
void ARegionBlock::EndClickPulse() { bClickPulse=false; UpdateAppearance(); }
void ARegionBlock::UpdateAppearance()
{
	const bool Highlight=bHovered || bClickPulse;
	if (auto* NameMID=NameWidget->GetMaterialInstance())
	{
		NameMID->SetScalarParameterValue(TEXT("TextBrightness"), FMath::Max(0.f, Highlight ? NameHoverGlowStrength : NameGlowStrength));
	}
	const FLinearColor Color=Highlight ? HighlightColor : NormalColor;
	if (DynamicMaterial)
	{
		DynamicMaterial->SetVectorParameterValue(TEXT("RegionColor"),Color);
		DynamicMaterial->SetScalarParameterValue(TEXT("GlowStrength"),bClickPulse ? 6.f : (bHovered ? 3.f : 1.2f));
		DynamicMaterial->SetScalarParameterValue(TEXT("FillOpacity"),Highlight ? .18f : .055f);
	}
	if (auto* Label=Cast<URegionNameWidget>(NameWidget->GetUserWidgetObject())) { Label->UpdateRegionLabel(RegionName,Highlight ? HighlightColor : FLinearColor(.45f,.65f,.75f),Highlight); }
	if (IsRegionShown()) { NameWidget->RequestRenderUpdate(); }
	else { NameWidget->SetComponentTickEnabled(false); }
}
