// Copyright Epic Games, Inc. All Rights Reserved.

#include "Settings/EHBAISettings.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "EHBAISettings"

namespace
{
	static FString NormalizePathForConfig(const FString& InPath)
	{
		FString Path = FPaths::ConvertRelativePathToFull(InPath);
		FPaths::NormalizeFilename(Path);
		return Path;
	}

	static FString GetUserHomeDir()
	{
		FString HomeDir = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
		if (HomeDir.IsEmpty())
		{
			HomeDir = FPlatformMisc::GetEnvironmentVariable(TEXT("HOME"));
		}
		if (HomeDir.IsEmpty())
		{
			HomeDir = FPaths::ProjectDir();
		}
		return NormalizePathForConfig(HomeDir);
	}

	static FString FindLocalCodexExecutable()
	{
		const FString ExplicitPath = FPlatformMisc::GetEnvironmentVariable(TEXT("CODEX_CLI_PATH")).TrimStartAndEnd();
		if (!ExplicitPath.IsEmpty() && FPaths::FileExists(ExplicitPath))
		{
			return NormalizePathForConfig(ExplicitPath);
		}

		const FString LocalAppDataDir = FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA"));
		if (!LocalAppDataDir.IsEmpty())
		{
			TArray<FString> CodexExecutables;
			IFileManager::Get().FindFilesRecursive(
				CodexExecutables,
				*FPaths::Combine(LocalAppDataDir, TEXT("OpenAI"), TEXT("Codex"), TEXT("bin")),
				TEXT("codex.exe"),
				true,
				false);

			if (!CodexExecutables.IsEmpty())
			{
				CodexExecutables.Sort();
				return NormalizePathForConfig(CodexExecutables.Last());
			}
		}

		return TEXT("codex");
	}

	static FString TomlQuote(FString Value)
	{
		Value.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
		Value.ReplaceInline(TEXT("\""), TEXT("\\\""));
		return FString::Printf(TEXT("\"%s\""), *Value);
	}

	static FString JsonWritePretty(const TSharedRef<FJsonObject>& Object)
	{
		FString Text;
		TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Text);
		FJsonSerializer::Serialize(Object, Writer);
		return Text;
	}

	static bool SaveTextFile(const FString& FilePath, const FString& Text, FString& OutError)
	{
		const FString Directory = FPaths::GetPath(FilePath);
		if (!IFileManager::Get().MakeDirectory(*Directory, true))
		{
			OutError = FString::Printf(TEXT("无法创建目录：%s"), *Directory);
			return false;
		}

		if (!FFileHelper::SaveStringToFile(Text, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = FString::Printf(TEXT("无法写入文件：%s"), *FilePath);
			return false;
		}
		return true;
	}

	static void RemoveGeneratedTomlBlock(TArray<FString>& Lines)
	{
		const FString BeginMarker = TEXT("# BEGIN EHB MCP GENERATED");
		const FString EndMarker = TEXT("# END EHB MCP GENERATED");
		TArray<FString> FilteredLines;
		bool bSkipping = false;
		for (const FString& Line : Lines)
		{
			const FString Trimmed = Line.TrimStartAndEnd();
			if (Trimmed == BeginMarker)
			{
				bSkipping = true;
				continue;
			}
			if (bSkipping)
			{
				if (Trimmed == EndMarker)
				{
					bSkipping = false;
				}
				continue;
			}
			FilteredLines.Add(Line);
		}
		Lines = MoveTemp(FilteredLines);
	}

	static void RemoveExistingMCPServerTomlTable(TArray<FString>& Lines, const FString& ServerName)
	{
		const FString Header = FString::Printf(TEXT("[mcp_servers.%s]"), *ServerName);
		TArray<FString> FilteredLines;
		bool bSkipping = false;
		for (const FString& Line : Lines)
		{
			const FString Trimmed = Line.TrimStartAndEnd();
			if (!bSkipping && Trimmed == Header)
			{
				bSkipping = true;
				continue;
			}

			if (bSkipping && Trimmed.StartsWith(TEXT("[")))
			{
				bSkipping = false;
			}

			if (!bSkipping)
			{
				FilteredLines.Add(Line);
			}
		}
		Lines = MoveTemp(FilteredLines);
	}

	static bool WriteCodexConfig(
		const FString& ConfigPath,
		const FString& ServerName,
		const FString& NodeExecutable,
		const FString& ServerScriptPath,
		const FString& ProjectRoot,
		FString& OutError)
	{
		FString ExistingText;
		FFileHelper::LoadFileToString(ExistingText, *ConfigPath);

		TArray<FString> Lines;
		ExistingText.ParseIntoArrayLines(Lines, false);
		RemoveGeneratedTomlBlock(Lines);
		RemoveExistingMCPServerTomlTable(Lines, ServerName);

		const FString GeneratedBlock = FString::Printf(
			TEXT("# BEGIN EHB MCP GENERATED\n")
			TEXT("[mcp_servers.%s]\n")
			TEXT("command = %s\n")
			TEXT("args = [\n")
			TEXT("  %s,\n")
			TEXT("  \"--project\",\n")
			TEXT("  %s\n")
			TEXT("]\n")
			TEXT("cwd = %s\n")
			TEXT("startup_timeout_sec = 10\n")
			TEXT("tool_timeout_sec = 120\n")
			TEXT("# END EHB MCP GENERATED\n"),
			*ServerName,
			*TomlQuote(NodeExecutable),
			*TomlQuote(ServerScriptPath),
			*TomlQuote(ProjectRoot),
			*TomlQuote(ProjectRoot));

		FString NewText = FString::Join(Lines, TEXT("\n")).TrimEnd();
		if (!NewText.IsEmpty())
		{
			NewText += TEXT("\n\n");
		}
		NewText += GeneratedBlock;
		return SaveTextFile(ConfigPath, NewText, OutError);
	}

	static TSharedRef<FJsonObject> MakeMCPServerJsonObject(
		const FString& NodeExecutable,
		const FString& ServerScriptPath,
		const FString& ProjectRoot)
	{
		TSharedRef<FJsonObject> ServerObject = MakeShared<FJsonObject>();
		ServerObject->SetStringField(TEXT("command"), NodeExecutable);

		TArray<TSharedPtr<FJsonValue>> Args;
		Args.Add(MakeShared<FJsonValueString>(ServerScriptPath));
		Args.Add(MakeShared<FJsonValueString>(TEXT("--project")));
		Args.Add(MakeShared<FJsonValueString>(ProjectRoot));
		ServerObject->SetArrayField(TEXT("args"), Args);
		return ServerObject;
	}

	static bool WriteJsonMCPConfig(
		const FString& ConfigPath,
		const FString& ServerName,
		const FString& NodeExecutable,
		const FString& ServerScriptPath,
		const FString& ProjectRoot,
		FString& OutError)
	{
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		FString ExistingText;
		if (FFileHelper::LoadFileToString(ExistingText, *ConfigPath))
		{
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ExistingText);
			TSharedPtr<FJsonObject> ParsedRoot;
			if (FJsonSerializer::Deserialize(Reader, ParsedRoot) && ParsedRoot.IsValid())
			{
				Root = ParsedRoot;
			}
		}

		TSharedPtr<FJsonObject> Servers = MakeShared<FJsonObject>();
		const TSharedPtr<FJsonObject>* ExistingServers = nullptr;
		if (Root->TryGetObjectField(TEXT("mcpServers"), ExistingServers) && ExistingServers && ExistingServers->IsValid())
		{
			Servers = *ExistingServers;
		}

		Servers->SetObjectField(ServerName, MakeMCPServerJsonObject(NodeExecutable, ServerScriptPath, ProjectRoot));
		Root->SetObjectField(TEXT("mcpServers"), Servers);
		return SaveTextFile(ConfigPath, JsonWritePretty(Root.ToSharedRef()), OutError);
	}
}

UEHBAISettings::UEHBAISettings()
{
	MCPServerName = TEXT("bpt-unreal");
	NodeExecutable = TEXT("node");
	EmbeddedChatProvider = EEHBAIEmbeddedChatProvider::LocalCodex;
	EmbeddedChatEndpoint = TEXT("https://api.openai.com/v1/responses");
	EmbeddedChatModel = FString();
	LocalCodexExecutable = FindLocalCodexExecutable();
	ResolvedMCPServerScriptPath = GetMCPServerScriptPath();
	ResolvedBridgeRootDir = GetBridgeRootDir();
}

FName UEHBAISettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

FName UEHBAISettings::GetSectionName() const
{
	return TEXT("EasyHouseAI");
}

#if WITH_EDITOR
FText UEHBAISettings::GetSectionText() const
{
	return LOCTEXT("SectionText", "建筑 AI 配置");
}

FText UEHBAISettings::GetSectionDescription() const
{
	return LOCTEXT("SectionDescription", "配置建筑工具集的 MCP 接入和可选 UE 内置对话。MCP 用于外部 AI 工具读取/修改 UE 建筑；UE 内置对话用于编辑器页卡内直接文本交流。");
}

void UEHBAISettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	ResolvedMCPServerScriptPath = GetMCPServerScriptPath();
	ResolvedBridgeRootDir = GetBridgeRootDir();

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	const bool bShouldGenerate =
		bEnableBuildingAIMCP
		&& (bRegenerateClientConfigsNow
			|| (bAutoGenerateClientConfigs
				&& (PropertyName == GET_MEMBER_NAME_CHECKED(UEHBAISettings, bEnableBuildingAIMCP)
					|| PropertyName == GET_MEMBER_NAME_CHECKED(UEHBAISettings, bAutoGenerateClientConfigs)
					|| PropertyName == GET_MEMBER_NAME_CHECKED(UEHBAISettings, MCPServerName)
					|| PropertyName == GET_MEMBER_NAME_CHECKED(UEHBAISettings, NodeExecutable)
					|| PropertyName == GET_MEMBER_NAME_CHECKED(UEHBAISettings, bGenerateCodexUserConfig)
					|| PropertyName == GET_MEMBER_NAME_CHECKED(UEHBAISettings, bGenerateCodexProjectConfig)
					|| PropertyName == GET_MEMBER_NAME_CHECKED(UEHBAISettings, bGenerateCursorProjectConfig)
					|| PropertyName == GET_MEMBER_NAME_CHECKED(UEHBAISettings, bGenerateClaudeDesktopConfig))));

	if (bShouldGenerate)
	{
		FString Status;
		GenerateMCPClientConfigsAndSaveStatus(Status);
	}

	bRegenerateClientConfigsNow = false;

	SaveConfig();
}
#endif

bool UEHBAISettings::GenerateMCPClientConfigsAndSaveStatus(FString& OutStatus)
{
	ResolvedMCPServerScriptPath = GetMCPServerScriptPath();
	ResolvedBridgeRootDir = GetBridgeRootDir();

	const bool bSucceeded = GenerateMCPClientConfigs(OutStatus);
	LastGeneratedAt = FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S"));
	LastGeneratedStatus = OutStatus;
	SaveConfig();
	return bSucceeded;
}

bool UEHBAISettings::ShouldAutoGenerateMCPClientConfigs() const
{
	return bEnableBuildingAIMCP && bAutoGenerateClientConfigs;
}

bool UEHBAISettings::GenerateMCPClientConfigs(FString& OutStatus) const
{
	const FString ServerName = MCPServerName.TrimStartAndEnd().IsEmpty()
		? FString(TEXT("bpt-unreal"))
		: MCPServerName.TrimStartAndEnd();
	const FString NodeCommand = NodeExecutable.TrimStartAndEnd().IsEmpty()
		? FString(TEXT("node"))
		: NodeExecutable.TrimStartAndEnd();
	const FString ProjectRoot = NormalizePathForConfig(FPaths::ProjectDir());
	const FString ServerScriptPath = GetMCPServerScriptPath();

	TArray<FString> ResultLines;
	bool bAnyTarget = false;
	bool bAllSucceeded = true;

	auto AddResult = [&ResultLines, &bAllSucceeded](const FString& Label, const FString& Path, bool bSucceeded, const FString& Error)
	{
		if (bSucceeded)
		{
			ResultLines.Add(FString::Printf(TEXT("%s：已生成 %s"), *Label, *Path));
		}
		else
		{
			bAllSucceeded = false;
			ResultLines.Add(FString::Printf(TEXT("%s：失败 %s（%s）"), *Label, *Path, *Error));
		}
	};

	if (bGenerateCodexUserConfig)
	{
		bAnyTarget = true;
		FString Error;
		const FString Path = GetCodexUserConfigPath();
		if (Path.IsEmpty())
		{
			AddResult(TEXT("Codex 用户配置"), TEXT("~/.codex/config.toml"), false, TEXT("未找到 USERPROFILE 或 HOME 环境变量"));
		}
		else
		{
			AddResult(TEXT("Codex 用户配置"), Path, WriteCodexConfig(Path, ServerName, NodeCommand, ServerScriptPath, ProjectRoot, Error), Error);
		}
	}

	if (bGenerateCodexProjectConfig)
	{
		bAnyTarget = true;
		FString Error;
		const FString Path = GetCodexProjectConfigPath();
		AddResult(TEXT("Codex"), Path, WriteCodexConfig(Path, ServerName, NodeCommand, ServerScriptPath, ProjectRoot, Error), Error);
	}

	if (bGenerateCursorProjectConfig)
	{
		bAnyTarget = true;
		FString Error;
		const FString Path = GetCursorProjectConfigPath();
		AddResult(TEXT("Cursor"), Path, WriteJsonMCPConfig(Path, ServerName, NodeCommand, ServerScriptPath, ProjectRoot, Error), Error);
	}

	if (bGenerateClaudeDesktopConfig)
	{
		bAnyTarget = true;
		FString Error;
		const FString Path = GetClaudeDesktopConfigPath();
		if (Path.IsEmpty())
		{
			AddResult(TEXT("Claude Desktop"), TEXT("%APPDATA%/Claude/claude_desktop_config.json"), false, TEXT("未找到 APPDATA 环境变量"));
		}
		else
		{
			AddResult(TEXT("Claude Desktop"), Path, WriteJsonMCPConfig(Path, ServerName, NodeCommand, ServerScriptPath, ProjectRoot, Error), Error);
		}
	}

	if (!bAnyTarget)
	{
		OutStatus = TEXT("未选择任何自动生成目标。请至少启用 Codex 用户配置、Codex 项目配置、Cursor 或 Claude Desktop 中的一项。");
		return false;
	}

	OutStatus = FString::Join(ResultLines, TEXT("\n"));
	return bAllSucceeded;
}

FString UEHBAISettings::GetMCPServerScriptPath() const
{
	FString PluginBaseDir;
	if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("EasyHouseBuilder")))
	{
		PluginBaseDir = Plugin->GetBaseDir();
	}
	else
	{
		PluginBaseDir = FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("EasyHouseBuilder"));
	}
	return NormalizePathForConfig(FPaths::Combine(PluginBaseDir, TEXT("MCP"), TEXT("bpt-mcp-server.mjs")));
}

FString UEHBAISettings::GetBridgeRootDir() const
{
	return NormalizePathForConfig(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("EHB_MCP"), TEXT("Bridge")));
}

FString UEHBAISettings::GetCodexUserConfigPath() const
{
	FString CodexHomeDir = FPlatformMisc::GetEnvironmentVariable(TEXT("CODEX_HOME"));
	if (CodexHomeDir.IsEmpty())
	{
		CodexHomeDir = FPaths::Combine(GetUserHomeDir(), TEXT(".codex"));
	}

	if (CodexHomeDir.IsEmpty())
	{
		return FString();
	}

	return NormalizePathForConfig(FPaths::Combine(CodexHomeDir, TEXT("config.toml")));
}

FString UEHBAISettings::GetCodexProjectConfigPath() const
{
	return NormalizePathForConfig(FPaths::Combine(FPaths::ProjectDir(), TEXT(".codex"), TEXT("config.toml")));
}

FString UEHBAISettings::GetCursorProjectConfigPath() const
{
	return NormalizePathForConfig(FPaths::Combine(FPaths::ProjectDir(), TEXT(".cursor"), TEXT("mcp.json")));
}

FString UEHBAISettings::GetClaudeDesktopConfigPath() const
{
	const FString AppDataDir = FPlatformMisc::GetEnvironmentVariable(TEXT("APPDATA"));
	if (AppDataDir.IsEmpty())
	{
		return FString();
	}
	return NormalizePathForConfig(FPaths::Combine(AppDataDir, TEXT("Claude"), TEXT("claude_desktop_config.json")));
}

#undef LOCTEXT_NAMESPACE
