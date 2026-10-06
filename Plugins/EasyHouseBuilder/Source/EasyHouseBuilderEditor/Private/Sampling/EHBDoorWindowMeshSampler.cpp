// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sampling/EHBDoorWindowMeshSampler.h"

#include "Actors/EHB_DoorWindow.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/StaticMesh.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "MeshDescription.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"
#include "StaticMeshAttributes.h"

#define LOCTEXT_NAMESPACE "EHBDoorWindowMeshSampler"

namespace
{
	FString SanitizeAssetName(const FString& InName)
	{
		FString Sanitized = ObjectTools::SanitizeObjectName(InName.TrimStartAndEnd());
		return Sanitized.IsEmpty() ? TEXT("BP_DoorWindow") : Sanitized;
	}

	FString NormalizeAssetFolder(const FString& InFolder)
	{
		FString Folder = InFolder.TrimStartAndEnd();
		if (Folder.IsEmpty())
		{
			Folder = TEXT("/Game/EHB_DoorWindows");
		}

		Folder.ReplaceInline(TEXT("\\"), TEXT("/"));
		while (Folder.EndsWith(TEXT("/")))
		{
			Folder.LeftChopInline(1);
		}

		if (!Folder.StartsWith(TEXT("/Game")))
		{
			Folder = TEXT("/Game/EHB_DoorWindows");
		}

		return Folder;
	}

	bool GetMeshDescriptionForLOD(UStaticMesh* StaticMesh, int32 LODIndex, const FMeshDescription*& OutMeshDescription, FText& OutErrorMessage)
	{
		OutMeshDescription = nullptr;
		if (!StaticMesh)
		{
			OutErrorMessage = LOCTEXT("NoDoorWindowMesh", "门窗采样失败：没有选择静态网格体。");
			return false;
		}

		if (LODIndex < 0 || LODIndex >= StaticMesh->GetNumSourceModels())
		{
			OutErrorMessage = FText::Format(
				LOCTEXT("InvalidDoorWindowLOD", "门窗采样失败：LOD 索引 {0} 无效。当前网格体可用 LOD 范围是 0 到 {1}。"),
				FText::AsNumber(LODIndex),
				FText::AsNumber(FMath::Max(0, StaticMesh->GetNumSourceModels() - 1)));
			return false;
		}

		OutMeshDescription = StaticMesh->GetMeshDescription(LODIndex);
		if (!OutMeshDescription)
		{
			OutErrorMessage = LOCTEXT("NoDoorWindowMeshDescription", "门窗采样失败：选中的 LOD 没有可读取的 MeshDescription。请确认该 StaticMesh 保留了源模型数据，或尝试使用 LOD0。");
			return false;
		}

		return true;
	}
}

bool FEHBDoorWindowMeshSampler::AnalyzeMesh(UStaticMesh* StaticMesh, const FEHBDoorWindowMeshSamplingOptions& Options, FEHBDoorWindowMeshAnalysis& OutAnalysis)
{
	OutAnalysis = FEHBDoorWindowMeshAnalysis();

	FText ErrorMessage;
	const FMeshDescription* MeshDescription = nullptr;
	if (!GetMeshDescriptionForLOD(StaticMesh, Options.LODIndex, MeshDescription, ErrorMessage))
	{
		OutAnalysis.Message = ErrorMessage;
		return false;
	}

	FStaticMeshConstAttributes Attributes(*MeshDescription);
	const TVertexAttributesConstRef<FVector3f> VertexPositions = Attributes.GetVertexPositions();

	FBox Bounds(ForceInit);
	for (const FVertexID VertexID : MeshDescription->Vertices().GetElementIDs())
	{
		Bounds += FVector(VertexPositions[VertexID]);
	}

	OutAnalysis.SourceVertexCount = MeshDescription->Vertices().Num();
	OutAnalysis.SourceTriangleCount = MeshDescription->Triangles().Num();

	if (!Bounds.IsValid || OutAnalysis.SourceVertexCount <= 0 || OutAnalysis.SourceTriangleCount <= 0)
	{
		OutAnalysis.Message = LOCTEXT("DoorWindowEmptyMesh", "门窗检测失败：源静态网格体没有有效顶点或三角面。");
		return false;
	}

	if (OutAnalysis.SourceTriangleCount > Options.MaxTriangleCount)
	{
		OutAnalysis.Message = FText::Format(
			LOCTEXT("DoorWindowTooManyTriangles", "门窗检测失败：源三角面数量过多。当前 {0} 个，允许上限 {1} 个。"),
			FText::AsNumber(OutAnalysis.SourceTriangleCount),
			FText::AsNumber(Options.MaxTriangleCount));
		return false;
	}

	const FVector Size = Bounds.GetSize();
	const float HorizontalX = FMath::Abs(Size.X);
	const float HorizontalY = FMath::Abs(Size.Y);
	OutAnalysis.BoundsSize = Size;
	OutAnalysis.SourceBoundsMin = Bounds.Min;
	OutAnalysis.SourceBoundsMax = Bounds.Max;
	OutAnalysis.bWidthUsesSourceY = HorizontalY > HorizontalX;
	OutAnalysis.OpeningWidth = FMath::Max(HorizontalX, HorizontalY);
	OutAnalysis.OpeningThickness = FMath::Min(HorizontalX, HorizontalY);
	OutAnalysis.OpeningHeight = FMath::Abs(Size.Z);

	if (OutAnalysis.OpeningWidth < Options.MinOpeningWidth)
	{
		OutAnalysis.Message = FText::Format(
			LOCTEXT("DoorWindowWidthTooSmall", "门窗检测失败：洞口宽度过小。当前 {0} cm，最小需要 {1} cm。"),
			FText::AsNumber(OutAnalysis.OpeningWidth),
			FText::AsNumber(Options.MinOpeningWidth));
		return false;
	}

	if (OutAnalysis.OpeningHeight < Options.MinOpeningHeight)
	{
		OutAnalysis.Message = FText::Format(
			LOCTEXT("DoorWindowHeightTooSmall", "门窗检测失败：洞口高度过小。当前 {0} cm，最小需要 {1} cm。"),
			FText::AsNumber(OutAnalysis.OpeningHeight),
			FText::AsNumber(Options.MinOpeningHeight));
		return false;
	}

	if (OutAnalysis.OpeningThickness < Options.MinOpeningThickness)
	{
		OutAnalysis.Message = FText::Format(
			LOCTEXT("DoorWindowThicknessTooSmall", "门窗检测失败：厚度过小。当前 {0} cm，最小需要 {1} cm。"),
			FText::AsNumber(OutAnalysis.OpeningThickness),
			FText::AsNumber(Options.MinOpeningThickness));
		return false;
	}

	const float ThicknessToWidthRatio = OutAnalysis.OpeningWidth > KINDA_SMALL_NUMBER
		? OutAnalysis.OpeningThickness / OutAnalysis.OpeningWidth
		: 1.0f;
	if (ThicknessToWidthRatio > Options.MaxThicknessToWidthRatio)
	{
		OutAnalysis.Message = FText::Format(
			LOCTEXT("DoorWindowTooThick", "门窗检测失败：网格体不像较薄的门窗构件。当前厚宽比 {0}，允许上限 {1}。"),
			FText::AsNumber(ThicknessToWidthRatio),
			FText::AsNumber(Options.MaxThicknessToWidthRatio));
		return false;
	}

	OutAnalysis.bCanSample = true;
	OutAnalysis.Message = FText::Format(
		LOCTEXT("DoorWindowAnalysisSucceeded", "门窗检测通过：宽 {0} cm，厚 {1} cm，高 {2} cm。源顶点 {3} 个，源三角面 {4} 个。"),
		FText::AsNumber(OutAnalysis.OpeningWidth),
		FText::AsNumber(OutAnalysis.OpeningThickness),
		FText::AsNumber(OutAnalysis.OpeningHeight),
		FText::AsNumber(OutAnalysis.SourceVertexCount),
		FText::AsNumber(OutAnalysis.SourceTriangleCount));
	return true;
}

bool FEHBDoorWindowMeshSampler::CreateBlueprintFromStaticMesh(
	UStaticMesh* StaticMesh,
	const FEHBDoorWindowMeshSamplingOptions& SamplingOptions,
	const FEHBDoorWindowBlueprintCreationOptions& CreationOptions,
	FEHBDoorWindowBlueprintCreationResult& OutResult,
	FText& OutErrorMessage)
{
	OutResult = FEHBDoorWindowBlueprintCreationResult();

	FEHBDoorWindowMeshAnalysis Analysis;
	if (!AnalyzeMesh(StaticMesh, SamplingOptions, Analysis))
	{
		OutErrorMessage = Analysis.Message;
		return false;
	}

	UClass* ParentClass = CreationOptions.ParentClass ? *CreationOptions.ParentClass : AEHB_DoorWindow::StaticClass();
	if (!ParentClass || !ParentClass->IsChildOf(AEHB_DoorWindow::StaticClass()))
	{
		OutErrorMessage = LOCTEXT("InvalidDoorWindowParentClass", "门窗蓝图生成失败：请选择 AEHB_DoorWindow 或它的子类作为蓝图父类。");
		return false;
	}

	const FString AssetFolder = NormalizeAssetFolder(CreationOptions.AssetFolder);
	const FString AssetName = SanitizeAssetName(CreationOptions.AssetName);
	const FString PackageName = AssetFolder / AssetName;

	if (!FPackageName::IsValidLongPackageName(PackageName))
	{
		OutErrorMessage = FText::Format(
			LOCTEXT("InvalidDoorWindowPackageName", "门窗蓝图生成失败：资产路径 {0} 不是有效的 /Game 路径。"),
			FText::FromString(PackageName));
		return false;
	}

	if (FindObject<UObject>(nullptr, *PackageName) || LoadObject<UObject>(nullptr, *PackageName))
	{
		OutErrorMessage = FText::Format(
			LOCTEXT("DoorWindowAssetAlreadyExists", "门窗蓝图生成失败：资产 {0} 已存在。请换一个名称，避免覆盖已有资源。"),
			FText::FromString(PackageName));
		return false;
	}

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		OutErrorMessage = LOCTEXT("DoorWindowPackageFailed", "门窗蓝图生成失败：无法创建目标资产包。");
		return false;
	}

	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
		ParentClass,
		Package,
		FName(*AssetName),
		BPTYPE_Normal,
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass(),
		FName(TEXT("EHBDoorWindowMeshSampler")));

	if (!Blueprint)
	{
		OutErrorMessage = LOCTEXT("DoorWindowBlueprintFailed", "门窗蓝图生成失败：CreateBlueprint 返回空对象。");
		return false;
	}

	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	AEHB_DoorWindow* DefaultDoorWindow = Blueprint->GeneratedClass
		? Cast<AEHB_DoorWindow>(Blueprint->GeneratedClass->GetDefaultObject())
		: nullptr;
	if (!DefaultDoorWindow)
	{
		OutErrorMessage = LOCTEXT("DoorWindowCDOFailed", "门窗蓝图生成失败：无法取得 AEHB_DoorWindow 默认对象。");
		return false;
	}

	DefaultDoorWindow->Modify();
	DefaultDoorWindow->ConfigureFromStaticMesh(
		StaticMesh,
		CreationOptions.Kind,
		CreationOptions.Kind == EEHBDoorWindowElementKind::Door ? 0.0f : CreationOptions.SillHeight);

	Blueprint->Modify();
	Blueprint->MarkPackageDirty();
	Package->MarkPackageDirty();

	FAssetRegistryModule::AssetCreated(Blueprint);

	OutResult.Blueprint = Blueprint;
	OutResult.CreatedClass = Blueprint->GeneratedClass;
	OutResult.PackageName = PackageName;
	OutResult.Analysis = Analysis;
	return true;
}

#undef LOCTEXT_NAMESPACE
