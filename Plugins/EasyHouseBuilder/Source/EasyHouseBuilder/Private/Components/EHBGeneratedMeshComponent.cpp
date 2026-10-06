// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/EHBGeneratedMeshComponent.h"

#include "BodySetupEnums.h"
#include "DynamicMeshBuilder.h"
#include "Engine/Engine.h"
#include "MaterialDomain.h"
#include "Materials/Material.h"
#include "Materials/MaterialRenderProxy.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Misc/Crc.h"
#include "PrimitiveDrawingUtils.h"
#include "PrimitiveSceneProxy.h"
#include "PrimitiveUniformShaderParametersBuilder.h"
#include "PrimitiveViewRelevance.h"
#include "RenderUtils.h"
#include "SceneInterface.h"
#include "SceneView.h"
#include "StaticMeshResources.h"

namespace
{
DEFINE_LOG_CATEGORY_STATIC(LogEHBGeneratedMesh, Log, All);

class FEHBGeneratedMeshProxySection
{
public:
	explicit FEHBGeneratedMeshProxySection(ERHIFeatureLevel::Type InFeatureLevel)
		: VertexFactory(InFeatureLevel, "FEHBGeneratedMeshProxySection")
	{
	}

	UMaterialInterface* Material = nullptr;
	FStaticMeshVertexBuffers VertexBuffers;
	FDynamicMeshIndexBuffer32 IndexBuffer;
	FLocalVertexFactory VertexFactory;
	bool bSectionVisible = true;
};

void ConvertEHBVertexToDynamicVertex(FDynamicMeshVertex& OutVertex, const FEHBMeshVertex& InVertex)
{
	OutVertex.Position = FVector3f(InVertex.Position);
	OutVertex.Color = InVertex.Color;
	OutVertex.TextureCoordinate[0] = FVector2f(InVertex.UV0);
	OutVertex.TextureCoordinate[1] = FVector2f(InVertex.UV1);
	OutVertex.TextureCoordinate[2] = FVector2f(InVertex.UV2);
	OutVertex.TextureCoordinate[3] = FVector2f(InVertex.UV3);
	OutVertex.TangentX = InVertex.Tangent.TangentX;
	OutVertex.TangentZ = InVertex.Normal;
	OutVertex.TangentZ.Vector.W = InVertex.Tangent.bFlipTangentY ? -127 : 127;
}

template <typename ElementType>
uint32 HashArrayBytes(const TArray<ElementType>& Values, uint32 Hash)
{
	if (!Values.IsEmpty())
	{
		Hash = FCrc::MemCrc32(Values.GetData(), Values.Num() * sizeof(ElementType), Hash);
	}
	return Hash;
}

uint32 ComputeSectionContentHash(
	const TArray<FVector>& Vertices,
	const TArray<int32>& Triangles,
	const TArray<FVector>& Normals,
	const TArray<FVector2D>& UV0,
	const TArray<FVector2D>& UV1,
	const TArray<FVector2D>& UV2,
	const TArray<FVector2D>& UV3,
	const TArray<FColor>& VertexColors,
	const TArray<FEHBMeshTangent>& Tangents,
	bool bCreateCollision)
{
	uint32 Hash = GetTypeHash(Vertices.Num());
	Hash = HashCombine(Hash, GetTypeHash(Triangles.Num()));
	Hash = HashCombine(Hash, GetTypeHash(bCreateCollision));
	Hash = HashArrayBytes(Vertices, Hash);
	Hash = HashArrayBytes(Triangles, Hash);
	Hash = HashArrayBytes(Normals, Hash);
	Hash = HashArrayBytes(UV0, Hash);
	Hash = HashArrayBytes(UV1, Hash);
	Hash = HashArrayBytes(UV2, Hash);
	Hash = HashArrayBytes(UV3, Hash);
	Hash = HashArrayBytes(VertexColors, Hash);
	Hash = HashArrayBytes(Tangents, Hash);
	return Hash == 0 ? 1 : Hash;
}

bool SectionMatchesInputs(const FEHBMeshSection& Section,const TArray<FVector>& Vertices,const TArray<int32>& Triangles,const TArray<FVector>& Normals,
 const TArray<FVector2D>& UV0,const TArray<FVector2D>& UV1,const TArray<FVector2D>& UV2,const TArray<FVector2D>& UV3,
 const TArray<FColor>& VertexColors,const TArray<FEHBMeshTangent>& Tangents,bool bCreateCollision)
{
 if(!Section.bSectionVisible||Section.bEnableCollision!=bCreateCollision)return false;
 if(Vertices.IsEmpty()||Triangles.Num()<3)return Section.ProcVertexBuffer.IsEmpty()&&Section.ProcIndexBuffer.IsEmpty()&&!Section.SectionLocalBox.IsValid;
 if(Section.ProcVertexBuffer.Num()!=Vertices.Num())return false;
 const int32 N=Vertices.Num();FBox Box(ForceInit);
 for(int32 I=0;I<N;++I)
 {
  const auto& V=Section.ProcVertexBuffer[I];const FEHBMeshTangent T=Tangents.Num()==N?Tangents[I]:FEHBMeshTangent();
  if(V.Position!=Vertices[I]||V.Normal!=(Normals.Num()==N?Normals[I]:FVector::UpVector)
   ||V.UV0!=(UV0.Num()==N?UV0[I]:FVector2D::ZeroVector)||V.UV1!=(UV1.Num()==N?UV1[I]:FVector2D::ZeroVector)
   ||V.UV2!=(UV2.Num()==N?UV2[I]:FVector2D::ZeroVector)||V.UV3!=(UV3.Num()==N?UV3[I]:FVector2D::ZeroVector)
   ||V.Color!=(VertexColors.Num()==N?VertexColors[I]:FColor::White)||V.Tangent.TangentX!=T.TangentX||V.Tangent.bFlipTangentY!=T.bFlipTangentY)return false;
  Box+=V.Position;
 }
 if(Section.SectionLocalBox.IsValid!=Box.IsValid||Section.SectionLocalBox.Min!=Box.Min||Section.SectionLocalBox.Max!=Box.Max)return false;
 int32 OutIndex=0;
 for(int32 I=0;I+2<Triangles.Num();I+=3)
 {
  const uint32 A=FMath::Clamp(Triangles[I],0,N-1),B=FMath::Clamp(Triangles[I+1],0,N-1),C=FMath::Clamp(Triangles[I+2],0,N-1);
  if(A==B||A==C||B==C)continue;
  if(OutIndex+2>=Section.ProcIndexBuffer.Num()||Section.ProcIndexBuffer[OutIndex]!=A||Section.ProcIndexBuffer[OutIndex+1]!=B||Section.ProcIndexBuffer[OutIndex+2]!=C)return false;
  OutIndex+=3;
 }
 return OutIndex==Section.ProcIndexBuffer.Num();
}

class FEHBGeneratedMeshSceneProxy final : public FPrimitiveSceneProxy
{
public:
	FEHBGeneratedMeshSceneProxy(UEHBGeneratedMeshComponent* Component)
		: FPrimitiveSceneProxy(Component)
		, BodySetup(Component->GetBodySetup())
		, MaterialRelevance(Component->GetMaterialRelevance(GetScene().GetShaderPlatform()))
	{
		const TArray<FEHBMeshSection>& SourceSections = Component->GetMeshSections();
		Sections.SetNumZeroed(SourceSections.Num());

		for (int32 SectionIndex = 0; SectionIndex < SourceSections.Num(); ++SectionIndex)
		{
			const FEHBMeshSection& SourceSection = SourceSections[SectionIndex];
			if (SourceSection.ProcVertexBuffer.IsEmpty() || SourceSection.ProcIndexBuffer.IsEmpty())
			{
				continue;
			}

			FEHBGeneratedMeshProxySection* NewSection = new FEHBGeneratedMeshProxySection(GetScene().GetFeatureLevel());

			TArray<FDynamicMeshVertex> DynamicVertices;
			DynamicVertices.SetNumUninitialized(SourceSection.ProcVertexBuffer.Num());
			for (int32 VertexIndex = 0; VertexIndex < SourceSection.ProcVertexBuffer.Num(); ++VertexIndex)
			{
				ConvertEHBVertexToDynamicVertex(DynamicVertices[VertexIndex], SourceSection.ProcVertexBuffer[VertexIndex]);
			}

			NewSection->IndexBuffer.Indices = SourceSection.ProcIndexBuffer;
			NewSection->VertexBuffers.InitFromDynamicVertex(&NewSection->VertexFactory, DynamicVertices, 4);

			BeginInitResource(&NewSection->VertexBuffers.PositionVertexBuffer);
			BeginInitResource(&NewSection->VertexBuffers.StaticMeshVertexBuffer);
			BeginInitResource(&NewSection->VertexBuffers.ColorVertexBuffer);
			BeginInitResource(&NewSection->IndexBuffer);
			BeginInitResource(&NewSection->VertexFactory);

			NewSection->Material = Component->GetMaterial(SectionIndex);
			if (!NewSection->Material)
			{
				NewSection->Material = UMaterial::GetDefaultMaterial(MD_Surface);
			}
			NewSection->bSectionVisible = SourceSection.bSectionVisible;
			Sections[SectionIndex] = NewSection;
		}
	}

	virtual ~FEHBGeneratedMeshSceneProxy() override
	{
		for (FEHBGeneratedMeshProxySection* Section : Sections)
		{
			if (!Section)
			{
				continue;
			}

			Section->VertexBuffers.PositionVertexBuffer.ReleaseResource();
			Section->VertexBuffers.StaticMeshVertexBuffer.ReleaseResource();
			Section->VertexBuffers.ColorVertexBuffer.ReleaseResource();
			Section->IndexBuffer.ReleaseResource();
			Section->VertexFactory.ReleaseResource();
			delete Section;
		}
	}

	virtual SIZE_T GetTypeHash() const override
	{
		static SIZE_T UniquePointer;
		return reinterpret_cast<SIZE_T>(&UniquePointer);
	}

	virtual void GetDynamicMeshElements(
		const TArray<const FSceneView*>& Views,
		const FSceneViewFamily& ViewFamily,
		uint32 VisibilityMap,
		FMeshElementCollector& Collector) const override
	{
		const bool bWireframe = AllowDebugViewmodes() && ViewFamily.EngineShowFlags.Wireframe;
		FColoredMaterialRenderProxy* WireframeMaterialProxy = nullptr;
		if (bWireframe)
		{
			WireframeMaterialProxy = new FColoredMaterialRenderProxy(
				GEngine && GEngine->WireframeMaterial ? GEngine->WireframeMaterial->GetRenderProxy() : nullptr,
				FLinearColor(0.0f, 0.5f, 1.0f));
			Collector.RegisterOneFrameMaterialProxy(WireframeMaterialProxy);
		}

		for (const FEHBGeneratedMeshProxySection* Section : Sections)
		{
			if (!Section || !Section->bSectionVisible)
			{
				continue;
			}

			FMaterialRenderProxy* MaterialProxy = bWireframe ? WireframeMaterialProxy : Section->Material->GetRenderProxy();

			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
			{
				if ((VisibilityMap & (1 << ViewIndex)) == 0)
				{
					continue;
				}

				FMeshBatch& Mesh = Collector.AllocateMesh();
				FMeshBatchElement& BatchElement = Mesh.Elements[0];
				BatchElement.IndexBuffer = &Section->IndexBuffer;
				BatchElement.FirstIndex = 0;
				BatchElement.NumPrimitives = Section->IndexBuffer.Indices.Num() / 3;
				BatchElement.MinVertexIndex = 0;
				BatchElement.MaxVertexIndex = Section->VertexBuffers.PositionVertexBuffer.GetNumVertices() - 1;

				FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
				FPrimitiveUniformShaderParametersBuilder Builder;
				BuildUniformShaderParameters(Builder);
				DynamicPrimitiveUniformBuffer.Set(Collector.GetRHICommandList(), Builder);
				BatchElement.PrimitiveUniformBufferResource = &DynamicPrimitiveUniformBuffer.UniformBuffer;

				Mesh.bWireframe = bWireframe;
				Mesh.VertexFactory = &Section->VertexFactory;
				Mesh.MaterialRenderProxy = MaterialProxy;
				Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
				Mesh.Type = PT_TriangleList;
				Mesh.DepthPriorityGroup = SDPG_World;
				Mesh.bCanApplyViewModeOverrides = false;
				Collector.AddMesh(ViewIndex, Mesh);
			}
		}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
		{
			if ((VisibilityMap & (1 << ViewIndex)) == 0)
			{
				continue;
			}

			if (ViewFamily.EngineShowFlags.Collision
				&& IsCollisionEnabled()
				&& BodySetup
				&& BodySetup->GetCollisionTraceFlag() != ECollisionTraceFlag::CTF_UseComplexAsSimple)
			{
				const FTransform GeomTransform(GetLocalToWorld());
				BodySetup->AggGeom.GetAggGeom(
					GeomTransform,
					GetSelectionColor(FColor(157, 149, 223, 255), IsSelected(), IsHovered()).ToFColor(true),
					nullptr,
					false,
					false,
					AlwaysHasVelocity(),
					ViewIndex,
					Collector);
			}

			RenderBounds(Collector.GetPDI(ViewIndex), ViewFamily.EngineShowFlags, GetBounds(), IsSelected());
		}
#endif
	}

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
	{
		FPrimitiveViewRelevance Result;
		Result.bDrawRelevance = IsShown(View);
		Result.bShadowRelevance = IsShadowCast(View);
		Result.bDynamicRelevance = true;
		Result.bRenderInMainPass = ShouldRenderInMainPass();
		Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
		Result.bRenderCustomDepth = ShouldRenderCustomDepth();
		Result.bTranslucentSelfShadow = bCastVolumetricTranslucentShadow;
		MaterialRelevance.SetPrimitiveViewRelevance(Result);
		Result.bVelocityRelevance = DrawsVelocity() && Result.bOpaque && Result.bRenderInMainPass;
		return Result;
	}

	virtual bool CanBeOccluded() const override
	{
		return !MaterialRelevance.bDisableDepthTest;
	}

	virtual uint32 GetMemoryFootprint() const override
	{
		return sizeof(*this) + GetAllocatedSize();
	}

private:
	TArray<FEHBGeneratedMeshProxySection*> Sections;
	UBodySetup* BodySetup = nullptr;
	FMaterialRelevance MaterialRelevance;
};
}

UEHBGeneratedMeshComponent::UEHBGeneratedMeshComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UEHBGeneratedMeshComponent::PostLoad()
{
	Super::PostLoad();

	if (MeshBodySetup && IsTemplate())
	{
		MeshBodySetup->SetFlags(RF_Public | RF_ArchetypeObject);
	}
}

bool UEHBGeneratedMeshComponent::MatchesMeshSection(int32 SectionIndex,const TArray<FVector>& Vertices,const TArray<int32>& Triangles,const TArray<FVector>& Normals,
 const TArray<FVector2D>& UV0,const TArray<FVector2D>& UV1,const TArray<FVector2D>& UV2,const TArray<FVector2D>& UV3,
 const TArray<FColor>& VertexColors,const TArray<FEHBMeshTangent>& Tangents,bool bCreateCollision) const
{
 return MeshSections.IsValidIndex(SectionIndex)&&SectionMatchesInputs(MeshSections[SectionIndex],Vertices,Triangles,Normals,UV0,UV1,UV2,UV3,VertexColors,Tangents,bCreateCollision);
}

void UEHBGeneratedMeshComponent::CreateMeshSection_LinearColor(
	int32 SectionIndex,
	const TArray<FVector>& Vertices,
	const TArray<int32>& Triangles,
	const TArray<FVector>& Normals,
	const TArray<FVector2D>& UV0,
	const TArray<FVector2D>& UV1,
	const TArray<FVector2D>& UV2,
	const TArray<FVector2D>& UV3,
	const TArray<FLinearColor>& VertexColors,
	const TArray<FEHBMeshTangent>& Tangents,
	bool bCreateCollision,
	bool bSRGBConversion)
{
	TArray<FColor> PackedColors;
	if (!VertexColors.IsEmpty())
	{
		PackedColors.Reserve(VertexColors.Num());
		for (const FLinearColor& VertexColor : VertexColors)
		{
			PackedColors.Add(VertexColor.ToFColor(bSRGBConversion));
		}
	}

	CreateMeshSection(SectionIndex, Vertices, Triangles, Normals, UV0, UV1, UV2, UV3, PackedColors, Tangents, bCreateCollision);
}

void UEHBGeneratedMeshComponent::CreateMeshSection_LinearColor(
	int32 SectionIndex,
	const TArray<FVector>& Vertices,
	const TArray<int32>& Triangles,
	const TArray<FVector>& Normals,
	const TArray<FVector2D>& UV0,
	const TArray<FLinearColor>& VertexColors,
	const TArray<FEHBMeshTangent>& Tangents,
	bool bCreateCollision,
	bool bSRGBConversion)
{
	static const TArray<FVector2D> EmptyUVs;
	CreateMeshSection_LinearColor(SectionIndex, Vertices, Triangles, Normals, UV0, EmptyUVs, EmptyUVs, EmptyUVs, VertexColors, Tangents, bCreateCollision, bSRGBConversion);
}

void UEHBGeneratedMeshComponent::CreateNamedMeshSection_LinearColor(
	FName SectionName,
	const TArray<FVector>& Vertices,
	const TArray<int32>& Triangles,
	const TArray<FVector>& Normals,
	const TArray<FVector2D>& UV0,
	const TArray<FLinearColor>& VertexColors,
	const TArray<FEHBMeshTangent>& Tangents,
	bool bCreateCollision,
	bool bSRGBConversion)
{
	const int32 SectionIndex = FindOrAddMeshSection(SectionName);
	CreateMeshSection_LinearColor(SectionIndex, Vertices, Triangles, Normals, UV0, VertexColors, Tangents, bCreateCollision, bSRGBConversion);
	SetMeshSectionName(SectionIndex, SectionName);
}

void UEHBGeneratedMeshComponent::CreateMeshSection(
	int32 SectionIndex,
	const TArray<FVector>& Vertices,
	const TArray<int32>& Triangles,
	const TArray<FVector>& Normals,
	const TArray<FVector2D>& UV0,
	const TArray<FVector2D>& UV1,
	const TArray<FVector2D>& UV2,
	const TArray<FVector2D>& UV3,
	const TArray<FColor>& VertexColors,
	const TArray<FEHBMeshTangent>& Tangents,
	bool bCreateCollision)
{
	if (SectionIndex < 0)
	{
		UE_LOG(LogEHBGeneratedMesh, Warning, TEXT("CreateMeshSection received negative section index on %s."), *GetFullName());
		return;
	}

	const uint32 NewContentHash = ComputeSectionContentHash(Vertices, Triangles, Normals, UV0, UV1, UV2, UV3, VertexColors, Tangents, bCreateCollision);
	if (MeshSections.IsValidIndex(SectionIndex))
	{
		FEHBMeshSection& ExistingSection = MeshSections[SectionIndex];
		if (ExistingSection.ContentHash == NewContentHash && SectionMatchesInputs(ExistingSection,Vertices,Triangles,Normals,UV0,UV1,UV2,UV3,VertexColors,Tangents,bCreateCollision))
		{
			++SkippedIdenticalSectionUpdateCount;
			return;
		}
	}

	if (SectionIndex >= MeshSections.Num())
	{
		MeshSections.SetNum(SectionIndex + 1, EAllowShrinking::No);
		MarkSectionNameMapDirty();
	}

	FEHBMeshSection& NewSection = MeshSections[SectionIndex];
	const FName ExistingSectionName = NewSection.SectionName;
	const bool bHadCollision = NewSection.bEnableCollision;
	NewSection.Reset();
	NewSection.SectionName = ExistingSectionName;

	const int32 NumVertices = Vertices.Num();
	if (NumVertices <= 0 || Triangles.Num() < 3)
	{
		NewSection.ContentHash = NewContentHash;
		NewSection.bEnableCollision = bCreateCollision;
		++SubmittedSectionUpdateCount;
		RequestMeshRefresh(bHadCollision || bCreateCollision);
		return;
	}

	NewSection.ProcVertexBuffer.SetNumUninitialized(NumVertices);
	for (int32 VertexIndex = 0; VertexIndex < NumVertices; ++VertexIndex)
	{
		FEHBMeshVertex& Vertex = NewSection.ProcVertexBuffer[VertexIndex];
		Vertex.Position = Vertices[VertexIndex];
		Vertex.Normal = Normals.Num() == NumVertices ? Normals[VertexIndex] : FVector::UpVector;
		Vertex.UV0 = UV0.Num() == NumVertices ? UV0[VertexIndex] : FVector2D::ZeroVector;
		Vertex.UV1 = UV1.Num() == NumVertices ? UV1[VertexIndex] : FVector2D::ZeroVector;
		Vertex.UV2 = UV2.Num() == NumVertices ? UV2[VertexIndex] : FVector2D::ZeroVector;
		Vertex.UV3 = UV3.Num() == NumVertices ? UV3[VertexIndex] : FVector2D::ZeroVector;
		Vertex.Color = VertexColors.Num() == NumVertices ? VertexColors[VertexIndex] : FColor::White;
		Vertex.Tangent = Tangents.Num() == NumVertices ? Tangents[VertexIndex] : FEHBMeshTangent();
		NewSection.SectionLocalBox += Vertex.Position;
	}

	const int32 MaxVertexIndex = NumVertices - 1;
	const int32 NumTriangleIndices = (Triangles.Num() / 3) * 3;
	int32 NumDegenerateTriangles = 0;
	for (int32 Index = 0; Index < NumTriangleIndices; Index += 3)
	{
		const int32 A = FMath::Clamp(Triangles[Index], 0, MaxVertexIndex);
		const int32 B = FMath::Clamp(Triangles[Index + 1], 0, MaxVertexIndex);
		const int32 C = FMath::Clamp(Triangles[Index + 2], 0, MaxVertexIndex);
		if (A == B || A == C || B == C)
		{
			++NumDegenerateTriangles;
		}
	}

	NewSection.ProcIndexBuffer.Reserve(NumTriangleIndices - NumDegenerateTriangles * 3);
	for (int32 Index = 0; Index < NumTriangleIndices; Index += 3)
	{
		const uint32 A = static_cast<uint32>(FMath::Clamp(Triangles[Index], 0, MaxVertexIndex));
		const uint32 B = static_cast<uint32>(FMath::Clamp(Triangles[Index + 1], 0, MaxVertexIndex));
		const uint32 C = static_cast<uint32>(FMath::Clamp(Triangles[Index + 2], 0, MaxVertexIndex));
		if (A != B && A != C && B != C)
		{
			NewSection.ProcIndexBuffer.Add(A);
			NewSection.ProcIndexBuffer.Add(B);
			NewSection.ProcIndexBuffer.Add(C);
		}
	}

	if (NumDegenerateTriangles > 0)
	{
		UE_LOG(LogEHBGeneratedMesh, Verbose, TEXT("Dropped %d degenerate triangle(s) while creating section %d on %s."),
			NumDegenerateTriangles,
			SectionIndex,
			*GetFullName());
	}

	NewSection.bEnableCollision = bCreateCollision;
	NewSection.bSectionVisible = true;
	NewSection.ContentHash = NewContentHash;
	++NewSection.Revision;
	++SubmittedSectionUpdateCount;
	RequestMeshRefresh(bHadCollision || bCreateCollision);
}

void UEHBGeneratedMeshComponent::UpdateMeshSection_LinearColor(
	int32 SectionIndex,
	const TArray<FVector>& Vertices,
	const TArray<FVector>& Normals,
	const TArray<FVector2D>& UV0,
	const TArray<FVector2D>& UV1,
	const TArray<FVector2D>& UV2,
	const TArray<FVector2D>& UV3,
	const TArray<FLinearColor>& VertexColors,
	const TArray<FEHBMeshTangent>& Tangents,
	bool bSRGBConversion)
{
	if (!MeshSections.IsValidIndex(SectionIndex))
	{
		return;
	}

	FEHBMeshSection& Section = MeshSections[SectionIndex];
	const int32 NumVerticesToUpdate = FMath::Min(Vertices.Num(), Section.ProcVertexBuffer.Num());
	Section.SectionLocalBox.Init();
	for (int32 VertexIndex = 0; VertexIndex < NumVerticesToUpdate; ++VertexIndex)
	{
		FEHBMeshVertex& Vertex = Section.ProcVertexBuffer[VertexIndex];
		Vertex.Position = Vertices[VertexIndex];
		Vertex.Normal = Normals.Num() == Vertices.Num() ? Normals[VertexIndex] : Vertex.Normal;
		Vertex.UV0 = UV0.Num() == Vertices.Num() ? UV0[VertexIndex] : Vertex.UV0;
		Vertex.UV1 = UV1.Num() == Vertices.Num() ? UV1[VertexIndex] : Vertex.UV1;
		Vertex.UV2 = UV2.Num() == Vertices.Num() ? UV2[VertexIndex] : Vertex.UV2;
		Vertex.UV3 = UV3.Num() == Vertices.Num() ? UV3[VertexIndex] : Vertex.UV3;
		Vertex.Color = VertexColors.Num() == Vertices.Num() ? VertexColors[VertexIndex].ToFColor(bSRGBConversion) : Vertex.Color;
		Vertex.Tangent = Tangents.Num() == Vertices.Num() ? Tangents[VertexIndex] : Vertex.Tangent;
	}

	for (const FEHBMeshVertex& Vertex : Section.ProcVertexBuffer)
	{
		Section.SectionLocalBox += Vertex.Position;
	}

	++Section.Revision;
	Section.ContentHash = 0;
	++SubmittedSectionUpdateCount;
	RequestMeshRefresh(Section.bEnableCollision);
}

void UEHBGeneratedMeshComponent::ClearMeshSection(int32 SectionIndex)
{
	if (MeshSections.IsValidIndex(SectionIndex))
	{
		const bool bHadCollision = MeshSections[SectionIndex].bEnableCollision;
		const FName ExistingSectionName = MeshSections[SectionIndex].SectionName;
		MeshSections[SectionIndex].Reset();
		MeshSections[SectionIndex].SectionName = ExistingSectionName;
		RequestMeshRefresh(bHadCollision);
	}
}

void UEHBGeneratedMeshComponent::ClearMeshSectionsFrom(int32 FirstSectionIndex)
{
	if (FirstSectionIndex < 0)
	{
		FirstSectionIndex = 0;
	}

	if (FirstSectionIndex >= MeshSections.Num())
	{
		return;
	}

	bool bHadCollision = false;
	for (int32 SectionIndex = FirstSectionIndex; SectionIndex < MeshSections.Num(); ++SectionIndex)
	{
		bHadCollision |= MeshSections[SectionIndex].bEnableCollision;
	}

	MeshSections.SetNum(FirstSectionIndex, EAllowShrinking::No);
	MarkSectionNameMapDirty();
	RequestMeshRefresh(bHadCollision);
}

void UEHBGeneratedMeshComponent::ClearAllMeshSections()
{
	bool bHadCollision = false;
	for (const FEHBMeshSection& Section : MeshSections)
	{
		bHadCollision |= Section.bEnableCollision;
	}

	MeshSections.Empty();
	MarkSectionNameMapDirty();
	RequestMeshRefresh(bHadCollision);
}

void UEHBGeneratedMeshComponent::SetMeshSectionVisible(int32 SectionIndex, bool bNewVisibility)
{
	if (MeshSections.IsValidIndex(SectionIndex) && MeshSections[SectionIndex].bSectionVisible != bNewVisibility)
	{
		MeshSections[SectionIndex].bSectionVisible = bNewVisibility;
		RequestMeshRefresh(false);
	}
}

bool UEHBGeneratedMeshComponent::IsMeshSectionVisible(int32 SectionIndex) const
{
	return MeshSections.IsValidIndex(SectionIndex) && MeshSections[SectionIndex].bSectionVisible;
}

int32 UEHBGeneratedMeshComponent::GetNumSections() const
{
	return MeshSections.Num();
}

bool UEHBGeneratedMeshComponent::SetMaterialIfChanged(int32 ElementIndex, UMaterialInterface* Material)
{
	if (ElementIndex < 0 || GetMaterial(ElementIndex) == Material)
	{
		return false;
	}

	SetMaterial(ElementIndex, Material);
	return true;
}

FEHBGeneratedMeshStats UEHBGeneratedMeshComponent::GetMeshStats() const
{
	FEHBGeneratedMeshStats Stats;
	Stats.SectionCount = MeshSections.Num();
	Stats.MeshRevision = MeshRevision;
	Stats.SubmittedSectionUpdateCount = static_cast<int64>(SubmittedSectionUpdateCount);
	Stats.SkippedIdenticalSectionUpdateCount = static_cast<int64>(SkippedIdenticalSectionUpdateCount);

	for (const FEHBMeshSection& Section : MeshSections)
	{
		const int32 SectionVertexCount = Section.ProcVertexBuffer.Num();
		const int32 SectionTriangleCount = Section.ProcIndexBuffer.Num() / 3;
		Stats.VertexCount += SectionVertexCount;
		Stats.TriangleCount += SectionTriangleCount;
		Stats.EstimatedVertexBufferBytes += static_cast<int64>(SectionVertexCount) * sizeof(FEHBMeshVertex);
		Stats.EstimatedIndexBufferBytes += static_cast<int64>(Section.ProcIndexBuffer.Num()) * sizeof(uint32);

		if (Section.bSectionVisible && SectionVertexCount > 0 && SectionTriangleCount > 0)
		{
			++Stats.VisibleSectionCount;
		}

		if (Section.bEnableCollision)
		{
			++Stats.CollisionSectionCount;
			Stats.CollisionVertexCount += SectionVertexCount;
			Stats.CollisionTriangleCount += SectionTriangleCount;
		}
	}

	return Stats;
}

void UEHBGeneratedMeshComponent::ResetMeshUpdateStats()
{
	SubmittedSectionUpdateCount = 0;
	SkippedIdenticalSectionUpdateCount = 0;
}

int32 UEHBGeneratedMeshComponent::FindMeshSectionIndex(FName SectionName) const
{
	if (SectionName.IsNone())
	{
		return INDEX_NONE;
	}

	RebuildSectionNameMap();
	if (const int32* FoundIndex = SectionNameToIndex.Find(SectionName))
	{
		return *FoundIndex;
	}

	return INDEX_NONE;
}

int32 UEHBGeneratedMeshComponent::FindOrAddMeshSection(FName SectionName)
{
	if (SectionName.IsNone())
	{
		return MeshSections.AddDefaulted();
	}

	if (const int32 ExistingIndex = FindMeshSectionIndex(SectionName); ExistingIndex != INDEX_NONE)
	{
		return ExistingIndex;
	}

	const int32 NewIndex = MeshSections.AddDefaulted();
	MeshSections[NewIndex].SectionName = SectionName;
	MarkSectionNameMapDirty();
	return NewIndex;
}

void UEHBGeneratedMeshComponent::SetMeshSectionName(int32 SectionIndex, FName SectionName)
{
	if (!MeshSections.IsValidIndex(SectionIndex))
	{
		return;
	}

	if (MeshSections[SectionIndex].SectionName != SectionName)
	{
		MeshSections[SectionIndex].SectionName = SectionName;
		MarkSectionNameMapDirty();
	}
}

FEHBMeshSection* UEHBGeneratedMeshComponent::GetProcMeshSection(int32 SectionIndex)
{
	return MeshSections.IsValidIndex(SectionIndex) ? &MeshSections[SectionIndex] : nullptr;
}

const FEHBMeshSection* UEHBGeneratedMeshComponent::GetProcMeshSection(int32 SectionIndex) const
{
	return MeshSections.IsValidIndex(SectionIndex) ? &MeshSections[SectionIndex] : nullptr;
}

void UEHBGeneratedMeshComponent::SetProcMeshSection(int32 SectionIndex, const FEHBMeshSection& Section)
{
	if (SectionIndex < 0)
	{
		return;
	}

	if (SectionIndex >= MeshSections.Num())
	{
		MeshSections.SetNum(SectionIndex + 1, EAllowShrinking::No);
		MarkSectionNameMapDirty();
	}

	MeshSections[SectionIndex] = Section;
	++MeshSections[SectionIndex].Revision;
	MeshSections[SectionIndex].ContentHash = 0;
	MarkSectionNameMapDirty();
	RequestMeshRefresh(Section.bEnableCollision);
}

void UEHBGeneratedMeshComponent::BeginMeshUpdate()
{
	++MeshUpdateDepth;
}

void UEHBGeneratedMeshComponent::EndMeshUpdate()
{
	if (MeshUpdateDepth <= 0)
	{
		return;
	}

	--MeshUpdateDepth;
	if (MeshUpdateDepth == 0)
	{
		FlushDeferredMeshRefresh();
	}
}

void UEHBGeneratedMeshComponent::AddCollisionConvexMesh(TArray<FVector> ConvexVerts)
{
	FKConvexElem NewConvexElem;
	NewConvexElem.VertexData = MoveTemp(ConvexVerts);
	NewConvexElem.UpdateElemBox();
	CollisionConvexElems.Add(MoveTemp(NewConvexElem));
	UpdateCollision();
}

void UEHBGeneratedMeshComponent::ClearCollisionConvexMeshes()
{
	CollisionConvexElems.Empty();
	UpdateCollision();
}

void UEHBGeneratedMeshComponent::SetCollisionConvexMeshes(const TArray<TArray<FVector>>& ConvexMeshes)
{
	CollisionConvexElems.Reset(ConvexMeshes.Num());
	for (const TArray<FVector>& ConvexMesh : ConvexMeshes)
	{
		FKConvexElem NewConvexElem;
		NewConvexElem.VertexData = ConvexMesh;
		NewConvexElem.UpdateElemBox();
		CollisionConvexElems.Add(MoveTemp(NewConvexElem));
	}
	UpdateCollision();
}

void UEHBGeneratedMeshComponent::RebuildSectionNameMap() const
{
	if (!bSectionNameMapDirty)
	{
		return;
	}

	SectionNameToIndex.Reset();
	for (int32 SectionIndex = 0; SectionIndex < MeshSections.Num(); ++SectionIndex)
	{
		const FName SectionName = MeshSections[SectionIndex].SectionName;
		if (!SectionName.IsNone() && !SectionNameToIndex.Contains(SectionName))
		{
			SectionNameToIndex.Add(SectionName, SectionIndex);
		}
	}

	bSectionNameMapDirty = false;
}

void UEHBGeneratedMeshComponent::MarkSectionNameMapDirty() const
{
	bSectionNameMapDirty = true;
}

bool UEHBGeneratedMeshComponent::GetTriMeshSizeEstimates(FTriMeshCollisionDataEstimates& OutTriMeshEstimates, bool bInUseAllTriData) const
{
	for (const FEHBMeshSection& Section : MeshSections)
	{
		if (Section.bEnableCollision)
		{
			OutTriMeshEstimates.VerticeCount += Section.ProcVertexBuffer.Num();
		}
	}
	return true;
}

bool UEHBGeneratedMeshComponent::GetPhysicsTriMeshData(FTriMeshCollisionData* CollisionData, bool InUseAllTriData)
{
	if (!CollisionData)
	{
		return false;
	}

	int32 VertexBase = 0;
	const bool bCopyUVs = UPhysicsSettings::Get()->bSupportUVFromHitResults;
	if (bCopyUVs)
	{
		CollisionData->UVs.AddZeroed(1);
	}

	for (int32 SectionIndex = 0; SectionIndex < MeshSections.Num(); ++SectionIndex)
	{
		const FEHBMeshSection& Section = MeshSections[SectionIndex];
		if (!Section.bEnableCollision)
		{
			continue;
		}

		for (const FEHBMeshVertex& Vertex : Section.ProcVertexBuffer)
		{
			CollisionData->Vertices.Add(FVector3f(Vertex.Position));
			if (bCopyUVs)
			{
				CollisionData->UVs[0].Add(Vertex.UV0);
			}
		}

		const int32 NumTriangles = Section.ProcIndexBuffer.Num() / 3;
		for (int32 TriangleIndex = 0; TriangleIndex < NumTriangles; ++TriangleIndex)
		{
			FTriIndices Triangle;
			Triangle.v0 = Section.ProcIndexBuffer[TriangleIndex * 3 + 0] + VertexBase;
			Triangle.v1 = Section.ProcIndexBuffer[TriangleIndex * 3 + 1] + VertexBase;
			Triangle.v2 = Section.ProcIndexBuffer[TriangleIndex * 3 + 2] + VertexBase;
			CollisionData->Indices.Add(Triangle);
			CollisionData->MaterialIndices.Add(SectionIndex);
		}

		VertexBase = CollisionData->Vertices.Num();
	}

	CollisionData->bFlipNormals = true;
	CollisionData->bDeformableMesh = true;
	CollisionData->bFastCook = true;
	return true;
}

bool UEHBGeneratedMeshComponent::ContainsPhysicsTriMeshData(bool InUseAllTriData) const
{
	for (const FEHBMeshSection& Section : MeshSections)
	{
		if (Section.bEnableCollision && Section.ProcIndexBuffer.Num() >= 3)
		{
			return true;
		}
	}
	return false;
}

FPrimitiveSceneProxy* UEHBGeneratedMeshComponent::CreateSceneProxy()
{
	return new FEHBGeneratedMeshSceneProxy(this);
}

UBodySetup* UEHBGeneratedMeshComponent::GetBodySetup()
{
	CreateMeshBodySetup();
	return MeshBodySetup;
}

UMaterialInterface* UEHBGeneratedMeshComponent::GetMaterialFromCollisionFaceIndex(int32 FaceIndex, int32& SectionIndex) const
{
	SectionIndex = 0;
	if (FaceIndex < 0)
	{
		return nullptr;
	}

	int32 TotalFaceCount = 0;
	for (int32 CurrentSectionIndex = 0; CurrentSectionIndex < MeshSections.Num(); ++CurrentSectionIndex)
	{
		const int32 NumFaces = MeshSections[CurrentSectionIndex].ProcIndexBuffer.Num() / 3;
		TotalFaceCount += NumFaces;
		if (FaceIndex < TotalFaceCount)
		{
			SectionIndex = CurrentSectionIndex;
			return GetMaterial(CurrentSectionIndex);
		}
	}

	return nullptr;
}

int32 UEHBGeneratedMeshComponent::GetNumMaterials() const
{
	return MeshSections.Num();
}

void UEHBGeneratedMeshComponent::GetStreamingRenderAssetInfo(FStreamingTextureLevelContext& LevelContext, TArray<FStreamingRenderAssetPrimitiveInfo>& OutStreamingRenderAssets) const
{
	// EHB meshes are generated editor geometry; they do not have static mesh UV density data for texture streaming builds.
}

FBoxSphereBounds UEHBGeneratedMeshComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	FBoxSphereBounds Result(LocalBounds.TransformBy(LocalToWorld));
	Result.BoxExtent *= BoundsScale;
	Result.SphereRadius *= BoundsScale;
	return Result;
}

void UEHBGeneratedMeshComponent::UpdateLocalBounds()
{
	FBox LocalBox(ForceInit);
	for (const FEHBMeshSection& Section : MeshSections)
	{
		LocalBox += Section.SectionLocalBox;
	}

	LocalBounds = LocalBox.IsValid
		? FBoxSphereBounds(LocalBox)
		: FBoxSphereBounds(FVector::ZeroVector, FVector::ZeroVector, 0.0);

	UpdateBounds();
	MarkRenderTransformDirty();
}

void UEHBGeneratedMeshComponent::RequestMeshRefresh(bool bNeedsCollisionUpdate)
{
	++MeshRevision;

	if (MeshUpdateDepth > 0)
	{
		bDeferredBoundsUpdate = true;
		bDeferredRenderUpdate = true;
		bDeferredCollisionUpdate |= bNeedsCollisionUpdate;
		return;
	}

	UpdateLocalBounds();
	if (bNeedsCollisionUpdate)
	{
		UpdateCollision();
	}
	MarkRenderStateDirty();
}

void UEHBGeneratedMeshComponent::FlushDeferredMeshRefresh()
{
	if (!bDeferredBoundsUpdate && !bDeferredRenderUpdate && !bDeferredCollisionUpdate)
	{
		return;
	}

	if (bDeferredBoundsUpdate)
	{
		UpdateLocalBounds();
	}
	if (bDeferredCollisionUpdate)
	{
		UpdateCollision();
	}
	if (bDeferredRenderUpdate)
	{
		MarkRenderStateDirty();
	}

	bDeferredBoundsUpdate = false;
	bDeferredRenderUpdate = false;
	bDeferredCollisionUpdate = false;
}

UBodySetup* UEHBGeneratedMeshComponent::CreateBodySetupHelper()
{
	UBodySetup* NewBodySetup = NewObject<UBodySetup>(this, NAME_None, IsTemplate() ? RF_Public | RF_ArchetypeObject : RF_NoFlags);
	NewBodySetup->BodySetupGuid = FGuid::NewGuid();
	NewBodySetup->bGenerateMirroredCollision = false;
	NewBodySetup->bDoubleSidedGeometry = true;
	NewBodySetup->CollisionTraceFlag = bUseComplexAsSimpleCollision ? CTF_UseComplexAsSimple : CTF_UseDefault;
	return NewBodySetup;
}

void UEHBGeneratedMeshComponent::CreateMeshBodySetup()
{
	if (!MeshBodySetup)
	{
		MeshBodySetup = CreateBodySetupHelper();
	}
}

void UEHBGeneratedMeshComponent::UpdateCollision()
{
	UWorld* World = GetWorld();
	const bool bUseAsyncCook = World && World->IsGameWorld() && bUseAsyncCooking;

	if (bUseAsyncCook)
	{
		for (UBodySetup* OldBodySetup : AsyncBodySetupQueue)
		{
			if (OldBodySetup)
			{
				OldBodySetup->AbortPhysicsMeshAsyncCreation();
			}
		}
		AsyncBodySetupQueue.Add(CreateBodySetupHelper());
	}
	else
	{
		AsyncBodySetupQueue.Empty();
		CreateMeshBodySetup();
	}

	UBodySetup* TargetBodySetup = bUseAsyncCook ? AsyncBodySetupQueue.Last() : MeshBodySetup;
	if (!TargetBodySetup)
	{
		return;
	}

	TargetBodySetup->AggGeom.ConvexElems = CollisionConvexElems;
	TargetBodySetup->CollisionTraceFlag = bUseComplexAsSimpleCollision ? CTF_UseComplexAsSimple : CTF_UseDefault;

	if (bUseAsyncCook)
	{
		TargetBodySetup->CreatePhysicsMeshesAsync(FOnAsyncPhysicsCookFinished::CreateUObject(this, &UEHBGeneratedMeshComponent::FinishPhysicsAsyncCook, TargetBodySetup));
	}
	else
	{
		TargetBodySetup->BodySetupGuid = FGuid::NewGuid();
		TargetBodySetup->bHasCookedCollisionData = true;
		TargetBodySetup->InvalidatePhysicsData();
		TargetBodySetup->CreatePhysicsMeshes();
		RecreatePhysicsState();
	}
}

void UEHBGeneratedMeshComponent::FinishPhysicsAsyncCook(bool bSuccess, UBodySetup* FinishedBodySetup)
{
	int32 FoundIndex = INDEX_NONE;
	if (!AsyncBodySetupQueue.Find(FinishedBodySetup, FoundIndex))
	{
		return;
	}

	const ECollisionTraceFlag CollisionTraceFlag = FinishedBodySetup->GetCollisionTraceFlag();
	const bool bEmptySimpleCollision = FinishedBodySetup->AggGeom.ConvexElems.IsEmpty();
	const bool bEmptyComplexCollision = !ContainsPhysicsTriMeshData(false);
	const bool bNoCookNeeded =
		(CollisionTraceFlag == CTF_UseSimpleAsComplex && bEmptySimpleCollision)
		|| (CollisionTraceFlag == CTF_UseComplexAsSimple && bEmptyComplexCollision)
		|| (CollisionTraceFlag == CTF_UseSimpleAndComplex && bEmptySimpleCollision && bEmptyComplexCollision);

	if (bSuccess || bNoCookNeeded)
	{
		MeshBodySetup = FinishedBodySetup;
		RecreatePhysicsState();
		AsyncBodySetupQueue.RemoveAt(0, FoundIndex + 1);
	}
	else
	{
		AsyncBodySetupQueue.RemoveAt(FoundIndex);
	}
}

#if WITH_EDITOR
void UEHBGeneratedMeshComponent::PostEditUndo()
{
	Super::PostEditUndo();
	// Mesh arrays are transactional; section lookup, render state and cooked
	// collision must be refreshed from the restored arrays after undo/redo.
	MarkSectionNameMapDirty();
	RequestMeshRefresh(true);
}
#endif
