// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/ISlateStyle.h"

class FSlateStyleSet;

/**
 * 建筑工具编辑器模块的 Slate Style 管理器。
 * 所有自定义 SVG 图标都通过这个 Style 注册，编辑模式和工具按钮只引用 Brush 名称。
 */
class FEasyHouseBuilderEditorStyle
{
public:
	/** 注册插件 Style。模块启动时调用。 */
	static void Register();

	/** 注销插件 Style。模块关闭时调用。 */
	static void Unregister();

	/** 返回 StyleSet 名称，用于 FSlateIcon 或 Brush 查询。 */
	static FName GetStyleSetName();

	/** 返回已注册的 Style 实例。调用前必须先 Register。 */
	static const ISlateStyle& Get();

private:
	/** 创建并填充 StyleSet，包括图标根目录和所有 Brush。 */
	static TSharedRef<FSlateStyleSet> Create();

	/** 当前模块持有的 StyleSet 实例。 */
	static TSharedPtr<FSlateStyleSet> StyleSet;
};
