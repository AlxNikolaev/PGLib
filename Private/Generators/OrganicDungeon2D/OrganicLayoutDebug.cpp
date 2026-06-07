// Fill out your copyright notice in the Description page of Project Settings.

#include "Generators/OrganicDungeon2D/OrganicLayoutDebug.h"

#include "Generators/OrganicDungeon2D/OrganicDungeonGenerator2D.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace OrganicLayoutDebugImpl
{
	// Four oriented-bounding-box corners of a room (world XY), CCW from bottom-left.
	void RoomCorners(const FOrganicRoom& Room, FVector2D OutCorners[4])
	{
		const float		C = FMath::Cos(FMath::DegreesToRadians(Room.RotationDeg));
		const float		S = FMath::Sin(FMath::DegreesToRadians(Room.RotationDeg));
		const FVector2D AxisX(C, S);
		const FVector2D AxisY(-S, C);
		const FVector2D HX = AxisX * Room.HalfExtent.X;
		const FVector2D HY = AxisY * Room.HalfExtent.Y;
		OutCorners[0] = Room.Center - HX - HY;
		OutCorners[1] = Room.Center + HX - HY;
		OutCorners[2] = Room.Center + HX + HY;
		OutCorners[3] = Room.Center - HX + HY;
	}

	void AccumulateBounds(const FOrganicDungeonGridData& Grid, FVector2D& OutMin, FVector2D& OutMax)
	{
		OutMin = FVector2D(BIG_NUMBER, BIG_NUMBER);
		OutMax = FVector2D(-BIG_NUMBER, -BIG_NUMBER);
		auto Add = [&](const FVector2D& P) {
			OutMin.X = FMath::Min(OutMin.X, P.X);
			OutMin.Y = FMath::Min(OutMin.Y, P.Y);
			OutMax.X = FMath::Max(OutMax.X, P.X);
			OutMax.Y = FMath::Max(OutMax.Y, P.Y);
		};
		for (const FOrganicRoom& Room : Grid.Rooms)
		{
			FVector2D Corners[4];
			RoomCorners(Room, Corners);
			for (const FVector2D& Pt : Corners)
			{
				Add(Pt);
			}
		}
		for (const FOrganicCorridor& Cor : Grid.Corridors)
		{
			for (const FVector2D& Pt : Cor.Centerline)
			{
				Add(Pt);
			}
		}
		for (const FOrganicJunction& J : Grid.Junctions)
		{
			Add(J.Center + FVector2D(J.Radius, J.Radius));
			Add(J.Center - FVector2D(J.Radius, J.Radius));
		}
		if (OutMin.X > OutMax.X) // empty layout
		{
			OutMin = FVector2D::ZeroVector;
			OutMax = FVector2D(1.0f, 1.0f);
		}
	}

	// True if a point lies inside (or on) a convex quad given in CCW order. Used to detect a corridor sample
	// that starts/ends inside a room body (a crossing the segment test alone could miss for very short spans).
	bool PointInQuad(const FVector2D& P, const FVector2D Quad[4])
	{
		for (int32 i = 0; i < 4; ++i)
		{
			const FVector2D A = Quad[i];
			const FVector2D B = Quad[(i + 1) % 4];
			const FVector2D Edge = B - A;
			const FVector2D ToP = P - A;
			// CCW quad: an interior point is to the left of (or on) every directed edge.
			if (Edge.X * ToP.Y - Edge.Y * ToP.X < -1.0f) // small slack so on-edge doorway points are not "inside"
			{
				return false;
			}
		}
		return true;
	}

	// True if segment [P0,P1] intersects segment [P2,P3] (proper or touching).
	bool SegmentsIntersect(const FVector2D& P0, const FVector2D& P1, const FVector2D& P2, const FVector2D& P3)
	{
		auto			Cross = [](const FVector2D& A, const FVector2D& B) { return A.X * B.Y - A.Y * B.X; };
		const FVector2D R = P1 - P0;
		const FVector2D S = P3 - P2;
		const float		Denom = Cross(R, S);
		const FVector2D QP = P2 - P0;
		if (FMath::IsNearlyZero(Denom)) // parallel — treat as non-crossing (collinear overlap is rare and not a body crossing)
		{
			return false;
		}
		const float T = Cross(QP, S) / Denom;
		const float U = Cross(QP, R) / Denom;
		return T >= 0.0f && T <= 1.0f && U >= 0.0f && U <= 1.0f;
	}

	// True if a corridor centerline segment [P0,P1] genuinely passes through a room body (OBB Quad). A shallow
	// doorway-approach touch is excluded by requiring an actual interior crossing: either an endpoint sits inside
	// the quad, or the segment crosses two of its edges (enter + exit) rather than merely grazing one.
	bool SegmentCrossesRoomBody(const FVector2D& P0, const FVector2D& P1, const FVector2D Quad[4])
	{
		if (PointInQuad(P0, Quad) || PointInQuad(P1, Quad))
		{
			return true;
		}
		int32 Hits = 0;
		for (int32 e = 0; e < 4; ++e)
		{
			if (SegmentsIntersect(P0, P1, Quad[e], Quad[(e + 1) % 4]))
			{
				++Hits;
			}
		}
		// A segment that merely grazes one edge near a doorway touches once; a genuine body crossing enters and
		// exits (two edge hits). Require >=2 so doorway-approach spans are not over-reported.
		return Hits >= 2;
	}

	// Builds an SVG path ("M..C..") that smooths a polyline with a Catmull-Rom spline converted to cubic
	// Beziers, so the few-point stored centerline renders as the visible curve. ToCanvas maps a world point to
	// canvas space. For < 3 points it falls back to a straight "M L" path. Pure string building.
	FString CatmullRomSvgPath(const TArray<FVector2D>& Pts, const TFunctionRef<FVector2D(const FVector2D&)>& ToCanvas)
	{
		if (Pts.Num() < 2)
		{
			return FString();
		}
		auto Canvas = [&](int32 i) {
			const int32 Clamped = FMath::Clamp(i, 0, Pts.Num() - 1);
			return ToCanvas(Pts[Clamped]);
		};
		const FVector2D P0 = Canvas(0);
		FString			Path = FString::Printf(TEXT("M %.0f %.0f "), P0.X, P0.Y);
		if (Pts.Num() == 2)
		{
			const FVector2D P1 = Canvas(1);
			Path += FString::Printf(TEXT("L %.0f %.0f"), P1.X, P1.Y);
			return Path;
		}
		// Catmull-Rom -> cubic Bezier control points (tension 0.5) per segment.
		for (int32 i = 0; i + 1 < Pts.Num(); ++i)
		{
			const FVector2D Pm1 = Canvas(i - 1);
			const FVector2D Pc = Canvas(i);
			const FVector2D Pn = Canvas(i + 1);
			const FVector2D Pn2 = Canvas(i + 2);
			const FVector2D C1 = Pc + (Pn - Pm1) / 6.0f;
			const FVector2D C2 = Pn - (Pn2 - Pc) / 6.0f;
			Path += FString::Printf(TEXT("C %.0f %.0f %.0f %.0f %.0f %.0f "), C1.X, C1.Y, C2.X, C2.Y, Pn.X, Pn.Y);
		}
		return Path;
	}

	// Counts corridor centerline segments that cross a room body that is NOT one of that corridor's own Room
	// anchors. Invariant: corridors never cross a room body (must be 0). Corridor-corridor overlap and junction
	// pass-through are legal and not counted (only ROOM bodies are obstacles).
	int32 CountCorridorRoomCrossings(const FOrganicDungeonGridData& Grid)
	{
		// Precompute room OBB corners once.
		TArray<TArray<FVector2D>> RoomQuads;
		RoomQuads.SetNum(Grid.Rooms.Num());
		for (int32 r = 0; r < Grid.Rooms.Num(); ++r)
		{
			FVector2D Corners[4];
			RoomCorners(Grid.Rooms[r], Corners);
			RoomQuads[r] = { Corners[0], Corners[1], Corners[2], Corners[3] };
		}

		int32 Crossings = 0;
		for (const FOrganicCorridor& Cor : Grid.Corridors)
		{
			const int32 AnchorRoomA = (Cor.AnchorA.Type == EOrganicAnchorType::Room) ? Cor.AnchorA.Index : INDEX_NONE;
			const int32 AnchorRoomB = (Cor.AnchorB.Type == EOrganicAnchorType::Room) ? Cor.AnchorB.Index : INDEX_NONE;
			for (int32 s = 0; s + 1 < Cor.Centerline.Num(); ++s)
			{
				const FVector2D& P0 = Cor.Centerline[s];
				const FVector2D& P1 = Cor.Centerline[s + 1];
				for (int32 r = 0; r < RoomQuads.Num(); ++r)
				{
					if (r == AnchorRoomA || r == AnchorRoomB)
					{
						continue; // a corridor legitimately enters its own endpoint rooms' doorways
					}
					if (SegmentCrossesRoomBody(P0, P1, RoomQuads[r].GetData()))
					{
						++Crossings;
						break; // one crossing per segment is enough to flag it
					}
				}
			}
		}
		return Crossings;
	}

	// Counts the connected components of the room-cell network. OrganicDungeon is gridless: connectivity is
	// corridor adjacency, not a grid flood-fill. The network nodes are {rooms ∪ junctions} and an edge joins two
	// nodes whenever a corridor (or a junction's host-corridor record) ties them together.
	//
	// Preferred source: the room-cell diagram's FLayoutCell2D.Neighbors — this is exactly the adjacency the runtime
	// ConvertLayoutToGridDiagram copies into the FVoronoiGridDiagram, so the diagnostic measures the SAME
	// connectivity the transition / portal path relies on. When the diagram has not been built yet (cells empty),
	// fall back to a BFS over corridor anchors + junction host-corridor records so the stat still works on the raw
	// vector layout.
	//
	// Returns the number of connected components over the occupied node set; an empty layout returns 0. A correct
	// gridless layout — every room joined via a free/aligned doorway or an on-demand junction — returns 1.
	int32 CountConnectedRegions(const FOrganicDungeonGridData& Grid)
	{
		// --- Preferred: the room-cell diagram (rooms first, then junctions), adjacency = Cell.Neighbors. ---
		const int32 NumCells = Grid.Diagram.Cells.Num();
		if (NumCells > 0)
		{
			TArray<bool> Visited;
			Visited.Init(false, NumCells);
			int32		  Components = 0;
			TArray<int32> Queue;
			for (int32 Seed = 0; Seed < NumCells; ++Seed)
			{
				if (Visited[Seed])
				{
					continue;
				}
				++Components;
				Queue.Reset();
				Queue.Add(Seed);
				Visited[Seed] = true;
				for (int32 Head = 0; Head < Queue.Num(); ++Head)
				{
					const int32 U = Queue[Head];
					for (const int32 V : Grid.Diagram.Cells[U].Neighbors)
					{
						if (V >= 0 && V < NumCells && !Visited[V])
						{
							Visited[V] = true;
							Queue.Add(V);
						}
					}
				}
			}
			return Components;
		}

		// --- Fallback (pre-diagram): BFS over the raw vector layout. Node ids are [0, Rooms.Num()) for rooms and a
		//     disjoint block [Rooms.Num(), Rooms.Num()+Junctions.Num()) for junctions. A corridor joins its two
		//     Room/Junction anchors; a Free anchor (open dead-end stub) is a non-joining endpoint. Junction
		//     host-corridor records additionally bridge the junction into the corridor it was tapped onto. ---
		const int32 NumRooms = Grid.Rooms.Num();
		const int32 NumJunctions = Grid.Junctions.Num();
		const int32 NumNodes = NumRooms + NumJunctions;
		if (NumNodes == 0)
		{
			return 0;
		}

		TArray<TArray<int32>> Adj;
		Adj.SetNum(NumNodes);
		auto NodeOf = [&](const FOrganicAnchor& A) -> int32 {
			if (A.Type == EOrganicAnchorType::Room && A.Index >= 0 && A.Index < NumRooms)
			{
				return A.Index;
			}
			if (A.Type == EOrganicAnchorType::Junction && A.Index >= 0 && A.Index < NumJunctions)
			{
				return NumRooms + A.Index;
			}
			return INDEX_NONE; // Free / Corridor / out-of-range — not a network node here
		};
		auto Join = [&](int32 U, int32 V) {
			if (U != INDEX_NONE && V != INDEX_NONE && U != V)
			{
				Adj[U].AddUnique(V);
				Adj[V].AddUnique(U);
			}
		};
		for (const FOrganicCorridor& Cor : Grid.Corridors)
		{
			Join(NodeOf(Cor.AnchorA), NodeOf(Cor.AnchorB));
		}
		// Bridge each junction into the host corridor it was tapped onto (its two endpoint rooms/junctions).
		for (int32 j = 0; j < NumJunctions; ++j)
		{
			const int32 JNode = NumRooms + j;
			for (const FOrganicAnchor& Host : Grid.Junctions[j].HostCorridorAnchors)
			{
				Join(JNode, NodeOf(Host));
			}
		}

		TArray<bool> Visited;
		Visited.Init(false, NumNodes);
		int32		  Components = 0;
		TArray<int32> Queue;
		for (int32 Seed = 0; Seed < NumNodes; ++Seed)
		{
			if (Visited[Seed])
			{
				continue;
			}
			++Components;
			Queue.Reset();
			Queue.Add(Seed);
			Visited[Seed] = true;
			for (int32 Head = 0; Head < Queue.Num(); ++Head)
			{
				const int32 U = Queue[Head];
				for (const int32 V : Adj[U])
				{
					if (!Visited[V])
					{
						Visited[V] = true;
						Queue.Add(V);
					}
				}
			}
		}
		return Components;
	}
} // namespace OrganicLayoutDebugImpl

FString FOrganicLayoutDebug::ToText(const FOrganicDungeonGridData& Grid)
{
	using namespace OrganicLayoutDebugImpl;

	// The three gridless invariants the dump reports: corridors never cross a room body (must be 0), the room-cell
	// network is ONE connected region (corridor adjacency), and every requested room was placed.
	const int32 Crossings = CountCorridorRoomCrossings(Grid);
	const int32 Regions = CountConnectedRegions(Grid);

	FString Out;
	Out += TEXT("# OD layout dump v3\n");
	Out += FString::Printf(TEXT("START %d\n"), Grid.StartRoomIndex);
	Out += FString::Printf(TEXT("END %d\n"), Grid.EndRoomIndex);
	// Gridless STATS: node/edge counts only (no grid dims, no loop/link/spine — loop/link generation is gone and
	// junctions are an on-demand connectivity fallback, not a requested count). CellSize is carried for the
	// runtime zone-allocation length threshold.
	Out += FString::Printf(TEXT("STATS rooms=%d corridors=%d junctions=%d cellSize=%.1f\n"),
		Grid.Rooms.Num(),
		Grid.Corridors.Num(),
		Grid.Junctions.Num(),
		Grid.CellSize);

	// VALID: the gridless invariants on one stable, greppable line (the e2e loop greps "VALID " / "crossings=0").
	//   rooms=<placed>/<requested>  every requested room placed,
	//   regions=<n>                 the room-cell network is ONE connected component (corridor adjacency),
	//   crossings=<n>               no corridor crosses a room body (hard invariant — must be 0).
	// ok=1 only when all three hold. A room shortfall is logged by the generator (best-effort placement), so it is
	// surfaced here but the on-demand-junction fallback still guarantees regions==1 for whatever was placed.
	const int32 ReqRooms = Grid.RequestedRoomCount;
	const bool	bRoomsOk = Grid.Rooms.Num() >= ReqRooms;
	const bool	bRegionsOk = (Grid.Rooms.Num() == 0) ? (Regions == 0) : (Regions == 1);
	const bool	bOk = bRoomsOk && bRegionsOk && Crossings == 0;
	Out += FString::Printf(TEXT("VALID ok=%d rooms=%d/%d regions=%d crossings=%d\n"), bOk ? 1 : 0, Grid.Rooms.Num(), ReqRooms, Regions, Crossings);

	for (int32 i = 0; i < Grid.Rooms.Num(); ++i)
	{
		const FOrganicRoom& R = Grid.Rooms[i];
		Out += FString::Printf(TEXT("ROOM %d center=%.1f,%.1f rot=%.1f half=%.1f,%.1f type=%d loc=%d\n"),
			i,
			R.Center.X,
			R.Center.Y,
			R.RotationDeg,
			R.HalfExtent.X,
			R.HalfExtent.Y,
			R.TypeIndex,
			R.LocationIndex);
		for (int32 d = 0; d < R.Doorways.Num(); ++d)
		{
			const FOrganicDoorway& Dr = R.Doorways[d];
			Out += FString::Printf(TEXT("DOOR %d %d pos=%.1f,%.1f nrm=%.2f,%.2f used=%d w=%.1f\n"),
				i,
				d,
				Dr.Pos.X,
				Dr.Pos.Y,
				Dr.OutwardNormal.X,
				Dr.OutwardNormal.Y,
				Dr.bUsed ? 1 : 0,
				Dr.Width);
		}
	}

	// aType/bType are EOrganicAnchorType: 0=Room 1=Corridor 2=Free 3=Junction (Junction index -> JUNC lines below).
	for (int32 i = 0; i < Grid.Corridors.Num(); ++i)
	{
		const FOrganicCorridor& Cor = Grid.Corridors[i];
		Out += FString::Printf(TEXT("CORR %d aType=%d aIdx=%d bType=%d bIdx=%d npts=%d\n"),
			i,
			static_cast<int32>(Cor.AnchorA.Type),
			Cor.AnchorA.Index,
			static_cast<int32>(Cor.AnchorB.Type),
			Cor.AnchorB.Index,
			Cor.Centerline.Num());
		for (int32 p = 0; p < Cor.Centerline.Num(); ++p)
		{
			const float R = Cor.Radii.IsValidIndex(p) ? Cor.Radii[p] : 0.0f;
			Out += FString::Printf(TEXT("PT %d %d %.1f %.1f %.1f\n"), i, p, Cor.Centerline[p].X, Cor.Centerline[p].Y, R);
		}
	}

	for (int32 i = 0; i < Grid.Junctions.Num(); ++i)
	{
		const FOrganicJunction& J = Grid.Junctions[i];
		Out += FString::Printf(
			TEXT("JUNC %d center=%.1f,%.1f radius=%.1f npts=%d loc=%d\n"), i, J.Center.X, J.Center.Y, J.Radius, J.Perimeter.Num(), J.LocationIndex);
		for (int32 p = 0; p < J.Perimeter.Num(); ++p)
		{
			Out += FString::Printf(TEXT("JPT %d %d %.1f %.1f\n"), i, p, J.Perimeter[p].X, J.Perimeter[p].Y);
		}
	}

	return Out;
}

FString FOrganicLayoutDebug::ToSvg(const FOrganicDungeonGridData& Grid)
{
	using namespace OrganicLayoutDebugImpl;

	FVector2D MinB, MaxB;
	AccumulateBounds(Grid, MinB, MaxB);

	const float Margin = 500.0f;
	const float W = (MaxB.X - MinB.X) + 2.0f * Margin;
	const float H = (MaxB.Y - MinB.Y) + 2.0f * Margin;

	// World → SVG: shift into margin box and flip Y so up is up.
	auto X = [&](float Wx) { return Wx - MinB.X + Margin; };
	auto Y = [&](float Wy) { return MaxB.Y - Wy + Margin; };

	FString Svg;
	Svg += FString::Printf(
		TEXT("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %.0f %.0f\" width=\"1000\">\n"), FMath::Max(1.0f, W), FMath::Max(1.0f, H));
	Svg += FString::Printf(TEXT("<rect x=\"0\" y=\"0\" width=\"%.0f\" height=\"%.0f\" fill=\"#1b1b1f\"/>\n"), W, H);
	const float Stroke = FMath::Max(8.0f, (W + H) * 0.0015f);

	// Corridors (under rooms): gridless model has no per-corridor role — one wavy curve, one color. Drawn as a
	// smoothed (Catmull-Rom) curve so the few-point wavy centerline renders as the visible curve, not a polyline.
	const TCHAR* const CorridorColor = TEXT("#888");
	for (const FOrganicCorridor& Cor : Grid.Corridors)
	{
		if (Cor.Centerline.Num() < 2)
		{
			continue;
		}
		const float	  AvgR = Cor.Radii.Num() > 0 ? Cor.Radii[0] : Stroke;
		const FString Path = CatmullRomSvgPath(Cor.Centerline, [&](const FVector2D& P) { return FVector2D(X(P.X), Y(P.Y)); });
		Svg += FString::Printf(TEXT("<path d=\"%s\" fill=\"none\" stroke=\"%s\" stroke-width=\"%.0f\" stroke-opacity=\"0.5\" "
									"stroke-linecap=\"round\" stroke-linejoin=\"round\"/>\n"),
			*Path,
			CorridorColor,
			FMath::Max(Stroke, AvgR * 2.0f));
		// Centerline overlay (thin) so the route is visible inside the width band.
		Svg += FString::Printf(TEXT("<path d=\"%s\" fill=\"none\" stroke=\"%s\" stroke-width=\"%.0f\"/>\n"), *Path, CorridorColor, Stroke);
	}

	// Junctions (over corridors, under rooms): deformed-circle hubs. MakeJunction always builds a 16-vertex
	// perimeter, so a junction is always drawn as a polygon (the radius-only circle fallback was dead code,
	// and its unscaled r attribute was inconsistent with the X()/Y() world->canvas transform).
	for (int32 j = 0; j < Grid.Junctions.Num(); ++j)
	{
		const FOrganicJunction& J = Grid.Junctions[j];
		if (J.Perimeter.Num() < 3)
		{
			continue;
		}
		FString Pts;
		for (const FVector2D& P : J.Perimeter)
		{
			Pts += FString::Printf(TEXT("%.0f,%.0f "), X(P.X), Y(P.Y));
		}
		Svg += FString::Printf(
			TEXT("<polygon points=\"%s\" fill=\"#9b59b6\" fill-opacity=\"0.6\" stroke=\"#d2a8e8\" stroke-width=\"%.0f\"/>\n"), *Pts, Stroke);
		Svg += FString::Printf(TEXT("<text x=\"%.0f\" y=\"%.0f\" fill=\"#e8d2f5\" font-size=\"%.0f\" text-anchor=\"middle\">J%d</text>\n"),
			X(J.Center.X),
			Y(J.Center.Y),
			FMath::Max(100.0f, Stroke * 14.0f),
			j);
	}

	// Rooms.
	for (int32 i = 0; i < Grid.Rooms.Num(); ++i)
	{
		FVector2D Corners[4];
		RoomCorners(Grid.Rooms[i], Corners);
		FString Pts;
		for (const FVector2D& C : Corners)
		{
			Pts += FString::Printf(TEXT("%.0f,%.0f "), X(C.X), Y(C.Y));
		}
		const TCHAR* Fill = (i == Grid.StartRoomIndex) ? TEXT("#2e7d32") : ((i == Grid.EndRoomIndex) ? TEXT("#b00020") : TEXT("#3a4a66"));
		Svg += FString::Printf(
			TEXT("<polygon points=\"%s\" fill=\"%s\" fill-opacity=\"0.85\" stroke=\"#dfe6f0\" stroke-width=\"%.0f\"/>\n"), *Pts, Fill, Stroke);
		Svg += FString::Printf(TEXT("<text x=\"%.0f\" y=\"%.0f\" fill=\"#fff\" font-size=\"%.0f\" text-anchor=\"middle\">%d</text>\n"),
			X(Grid.Rooms[i].Center.X),
			Y(Grid.Rooms[i].Center.Y),
			FMath::Max(120.0f, Stroke * 18.0f),
			i);
	}

	Svg += TEXT("</svg>\n");
	return Svg;
}

FString FOrganicLayoutDebug::DumpToFiles(const FOrganicDungeonGridData& Grid, const FString& BaseName)
{
	const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Logs"), TEXT("OD"));
	IFileManager::Get().MakeDirectory(*Dir, /*Tree=*/true);

	const FString TxtPath = FPaths::Combine(Dir, BaseName + TEXT(".txt"));
	const FString SvgPath = FPaths::Combine(Dir, BaseName + TEXT(".svg"));

	const bool bTxt = FFileHelper::SaveStringToFile(ToText(Grid), *TxtPath);
	const bool bSvg = FFileHelper::SaveStringToFile(ToSvg(Grid), *SvgPath);

	return (bTxt && bSvg) ? TxtPath : FString();
}
