#pragma once

inline bool SortPlaneVerticesByAngle(TArray<FVector2D> InVertices, TArray<FVector2D>& OutVertices)
{
	if (InVertices.Num() < 3)
	{
		return false;
	}
	
	FVector2D Centroid = FVector2D::ZeroVector;
	for (const FVector2D& Vertex : InVertices)
	{
		Centroid += Vertex;
	}
	Centroid /= InVertices.Num();
	
	TArray<TPair<int32, float>> VertexAngles;
	VertexAngles.Reserve(InVertices.Num());

	for (int32 i = 0; i < InVertices.Num(); ++i)
	{
		const FVector2D Direction = InVertices[i] - Centroid;
		float Angle = FMath::Atan2(Direction.Y, Direction.X); // I.k., it's so expensive
		VertexAngles.Emplace(i, Angle);
	}
	
	VertexAngles.Sort([](const TPair<int32, float>& A, const TPair<int32, float>& B)
	{
		return A.Value < B.Value;
	});
	
	TArray<FVector2D> SortedVertices;
	SortedVertices.Reserve(InVertices.Num());
	for (const auto& VertexAngle : VertexAngles)
	{
		SortedVertices.Add(InVertices[VertexAngle.Key]);
	}
	
	OutVertices.Empty();
	OutVertices = MoveTemp(SortedVertices);
	return true;
}
