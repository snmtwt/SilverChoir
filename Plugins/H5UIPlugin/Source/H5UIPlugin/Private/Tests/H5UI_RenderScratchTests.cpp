#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Framework/Application/SlateApplication.h"
#include "H5UI_Interfaces.h"
#include "Layout/Geometry.h"
#include "Rendering/DrawElements.h"
#include "RmlUi/Core/Vertex.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FH5UI_RenderScratchTest,
	"H5UIPlugin.Rendering.ScratchReuseAndSubmissionCopies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FH5UI_RenderScratchTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("Slate is initialized"), FSlateApplication::IsInitialized()))
	{
		return false;
	}

	FH5UI_RenderInterface Renderer;
	Rml::Vertex Vertices[3] = {};
	Vertices[0].position = Rml::Vector2f(0.0f, 0.0f);
	Vertices[1].position = Rml::Vector2f(20.0f, 0.0f);
	Vertices[2].position = Rml::Vector2f(0.0f, 20.0f);
	for (Rml::Vertex& Vertex : Vertices)
	{
		Vertex.colour = Rml::ColourbPremultiplied(255, 255, 255, 255);
	}
	const int Indices[] = {0, 1, 2};
	const Rml::CompiledGeometryHandle GeometryHandle = Renderer.CompileGeometry(
		Rml::Span<const Rml::Vertex>(Vertices, UE_ARRAY_COUNT(Vertices)),
		Rml::Span<const int>(Indices, UE_ARRAY_COUNT(Indices)));
	if (!TestTrue(TEXT("Triangle compiles"), GeometryHandle != 0))
	{
		return false;
	}

	FSlateWindowElementList Elements(nullptr);
	const FGeometry Geometry = FGeometry::MakeRoot(FVector2f(100.0f, 100.0f), FSlateLayoutTransform());
	Renderer.BeginPaint(Geometry, Elements, 7);
	Renderer.RenderGeometry(GeometryHandle, Rml::Vector2f(0.0f, 0.0f), 0);
	const uint64 WarmGrowthCount = Renderer.GetSlateVertexScratchGrowthCount();
	TestEqual(TEXT("Warmup grows the scratch once"), WarmGrowthCount, uint64(1));
	Renderer.RenderGeometry(GeometryHandle, Rml::Vector2f(10.0f, 0.0f), 0);
	TestEqual(TEXT("Same size submission retains scratch capacity"),
		Renderer.GetSlateVertexScratchGrowthCount(), WarmGrowthCount);

	// Model a recursive draw while an outer submission still owns the scratch.
	const FVector2f OuterPosition = Renderer.SlateVertexScratch[0].Position;
	{
		TGuardValue<bool> BorrowedScratch(Renderer.bSlateVertexScratchInUse, true);
		Renderer.RenderGeometry(GeometryHandle, Rml::Vector2f(20.0f, 0.0f), 0);
		TestEqual(TEXT("Reentrant draw preserves the outer scratch"),
			Renderer.SlateVertexScratch[0].Position, OuterPosition);
		TestTrue(TEXT("Reentrant draw retains the outer borrow"), Renderer.bSlateVertexScratchInUse);
	}
	TestFalse(TEXT("Scratch borrow is released"), Renderer.bSlateVertexScratchInUse);
	TestEqual(TEXT("Fallback does not grow the shared scratch"),
		Renderer.GetSlateVertexScratchGrowthCount(), WarmGrowthCount);
	const FH5UI_PerformanceStats Stats = Renderer.EndPaint(FIntPoint(100, 100));
	Renderer.ReleaseGeometry(GeometryHandle);

	const auto& Draws = Elements.GetUncachedDrawElements().Get<(uint8)EElementType::ET_CustomVerts>();
	if (TestEqual(TEXT("Three real Slate submissions are retained"), Draws.Num(), 3))
	{
		for (int32 Index = 0; Index < Draws.Num(); ++Index)
		{
			if (TestEqual(TEXT("Each submission owns three vertices"), Draws[Index].Vertices.Num(), 3))
			{
				TestEqual(TEXT("Submitted position survives subsequent scratch writes"),
					Draws[Index].Vertices[0].Position, FVector2f(float(Index * 10), 0.0f));
			}
			TestEqual(TEXT("Layer ordering is preserved"), Draws[Index].GetLayer(), 7 + Index);
		}
	}
	TestEqual(TEXT("Draw statistics are preserved"), Stats.DrawBatches, 3);
	return true;
}

#endif
