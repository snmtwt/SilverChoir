#pragma once

#include "CoreMinimal.h"
#include "RmlUi/Core/FileInterface.h"
#include "RmlUi/Core/RenderInterface.h"
#include "RmlUi/Core/SystemInterface.h"
#include "Rendering/RenderingCommon.h"
#include "Rendering/SlateResourceHandle.h"
#include "H5UI_Types.h"

struct FGeometry;
class FSlateWindowElementList;
class UTexture;

struct FH5UI_GeneratedPseudoSelector
{
	FString Selector;
	bool bBefore = false;

	bool operator==(const FH5UI_GeneratedPseudoSelector& Other) const
	{
		return bBefore == Other.bBefore && Selector == Other.Selector;
	}
};

namespace H5UIPlugin
{
	/** Translate common browser CSS into RmlUi-compatible forms (gradients, images, noise props). */
	void NormalizeCssForRml(FString& Css, TArray<FH5UI_GeneratedPseudoSelector>* GeneratedPseudoSelectors = nullptr);
}

class FH5UI_SystemInterface final : public Rml::SystemInterface
{
public:
	virtual double GetElapsedTime() override;
	virtual void JoinPath(Rml::String& TranslatedPath, const Rml::String& DocumentPath, const Rml::String& Path) override;
	virtual bool LogMessage(Rml::Log::Type Type, const Rml::String& Message) override;
	virtual void SetClipboardText(const Rml::String& Text) override;
	virtual void GetClipboardText(Rml::String& Text) override;
};

class FH5UI_FileInterface final : public Rml::FileInterface
{
public:
	void SetPluginResourceRoot(const FString& InRoot);
	FString ResolvePath(const FString& Path) const;
	void BeginDocumentStyleCapture();
	TArray<FH5UI_GeneratedPseudoSelector> ConsumeGeneratedPseudoSelectors();

	virtual Rml::FileHandle Open(const Rml::String& Path) override;
	virtual void Close(Rml::FileHandle File) override;
	virtual size_t Read(void* Buffer, size_t Size, Rml::FileHandle File) override;
	virtual bool Seek(Rml::FileHandle File, long Offset, int Origin) override;
	virtual size_t Tell(Rml::FileHandle File) override;
	virtual size_t Length(Rml::FileHandle File) override;

private:
	struct FOpenFile
	{
		TUniquePtr<IFileHandle> DiskFile;
		TArray<uint8> Memory;
		int64 Offset = 0;
	};

	FString PluginResourceRoot;
	bool bCaptureGeneratedPseudoSelectors = false;
	TArray<FH5UI_GeneratedPseudoSelector> CapturedGeneratedPseudoSelectors;
};

class FH5UI_RenderInterface final : public Rml::RenderInterface
{
public:
	FH5UI_RenderInterface();
	virtual ~FH5UI_RenderInterface() override;

	void BeginPaint(const FGeometry& InGeometry, FSlateWindowElementList& InElementList, int32 InLayerId, float InCoordinateScale = 1.0f);
	FH5UI_PerformanceStats EndPaint(const FIntPoint& ViewSize);
	Rml::TextureHandle CreateExternalTexture(UTexture* Texture);

	virtual Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> Vertices, Rml::Span<const int> Indices) override;
	virtual void RenderGeometry(Rml::CompiledGeometryHandle Geometry, Rml::Vector2f Translation, Rml::TextureHandle Texture) override;
	virtual void ReleaseGeometry(Rml::CompiledGeometryHandle Geometry) override;
	virtual Rml::TextureHandle LoadTexture(Rml::Vector2i& TextureDimensions, const Rml::String& Source) override;
	virtual Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> Source, Rml::Vector2i SourceDimensions) override;
	virtual void ReleaseTexture(Rml::TextureHandle Texture) override;
	virtual void EnableScissorRegion(bool bEnable) override;
	virtual void SetScissorRegion(Rml::Rectanglei Region) override;
	virtual void EnableClipMask(bool bEnable) override;
	virtual void RenderToClipMask(
		Rml::ClipMaskOperation Operation,
		Rml::CompiledGeometryHandle Geometry,
		Rml::Vector2f Translation) override;
	virtual void SetTransform(const Rml::Matrix4f* Transform) override;
	virtual Rml::CompiledShaderHandle CompileShader(const Rml::String& Name, const Rml::Dictionary& Parameters) override;
	virtual void RenderShader(
		Rml::CompiledShaderHandle Shader,
		Rml::CompiledGeometryHandle Geometry,
		Rml::Vector2f Translation,
		Rml::TextureHandle Texture) override;
	virtual void ReleaseShader(Rml::CompiledShaderHandle Shader) override;

	void SetFileInterface(FH5UI_FileInterface* InFileInterface);

#if WITH_DEV_AUTOMATION_TESTS
	uint64 GetSlateVertexScratchGrowthCount() const { return SlateVertexScratchGrowthCount; }
#endif

private:
	struct FCompiledGeometry;
	struct FCompiledShader;
	struct FTextureData;
	struct FScopedSlateVertices;

#if WITH_DEV_AUTOMATION_TESTS
	friend class FH5UI_RenderScratchTest;
	uint64 SlateVertexScratchGrowthCount = 0;
#endif

	TArray<FSlateVertex> SlateVertexScratch;
	bool bSlateVertexScratchInUse = false;

	Rml::TextureHandle CreateTexture(const TArray<uint8>& PremultipliedBGRA, int32 Width, int32 Height);
	TOptional<FSlateRect> GetActiveLocalClipRect() const;
	void RefreshSlateClips();
	FSlateResourceHandle ResolveResourceHandle(FTextureData* TextureData) const;

	FH5UI_FileInterface* FileInterface = nullptr;
	const FGeometry* PaintGeometry = nullptr;
	FSlateWindowElementList* ElementList = nullptr;
	int32 LayerId = 0;
	float CoordinateScale = 1.0f;
	bool bScissorEnabled = false;
	bool bClipMaskEnabled = false;
	bool bSlateScissorClipPushed = false;
	bool bSlateMaskClipPushed = false;
	Rml::Rectanglei ScissorRegion;
	TOptional<FSlateRect> ClipMaskRect;
	TOptional<Rml::Matrix4f> CurrentTransform;
	uint64 TextureSerial = 0;
	FH5UI_PerformanceStats CurrentStats;
	double PaintStartSeconds = 0.0;
};
