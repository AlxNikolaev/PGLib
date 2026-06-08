#include "Generators/CellDungeon2D/CellDungeonDebug.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogCellDungeon, Log, All);

namespace
{
	// Single-character code for a cell state, used by the text dump.
	TCHAR StateChar(ECellState State)
	{
		switch (State)
		{
			case ECellState::Empty:
				return TEXT('E');
			case ECellState::RoomOccupied:
				return TEXT('R');
			case ECellState::Corridor:
				return TEXT('C');
			case ECellState::Blocked:
				return TEXT('B');
			default:
				return TEXT('?');
		}
	}

	// Counts connected components over the {RoomOccupied UNION Corridor} cells using cell
	// neighbor adjacency (BFS). Cells that are Empty/Blocked or invalid are excluded.
	int32 CountRegions(const FCellDungeonResult& R)
	{
		const int32 NumCells = R.Diagram.Cells.Num();

		auto IsMember = [&R, NumCells](int32 Idx) -> bool {
			if (Idx < 0 || Idx >= NumCells)
			{
				return false;
			}
			if (Idx >= R.CellState.Num())
			{
				return false;
			}
			const ECellState S = R.CellState[Idx];
			return S == ECellState::RoomOccupied || S == ECellState::Corridor;
		};

		TArray<bool> Visited;
		Visited.Init(false, NumCells);

		int32		  Regions = 0;
		TArray<int32> Stack;

		for (int32 Start = 0; Start < NumCells; ++Start)
		{
			if (Visited[Start] || !IsMember(Start))
			{
				continue;
			}

			++Regions;
			Stack.Reset();
			Stack.Push(Start);
			Visited[Start] = true;

			while (Stack.Num() > 0)
			{
				const int32 Cur = Stack.Pop(EAllowShrinking::No);
				for (const int32 Nbr : R.Diagram.Cells[Cur].Neighbors)
				{
					if (Nbr >= 0 && Nbr < NumCells && !Visited[Nbr] && IsMember(Nbr))
					{
						Visited[Nbr] = true;
						Stack.Push(Nbr);
					}
				}
			}
		}

		return Regions;
	}

	// Counts Corridor cells that also report a room index (a corridor carved inside a room
	// footprint), which is an invalid layout.
	int32 CountCrossings(const FCellDungeonResult& R)
	{
		const int32 NumCells = R.Diagram.Cells.Num();
		int32		Crossings = 0;
		for (int32 Idx = 0; Idx < NumCells; ++Idx)
		{
			if (Idx >= R.CellState.Num())
			{
				break;
			}
			if (R.CellState[Idx] != ECellState::Corridor)
			{
				continue;
			}
			if (Idx < R.CellRoomIndex.Num() && R.CellRoomIndex[Idx] != INDEX_NONE)
			{
				++Crossings;
			}
		}
		return Crossings;
	}

	// Builds the canonical VALID summary line shared by the text and SVG renderings.
	FString MakeValidLine(const FCellDungeonResult& R, int32& OutRegions, int32& OutCrossings)
	{
		OutRegions = CountRegions(R);
		OutCrossings = CountCrossings(R);

		const int32 Placed = R.Rooms.Num();
		const int32 Requested = R.RequestedRoomCount;
		const bool	bOk = (Placed == Requested) && (OutRegions == 1) && (OutCrossings == 0);

		return FString::Printf(TEXT("VALID ok=%d rooms=%d/%d regions=%d crossings=%d"), bOk ? 1 : 0, Placed, Requested, OutRegions, OutCrossings);
	}

	// Escapes the small set of characters that are unsafe inside SVG text content.
	FString XmlEscape(const FString& In)
	{
		FString Out = In;
		Out.ReplaceInline(TEXT("&"), TEXT("&amp;"));
		Out.ReplaceInline(TEXT("<"), TEXT("&lt;"));
		Out.ReplaceInline(TEXT(">"), TEXT("&gt;"));
		return Out;
	}

	// Room fill color BY TYPE: start = green, end = red, each middle type its own hue.
	const TCHAR* RoomColor(int32 TypeIndex)
	{
		if (TypeIndex == -2)
		{
			return TEXT("#2e9e3f"); // start = green
		}
		if (TypeIndex == -3)
		{
			return TEXT("#c0392b"); // end = red
		}
		static const TCHAR* Palette[] = { TEXT("#4f9dff"), TEXT("#ffb02e"), TEXT("#b06bd9"), TEXT("#2ec4b6"), TEXT("#e85d9b"), TEXT("#9bcf3a") };
		const int32			N = UE_ARRAY_COUNT(Palette);
		return Palette[((TypeIndex % N) + N) % N];
	}

	// Uniform Catmull-Rom through control points (smooth corridor centerline, mirrors the in-engine
	// ESplinePointType::Curve look). SegSamples points per segment.
	TArray<FVector2D> SampleCatmullRom(const TArray<FVector2D>& P, int32 SegSamples)
	{
		TArray<FVector2D> Out;
		if (P.Num() < 3)
		{
			Out = P;
			return Out;
		}
		auto Pt = [&](int32 i) -> FVector2D { return P[FMath::Clamp(i, 0, P.Num() - 1)]; };
		for (int32 i = 0; i < P.Num() - 1; ++i)
		{
			const FVector2D P0 = Pt(i - 1), P1 = Pt(i), P2 = Pt(i + 1), P3 = Pt(i + 2);
			for (int32 s = 0; s < SegSamples; ++s)
			{
				const float		t = static_cast<float>(s) / static_cast<float>(SegSamples);
				const float		t2 = t * t, t3 = t2 * t;
				const FVector2D V =
					(P1 * 2.0f + (P2 - P0) * t + (P0 * 2.0f - P1 * 5.0f + P2 * 4.0f - P3) * t2 + (P1 * 3.0f - P0 - P2 * 3.0f + P3) * t3) * 0.5f;
				Out.Add(V);
			}
		}
		Out.Add(P.Last());
		return Out;
	}

	// Offset a polyline to a closed ribbon polygon of the given width (per-vertex normals).
	TArray<FVector2D> BuildRibbon(const TArray<FVector2D>& P, float Width)
	{
		TArray<FVector2D> Poly;
		const int32		  N = P.Num();
		if (N < 2)
		{
			return Poly;
		}
		auto SegNormal = [&](int32 a, int32 b) -> FVector2D {
			const FVector2D D = (P[b] - P[a]).GetSafeNormal();
			return FVector2D(-D.Y, D.X);
		};
		TArray<FVector2D> Nrm;
		Nrm.SetNum(N);
		Nrm[0] = SegNormal(0, 1);
		Nrm[N - 1] = SegNormal(N - 2, N - 1);
		for (int32 i = 1; i < N - 1; ++i)
		{
			Nrm[i] = (SegNormal(i - 1, i) + SegNormal(i, i + 1)).GetSafeNormal();
		}
		const float H = Width * 0.5f;
		for (int32 i = 0; i < N; ++i)
		{
			Poly.Add(P[i] + Nrm[i] * H);
		}
		for (int32 i = N - 1; i >= 0; --i)
		{
			Poly.Add(P[i] - Nrm[i] * H);
		}
		return Poly;
	}
} // namespace

FString FCellDungeonDebug::ToText(const FCellDungeonResult& R)
{
	int32		  Regions = 0;
	int32		  Crossings = 0;
	const FString ValidLine = MakeValidLine(R, Regions, Crossings);

	FString Out;
	Out += TEXT("# CellDungeon debug dump\n");
	Out += ValidLine;
	Out += TEXT("\n");

	// Per-cell lines.
	const int32 NumCells = R.Diagram.Cells.Num();
	for (int32 Idx = 0; Idx < NumCells; ++Idx)
	{
		const FVoronoiCell2D& Cell = R.Diagram.Cells[Idx];
		const ECellState	  State = (Idx < R.CellState.Num()) ? R.CellState[Idx] : ECellState::Empty;
		const int32			  RoomIdx = (Idx < R.CellRoomIndex.Num()) ? R.CellRoomIndex[Idx] : INDEX_NONE;

		Out += FString::Printf(
			TEXT("CELL %d state=%c room=%d site=%.1f,%.1f\n"), Idx, StateChar(State), RoomIdx, Cell.SiteLocation.X, Cell.SiteLocation.Y);
	}

	// Per-room lines.
	for (int32 Idx = 0; Idx < R.Rooms.Num(); ++Idx)
	{
		const FCellPlacedRoom& Room = R.Rooms[Idx];
		const float			   HalfX = Room.Footprint.X * 0.5f;
		const float			   HalfY = Room.Footprint.Y * 0.5f;
		Out += FString::Printf(TEXT("ROOM %d center=%.1f,%.1f rot=%.1f half=%.1f,%.1f type=%d\n"),
			Idx,
			Room.Center.X,
			Room.Center.Y,
			Room.RotationDeg,
			HalfX,
			HalfY,
			Room.TypeIndex);
	}

	// Per-corridor lines.
	for (int32 Idx = 0; Idx < R.CorridorPaths.Num(); ++Idx)
	{
		const TArray<int32>& Path = R.CorridorPaths[Idx];
		FString				 CellList;
		for (int32 j = 0; j < Path.Num(); ++j)
		{
			if (j > 0)
			{
				CellList += TEXT(",");
			}
			CellList += FString::FromInt(Path[j]);
		}
		Out += FString::Printf(TEXT("CORR %d cells=%s\n"), Idx, *CellList);
	}

	// Placement (coarse) diagram blob cells: full polygon vertices so the renderer can draw the
	// room-layout grid faintly behind the result. "PCELL <coarseIdx> verts=x,y;x,y;..."
	for (const int32 CoarseIdx : R.PlacementBlobCells)
	{
		if (!R.PlacementDiagram.Cells.IsValidIndex(CoarseIdx))
		{
			continue;
		}
		const FVoronoiCell2D& Cell = R.PlacementDiagram.Cells[CoarseIdx];
		if (Cell.Vertices.Num() < 3)
		{
			continue;
		}
		FString Verts;
		for (int32 v = 0; v < Cell.Vertices.Num(); ++v)
		{
			if (v > 0)
			{
				Verts += TEXT(";");
			}
			Verts += FString::Printf(TEXT("%.1f,%.1f"), Cell.Vertices[v].X, Cell.Vertices[v].Y);
		}
		Out += FString::Printf(TEXT("PCELL %d verts=%s\n"), CoarseIdx, *Verts);
	}

	return Out;
}

FString FCellDungeonDebug::ToSvg(const FCellDungeonResult& R)
{
	int32		  Regions = 0;
	int32		  Crossings = 0;
	const FString ValidLine = MakeValidLine(R, Regions, Crossings);

	const int32 NumCells = R.Diagram.Cells.Num();
	const float CorridorW = (R.CorridorWidth > 1.f) ? R.CorridorWidth : 300.f;

	// World-space corners (CCW) of a placed room rect.
	auto RoomCorners = [](const FCellPlacedRoom& Room) -> TArray<FVector2D> {
		const float		  HalfX = Room.Footprint.X * 0.5f;
		const float		  HalfY = Room.Footprint.Y * 0.5f;
		const float		  Rad = FMath::DegreesToRadians(Room.RotationDeg);
		const float		  CosR = FMath::Cos(Rad);
		const float		  SinR = FMath::Sin(Rad);
		const FVector2D	  Local[4] = { FVector2D(-HalfX, -HalfY), FVector2D(HalfX, -HalfY), FVector2D(HalfX, HalfY), FVector2D(-HalfX, HalfY) };
		TArray<FVector2D> Out;
		for (int32 c = 0; c < 4; ++c)
		{
			Out.Add(FVector2D(Room.Center.X + (Local[c].X * CosR - Local[c].Y * SinR), Room.Center.Y + (Local[c].X * SinR + Local[c].Y * CosR)));
		}
		return Out;
	};

	// Pre-sample each corridor centerline (smooth Catmull-Rom through its cell sites).
	TArray<TArray<FVector2D>> CorridorCurves;
	for (const TArray<int32>& Path : R.CorridorPaths)
	{
		TArray<FVector2D> Sites;
		for (const int32 CellIdx : Path)
		{
			if (CellIdx >= 0 && CellIdx < NumCells)
			{
				Sites.Add(R.Diagram.Cells[CellIdx].SiteLocation);
			}
		}
		if (Sites.Num() >= 2)
		{
			CorridorCurves.Add(SampleCatmullRom(Sites, 12));
		}
	}

	// --- Content bounds: rooms + placement cells + corridor curves (NOT the whole diagram). ---
	FBox2D BB(ForceInit);
	bool   bHave = false;
	auto   Acc = [&](const FVector2D& P) {
		  if (!bHave)
		  {
			  BB.Min = P;
			  BB.Max = P;
			  bHave = true;
		  }
		  else
		  {
			  BB += P;
		  }
	};
	for (const FCellPlacedRoom& Room : R.Rooms)
	{
		for (const FVector2D& C : RoomCorners(Room))
		{
			Acc(C);
		}
	}
	for (const int32 CoarseIdx : R.PlacementBlobCells)
	{
		if (R.PlacementDiagram.Cells.IsValidIndex(CoarseIdx))
		{
			for (const FVector2D& V : R.PlacementDiagram.Cells[CoarseIdx].Vertices)
			{
				Acc(V);
			}
		}
	}
	for (const TArray<FVector2D>& Curve : CorridorCurves)
	{
		for (const FVector2D& P : Curve)
		{
			Acc(P);
		}
	}
	if (!bHave)
	{
		BB.Min = FVector2D(-1.f, -1.f);
		BB.Max = FVector2D(1.f, 1.f);
	}

	const float Pad = FMath::Max(CorridorW, 300.f);
	const float MinX = BB.Min.X - Pad;
	const float MinY = BB.Min.Y - Pad;
	const float MaxX = BB.Max.X + Pad;
	const float MaxY = BB.Max.Y + Pad;
	const float W = FMath::Max(1.f, MaxX - MinX);
	const float H = FMath::Max(1.f, MaxY - MinY);

	// World -> SVG-units (flip Y). viewBox spans [0,W]x[0,H]; width/height px scaled to fit on screen.
	auto SX = [&](float Wx) -> float { return Wx - MinX; };
	auto SY = [&](float Wy) -> float { return MaxY - Wy; };

	const float MaxPx = 1000.f;
	const float Scale = MaxPx / FMath::Max(W, H);
	const float PxW = W * Scale;
	const float PxH = H * Scale;
	const float Label = FMath::Max(W, H) * 0.018f; // text size in world units

	FString Svg;
	Svg += TEXT("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
	Svg += FString::Printf(
		TEXT("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %.1f %.1f\" width=\"%.0f\" height=\"%.0f\">\n"), W, H, PxW, PxH);
	Svg += FString::Printf(TEXT("<rect x=\"0\" y=\"0\" width=\"%.1f\" height=\"%.1f\" fill=\"#101014\"/>\n"), W, H);

	// Placement (coarse) diagram, faint, behind everything.
	for (const int32 CoarseIdx : R.PlacementBlobCells)
	{
		if (!R.PlacementDiagram.Cells.IsValidIndex(CoarseIdx))
		{
			continue;
		}
		const FVoronoiCell2D& Cell = R.PlacementDiagram.Cells[CoarseIdx];
		if (Cell.Vertices.Num() < 3)
		{
			continue;
		}
		FString Points;
		for (int32 v = 0; v < Cell.Vertices.Num(); ++v)
		{
			Points += FString::Printf(TEXT("%s%.1f,%.1f"), (v > 0 ? TEXT(" ") : TEXT("")), SX(Cell.Vertices[v].X), SY(Cell.Vertices[v].Y));
		}
		Svg += FString::Printf(
			TEXT(
				"<polygon points=\"%s\" fill=\"#ffd27f\" fill-opacity=\"0.16\" stroke=\"#ffd27f\" stroke-opacity=\"0.30\" stroke-width=\"%.1f\"/>\n"),
			*Points,
			Label * 0.08f);
	}

	// Corridors: smooth true-width ribbons + thin centerline.
	for (const TArray<FVector2D>& Curve : CorridorCurves)
	{
		const TArray<FVector2D> Ribbon = BuildRibbon(Curve, CorridorW);
		if (Ribbon.Num() >= 3)
		{
			FString Points;
			for (int32 i = 0; i < Ribbon.Num(); ++i)
			{
				Points += FString::Printf(TEXT("%s%.1f,%.1f"), (i > 0 ? TEXT(" ") : TEXT("")), SX(Ribbon[i].X), SY(Ribbon[i].Y));
			}
			Svg += FString::Printf(TEXT("<polygon points=\"%s\" fill=\"#5a6680\" fill-opacity=\"0.55\" stroke=\"none\"/>\n"), *Points);
		}
		FString Line;
		for (int32 i = 0; i < Curve.Num(); ++i)
		{
			Line += FString::Printf(TEXT("%s%.1f,%.1f"), (i > 0 ? TEXT(" ") : TEXT("")), SX(Curve[i].X), SY(Curve[i].Y));
		}
		Svg += FString::Printf(TEXT("<polyline points=\"%s\" fill=\"none\" stroke=\"#aab4cc\" stroke-width=\"%.1f\"/>\n"), *Line, Label * 0.10f);
	}

	// Rooms filled by TYPE (start green, end red, per-type hue) + a center label.
	for (int32 Idx = 0; Idx < R.Rooms.Num(); ++Idx)
	{
		const FCellPlacedRoom&	Room = R.Rooms[Idx];
		const TArray<FVector2D> Corners = RoomCorners(Room);
		FString					Points;
		for (int32 c = 0; c < Corners.Num(); ++c)
		{
			Points += FString::Printf(TEXT("%s%.1f,%.1f"), (c > 0 ? TEXT(" ") : TEXT("")), SX(Corners[c].X), SY(Corners[c].Y));
		}
		Svg += FString::Printf(TEXT("<polygon points=\"%s\" fill=\"%s\" fill-opacity=\"0.95\" stroke=\"#eef2f7\" stroke-width=\"%.1f\"/>\n"),
			*Points,
			RoomColor(Room.TypeIndex),
			Label * 0.10f);

		FString Lbl = (Room.TypeIndex == -2) ? TEXT("S") : (Room.TypeIndex == -3) ? TEXT("E") : FString::Printf(TEXT("t%d"), Room.TypeIndex);
		Svg += FString::Printf(TEXT("<text x=\"%.1f\" y=\"%.1f\" fill=\"#ffffff\" font-family=\"sans-serif\" font-size=\"%.1f\" font-weight=\"bold\" "
									"text-anchor=\"middle\" dominant-baseline=\"central\">%s</text>\n"),
			SX(Room.Center.X),
			SY(Room.Center.Y),
			Label,
			*Lbl);
	}

	// Title with the VALID line.
	Svg += FString::Printf(TEXT("<text x=\"%.1f\" y=\"%.1f\" fill=\"#cfd6e0\" font-family=\"monospace\" font-size=\"%.1f\">%s</text>\n"),
		Label * 0.5f,
		Label * 1.4f,
		Label,
		*XmlEscape(ValidLine));

	Svg += TEXT("</svg>\n");
	return Svg;
}

FString FCellDungeonDebug::DumpToFiles(const FCellDungeonResult& R, const FString& BaseName)
{
	const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Logs"), TEXT("CellDungeon"));

	IFileManager& FileManager = IFileManager::Get();
	if (!FileManager.DirectoryExists(*Dir))
	{
		if (!FileManager.MakeDirectory(*Dir, /*Tree=*/true))
		{
			UE_LOG(LogCellDungeon, Warning, TEXT("DumpToFiles: failed to create directory '%s'"), *Dir);
			return FString();
		}
	}

	const FString TxtPath = FPaths::Combine(Dir, BaseName + TEXT(".txt"));
	const FString SvgPath = FPaths::Combine(Dir, BaseName + TEXT(".svg"));

	const FString TextContent = ToText(R);
	const FString SvgContent = ToSvg(R);

	if (!FFileHelper::SaveStringToFile(TextContent, *TxtPath))
	{
		UE_LOG(LogCellDungeon, Warning, TEXT("DumpToFiles: failed to write '%s'"), *TxtPath);
		return FString();
	}

	if (!FFileHelper::SaveStringToFile(SvgContent, *SvgPath))
	{
		UE_LOG(LogCellDungeon, Warning, TEXT("DumpToFiles: failed to write '%s'"), *SvgPath);
		return FString();
	}

	const FString AbsTxtPath = FPaths::ConvertRelativePathToFull(TxtPath);
	UE_LOG(LogCellDungeon, Log, TEXT("DumpToFiles: wrote '%s' and '%s'"), *AbsTxtPath, *FPaths::ConvertRelativePathToFull(SvgPath));
	return AbsTxtPath;
}
