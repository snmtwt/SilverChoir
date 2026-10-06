// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "EHBAISettings.generated.h"

UENUM()
enum class EEHBAIEmbeddedChatProvider : uint8
{
	HttpAPI UMETA(DisplayName = "远程 HTTP 接口"),
	LocalCodex UMETA(DisplayName = "本地 Codex")
};

UENUM()
enum class EEHBAIEmbeddedChatAuthMode : uint8
{
	BearerToken UMETA(DisplayName = "Bearer 令牌"),
	RawAuthorizationValue UMETA(DisplayName = "原始授权头值")
};

UENUM()
enum class EEHBAIEmbeddedChatRequestFormat : uint8
{
	ResponsesAPI UMETA(DisplayName = "Responses 接口"),
	ChatCompletions UMETA(DisplayName = "Chat Completions 接口")
};

/**
 * Editor-only AI settings for the MCP bridge and optional embedded chat.
 *
 * External AI tools should connect through the local MCP server and operate on
 * the editor bridge with preview/validate/commit semantics. The optional
 * embedded chat is only an editor-side text client.
 */
UCLASS(Config = Editor, DefaultConfig, DisplayName = "建筑 AI 配置")
class EASYHOUSEBUILDEREDITOR_API UEHBAISettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UEHBAISettings();

	virtual FName GetCategoryName() const override;
	virtual FName GetSectionName() const override;

#if WITH_EDITOR
	virtual FText GetSectionText() const override;
	virtual FText GetSectionDescription() const override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	bool GenerateMCPClientConfigs(FString& OutStatus) const;
	bool GenerateMCPClientConfigsAndSaveStatus(FString& OutStatus);
	bool ShouldAutoGenerateMCPClientConfigs() const;
	FString GetMCPServerScriptPath() const;
	FString GetBridgeRootDir() const;
	FString GetCodexUserConfigPath() const;
	FString GetCodexProjectConfigPath() const;
	FString GetCursorProjectConfigPath() const;
	FString GetClaudeDesktopConfigPath() const;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "MCP 接入", meta = (DisplayName = "启用建筑 AI MCP", ToolTip = "启用后，编辑器会启动本地 MCP 桥服务，并允许外部 AI 工具通过 MCP 读取和修改当前建筑。关闭后，桥服务会停止处理请求。"))
	bool bEnableBuildingAIMCP = false;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "MCP 接入", meta = (DisplayName = "启用时自动生成客户端配置", ToolTip = "打开“启用建筑 AI MCP”时，自动写入所选 MCP 客户端的配置文件。写入采用托管块或 JSON 合并，只会更新名为 bpt-unreal 的服务器配置。", EditCondition = "bEnableBuildingAIMCP"))
	bool bAutoGenerateClientConfigs = true;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "MCP 接入", meta = (DisplayName = "MCP 服务器名称", ToolTip = "写入 MCP 客户端配置时使用的服务器 ID。建议保持 bpt-unreal，AI 工具中会以这个名字显示。", EditCondition = "bEnableBuildingAIMCP"))
	FString MCPServerName;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "MCP 接入", meta = (DisplayName = "Node 可执行文件", ToolTip = "启动 MCP 服务的命令。默认使用 node，要求 node 已在系统 PATH 中。若你的 Node 不在 PATH，可填写完整 node.exe 路径。", EditCondition = "bEnableBuildingAIMCP"))
	FString NodeExecutable;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "UE 内置对话", meta = (DisplayName = "启用 UE 内置 AI 对话", ToolTip = "启用后，建筑模式里的 AI 创建页卡可以直接与 AI 对话。可选择远程 HTTP 接口模式，或本地 Codex 模式。隐藏系统规范会自动随请求发送，不会显示在聊天窗口。"))
	bool bEnableEmbeddedAIChat = false;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "UE 内置对话", meta = (DisplayName = "对话模式", ToolTip = "远程 HTTP 接口模式需要在 UE 中填写 API 密钥；本地 Codex 模式不需要 UE 保存密钥，会调用本机已登录的 Codex。", EditCondition = "bEnableEmbeddedAIChat"))
	EEHBAIEmbeddedChatProvider EmbeddedChatProvider = EEHBAIEmbeddedChatProvider::LocalCodex;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "UE 内置对话|远程接口", meta = (DisplayName = "请求格式", ToolTip = "选择 UE 内置对话使用的 HTTP 请求体格式。OpenAI 官方和 0011.ai 的 /v1/responses 使用 Responses 接口；传统 /v1/chat/completions 使用 Chat Completions 接口。", EditCondition = "bEnableEmbeddedAIChat && EmbeddedChatProvider == EEHBAIEmbeddedChatProvider::HttpAPI"))
	EEHBAIEmbeddedChatRequestFormat EmbeddedChatRequestFormat = EEHBAIEmbeddedChatRequestFormat::ResponsesAPI;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "UE 内置对话|远程接口", meta = (DisplayName = "AI 接口地址", ToolTip = "UE 内置对话请求的 HTTP 地址。例如 OpenAI: https://api.openai.com/v1/responses；0011.ai 示例: https://aicoding.0011.ai/v1/responses。", EditCondition = "bEnableEmbeddedAIChat && EmbeddedChatProvider == EEHBAIEmbeddedChatProvider::HttpAPI"))
	FString EmbeddedChatEndpoint;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "UE 内置对话|远程接口", meta = (DisplayName = "API 密钥", ToolTip = "只用于远程 HTTP 接口模式。该字段会写入本机/项目编辑器配置，请不要提交到公共仓库。本地 Codex 模式不需要填写。", PasswordField = "true", EditCondition = "bEnableEmbeddedAIChat && EmbeddedChatProvider == EEHBAIEmbeddedChatProvider::HttpAPI"))
	FString EmbeddedChatAPIKey;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "UE 内置对话|远程接口", meta = (DisplayName = "授权头模式", ToolTip = "OpenAI 官方通常使用 Bearer 令牌；部分兼容服务示例会要求授权头直接填写原始密钥。", EditCondition = "bEnableEmbeddedAIChat && EmbeddedChatProvider == EEHBAIEmbeddedChatProvider::HttpAPI"))
	EEHBAIEmbeddedChatAuthMode EmbeddedChatAuthMode = EEHBAIEmbeddedChatAuthMode::BearerToken;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "UE 内置对话", meta = (DisplayName = "模型名称", ToolTip = "UE 内置对话使用的模型名称。远程 HTTP 接口模式填服务商模型 ID；本地 Codex 模式填 Codex 支持的模型，留空则使用 Codex 默认配置。", EditCondition = "bEnableEmbeddedAIChat"))
	FString EmbeddedChatModel;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "UE 内置对话|远程接口", meta = (DisplayName = "最大输出令牌数", ToolTip = "限制远程 HTTP 接口模式单次响应的最大输出长度。本地 Codex 模式不使用该参数。", ClampMin = "128", ClampMax = "32768", UIMin = "512", UIMax = "8192", EditCondition = "bEnableEmbeddedAIChat && EmbeddedChatProvider == EEHBAIEmbeddedChatProvider::HttpAPI"))
	int32 EmbeddedChatMaxOutputTokens = 4096;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "UE 内置对话", meta = (DisplayName = "超时时间（秒）", ToolTip = "UE 内置对话等待 AI 响应的超时时间。远程 HTTP 接口会直接使用该值；本地 Codex 至少会等待 300 秒，因为它可能需要通过 MCP 读取和修改 UE 场景。", ClampMin = "5", ClampMax = "900", UIMin = "15", UIMax = "300", EditCondition = "bEnableEmbeddedAIChat"))
	int32 EmbeddedChatTimeoutSeconds = 90;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "UE 内置对话|本地 Codex", meta = (DisplayName = "Codex 可执行文件", ToolTip = "本地 Codex 模式启动 codex mcp-server 使用的 codex.exe 路径。通常会自动探测；如果失败，可填写完整 codex.exe 路径。", EditCondition = "bEnableEmbeddedAIChat && EmbeddedChatProvider == EEHBAIEmbeddedChatProvider::LocalCodex"))
	FString LocalCodexExecutable;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "自动生成目标", meta = (DisplayName = "生成 Codex 用户配置（推荐）", ToolTip = "生成或更新当前用户的 ~/.codex/config.toml。Codex app 读取用户级 MCP 配置最稳定；写入采用 BEGIN/END 托管块，只会更新当前 MCP 服务器配置。", EditCondition = "bEnableBuildingAIMCP"))
	bool bGenerateCodexUserConfig = true;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "自动生成目标", meta = (DisplayName = "生成 Codex 项目配置", ToolTip = "生成或更新当前工程下的 .codex/config.toml。Codex 需要信任该工程后才会加载项目级 MCP 配置；用户级配置已足够时可关闭。", EditCondition = "bEnableBuildingAIMCP"))
	bool bGenerateCodexProjectConfig = true;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "自动生成目标", meta = (DisplayName = "生成 Cursor 项目配置", ToolTip = "生成或更新当前工程下的 .cursor/mcp.json。Cursor 的 Tools & MCP 页面会读取该项目配置。", EditCondition = "bEnableBuildingAIMCP"))
	bool bGenerateCursorProjectConfig = true;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "自动生成目标", meta = (DisplayName = "生成 Claude Desktop 用户配置", ToolTip = "生成或更新 %APPDATA%/Claude/claude_desktop_config.json。该操作会写入用户级配置；修改后通常需要重启 Claude Desktop。", EditCondition = "bEnableBuildingAIMCP"))
	bool bGenerateClaudeDesktopConfig = false;

	UPROPERTY(EditAnywhere, Transient, Category = "自动生成目标", meta = (DisplayName = "立即重新生成配置", ToolTip = "勾选后会立刻按当前设置重新生成 MCP 客户端配置，并自动恢复为未勾选状态。", EditCondition = "bEnableBuildingAIMCP"))
	bool bRegenerateClientConfigsNow = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "生成结果", meta = (DisplayName = "MCP 服务脚本路径", ToolTip = "UE 将写入客户端配置的本地 MCP 服务脚本路径。"))
	FString ResolvedMCPServerScriptPath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "生成结果", meta = (DisplayName = "桥服务目录", ToolTip = "UE 编辑器和 MCP 服务交换 JSON 请求/响应的本地目录。"))
	FString ResolvedBridgeRootDir;

	UPROPERTY(Config, VisibleAnywhere, BlueprintReadOnly, Category = "生成结果", meta = (DisplayName = "最近生成时间", ToolTip = "最近一次自动生成或手动重新生成 MCP 客户端配置的本地时间。"))
	FString LastGeneratedAt;

	UPROPERTY(Config, VisibleAnywhere, BlueprintReadOnly, Category = "生成结果", meta = (DisplayName = "最近生成结果", ToolTip = "最近一次生成 MCP 客户端配置的结果摘要。若失败，会在这里显示失败原因。", MultiLine = "true"))
	FString LastGeneratedStatus;
};
