#include "RegionTextMaterialCommandlet.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionEyeAdaptationInverse.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "MaterialEditingLibrary.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/SavePackage.h"

int32 URegionTextMaterialCommandlet::Main(const FString& Params)
{
	auto* Source=LoadObject<UMaterial>(nullptr,TEXT("/Engine/EngineMaterials/Widget3DPassThrough.Widget3DPassThrough"));
	auto* Target=LoadObject<UMaterial>(nullptr,TEXT("/Game/System/Map/BaseMap/Materials/M_3DText.M_3DText"));
	if (!Source || !Target) { return 1; }
	const FString File=FPackageName::LongPackageNameToFilename(Target->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
	const FString Backup=FPaths::ProjectSavedDir()/TEXT("RegionTextBackups")/FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"))/TEXT("M_3DText.uasset");
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Backup),true);
	if (IFileManager::Get().Copy(*Backup,*File)!=COPY_OK) { return 1; }
	UMaterialEditingLibrary::DeleteAllMaterialExpressions(Target);
	TMap<UMaterialExpression*,UMaterialExpression*> Copies;
	for (UMaterialExpression* Expr : Source->GetExpressions())
	{
		auto* Copy=UMaterialEditingLibrary::DuplicateMaterialExpression(Target,nullptr,Expr);
		Copies.Add(Expr,Copy);
	}
	for (auto& Pair : Copies)
	{
		for (int32 Index=0; FExpressionInput* Input=Pair.Value->GetInput(Index); ++Index)
		{
			if (Input->Expression) { Input->Expression=Copies.FindRef(Input->Expression); }
		}
	}
	auto* Data=Target->GetEditorOnlyData(); const auto* Original=Source->GetEditorOnlyData();
	Data->EmissiveColor=Original->EmissiveColor; Data->EmissiveColor.Expression=Copies.FindRef(Original->EmissiveColor.Expression);
	Data->Opacity=Original->Opacity; Data->Opacity.Expression=Copies.FindRef(Original->Opacity.Expression);
	Target->MaterialDomain=MD_Surface; Target->BlendMode=BLEND_Translucent; Target->SetShadingModel(MSM_Unlit); Target->TwoSided=false;
	auto* Exposure=CastChecked<UMaterialExpressionEyeAdaptationInverse>(UMaterialEditingLibrary::CreateMaterialExpression(Target,UMaterialExpressionEyeAdaptationInverse::StaticClass()));
	auto* Brightness=CastChecked<UMaterialExpressionScalarParameter>(UMaterialEditingLibrary::CreateMaterialExpression(Target,UMaterialExpressionScalarParameter::StaticClass()));
	Brightness->ParameterName=TEXT("TextBrightness"); Brightness->DefaultValue=3.f;
	auto* Multiply=CastChecked<UMaterialExpressionMultiply>(UMaterialEditingLibrary::CreateMaterialExpression(Target,UMaterialExpressionMultiply::StaticClass()));
	Multiply->A=Data->EmissiveColor; Multiply->B.Connect(0,Brightness);
	Exposure->LightValueInput.Connect(0,Multiply);
	Data->EmissiveColor.Connect(0,Exposure);
	Target->PostEditChange(); UMaterialEditingLibrary::RecompileMaterial(Target);
	FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone;
	if (!UPackage::SavePackage(Target->GetOutermost(),Target,*File,Args)) { return 1; }
	UE_LOG(LogTemp,Display,TEXT("REGION_TEXT_NATIVE_GRAPH_OK")); return 0;
}

