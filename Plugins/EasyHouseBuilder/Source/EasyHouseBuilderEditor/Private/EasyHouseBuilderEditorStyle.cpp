// Copyright Epic Games, Inc. All Rights Reserved.

#include "EasyHouseBuilderEditorStyle.h"

#include "Brushes/SlateImageBrush.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"

TSharedPtr<FSlateStyleSet> FEasyHouseBuilderEditorStyle::StyleSet;

void FEasyHouseBuilderEditorStyle::Register()
{
	// Slate Style 是全局注册资源，重复注册会导致名称冲突，因此先检查实例是否已存在。
	if (!StyleSet.IsValid())
	{
		StyleSet = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleSet);
	}
}

void FEasyHouseBuilderEditorStyle::Unregister()
{
	// 模块卸载时移除注册，避免热重载后旧 Brush 继续占用同名 Style。
	if (StyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet);
		StyleSet.Reset();
	}
}

FName FEasyHouseBuilderEditorStyle::GetStyleSetName()
{
	// 使用稳定英文名称作为 StyleSet ID，供 FSlateIcon 和 SImage 查询。
	static const FName StyleSetName(TEXT("EasyHouseBuilderEditorStyle"));
	return StyleSetName;
}

const ISlateStyle& FEasyHouseBuilderEditorStyle::Get()
{
	// 所有调用方都应在模块 StartupModule 注册 Style 后再读取 Brush。
	check(StyleSet.IsValid());
	return *StyleSet;
}

TSharedRef<FSlateStyleSet> FEasyHouseBuilderEditorStyle::Create()
{
	// 创建 StyleSet 并定位插件 Resources 目录，图标文件统一放在 Resources/Icons 下。
	TSharedRef<FSlateStyleSet> Style = MakeShared<FSlateStyleSet>(GetStyleSetName());

	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("EasyHouseBuilder"));
	const FString ResourcesDir = Plugin.IsValid()
		? FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"))
		: FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("EasyHouseBuilder/Resources"));

	Style->SetContentRoot(ResourcesDir);

	// 模式菜单使用 16/20 尺寸，工具按钮使用 24 尺寸，SVG 会自动缩放。
	const FVector2D Icon16x16(16.0f, 16.0f);
	const FVector2D Icon20x20(20.0f, 20.0f);
	const FVector2D Icon24x24(24.0f, 24.0f);

	// 编辑模式入口图标。
	Style->Set("EasyHouseBuilder.Mode", new FSlateVectorImageBrush(Style->RootToContentDir(TEXT("Icons/Mode"), TEXT(".svg")), Icon20x20));
	Style->Set("EasyHouseBuilder.Mode.Small", new FSlateVectorImageBrush(Style->RootToContentDir(TEXT("Icons/Mode"), TEXT(".svg")), Icon16x16));

	// 左侧工具栏图标，与工具面板枚举一一对应。
	Style->Set("EasyHouseBuilder.MeshSampling", new FSlateVectorImageBrush(Style->RootToContentDir(TEXT("Icons/MeshSampling"), TEXT(".svg")), Icon24x24));
	Style->Set("EasyHouseBuilder.BuildingSelection", new FSlateVectorImageBrush(Style->RootToContentDir(TEXT("Icons/BuildingSelection"), TEXT(".svg")), Icon24x24));
	Style->Set("EasyHouseBuilder.AICreation", new FSlateVectorImageBrush(Style->RootToContentDir(TEXT("Icons/AICreation"), TEXT(".svg")), Icon24x24));
	Style->Set("EasyHouseBuilder.FoundationAndFloor", new FSlateVectorImageBrush(Style->RootToContentDir(TEXT("Icons/FoundationAndFloor"), TEXT(".svg")), Icon24x24));
	Style->Set("EasyHouseBuilder.Walls", new FSlateVectorImageBrush(Style->RootToContentDir(TEXT("Icons/Walls"), TEXT(".svg")), Icon24x24));
	Style->Set("EasyHouseBuilder.DoorsAndWindows", new FSlateVectorImageBrush(Style->RootToContentDir(TEXT("Icons/DoorsAndWindows"), TEXT(".svg")), Icon24x24));
	Style->Set("EasyHouseBuilder.Railings", new FSlateVectorImageBrush(Style->RootToContentDir(TEXT("Icons/Railings"), TEXT(".svg")), Icon24x24));
	Style->Set("EasyHouseBuilder.Roof", new FSlateVectorImageBrush(Style->RootToContentDir(TEXT("Icons/Roof"), TEXT(".svg")), Icon24x24));
	Style->Set("EasyHouseBuilder.Floor", new FSlateVectorImageBrush(Style->RootToContentDir(TEXT("Icons/Floor"), TEXT(".svg")), Icon24x24));
	Style->Set("EasyHouseBuilder.Stairs", new FSlateVectorImageBrush(Style->RootToContentDir(TEXT("Icons/Stairs"), TEXT(".svg")), Icon24x24));

	return Style;
}
