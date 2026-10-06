#pragma once
#include "CoreMinimal.h"

namespace RegionBlockGeometry
{
struct FMesh
{
	TArray<FVector> Vertices;
	TArray<int32> Indices;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	FVector LabelPosition;
};
bool Build(TArray<FVector2D> Points, double Height, FMesh& Out, FText& Error);
}
