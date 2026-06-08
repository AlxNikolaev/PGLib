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

	// Fill color for a cell polygon by state.
	const TCHAR* StateFill(ECellState State)
	{
		switch (State)
		{
			case ECellState::Empty:
				return TEXT("#1a1a1f"); // dark
			case ECellState::Corridor:
				return TEXT("#2f6fd0"); // blue
			case ECellState::RoomOccupied:
				return TEXT("#566273"); // slate
			case ECellState::Blocked:
				return TEXT("#08080a"); // near-black
			default:
				return TEXT("#000000");
		}
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

	// Fit a viewBox to the diagram bounds with padding. Y is flipped for screen space, so a
	// world point (x, y) maps to screen (x, -y); we offset by MaxY to keep coordinates positive.
	const FBox2D& B = R.Diagram.Bounds;
	const float	  Pad = 200.f;

	float MinX = B.Min.X;
	float MinY = B.Min.Y;
	float MaxX = B.Max.X;
	float MaxY = B.Max.Y;

	// Guard against degenerate/empty bounds.
	if (!(MaxX > MinX) || !(MaxY > MinY))
	{
		MinX = -1.f;
		MinY = -1.f;
		MaxX = 1.f;
		MaxY = 1.f;
	}

	const float Width = (MaxX - MinX) + 2.f * Pad;
	const float Height = (MaxY - MinY) + 2.f * Pad;

	// World -> screen helpers (flip Y).
	auto SX = [&](float Wx) -> float { return Wx - MinX + Pad; };
	auto SY = [&](float Wy) -> float { return (MaxY - Wy) + Pad; };

	FString Svg;
	Svg += TEXT("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
	Svg += FString::Printf(
		TEXT("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %.1f %.1f\" width=\"%.1f\" height=\"%.1f\">\n"), Width, Height, Width, Height);

	// Background.
	Svg += FString::Printf(TEXT("<rect x=\"0\" y=\"0\" width=\"%.1f\" height=\"%.1f\" fill=\"#0d0d10\"/>\n"), Width, Height);

	// Cell polygons filled by state.
	const int32 NumCells = R.Diagram.Cells.Num();
	for (int32 Idx = 0; Idx < NumCells; ++Idx)
	{
		const FVoronoiCell2D& Cell = R.Diagram.Cells[Idx];
		if (Cell.Vertices.Num() < 3)
		{
			continue;
		}

		const ECellState State = (Idx < R.CellState.Num()) ? R.CellState[Idx] : ECellState::Empty;

		FString Points;
		for (int32 v = 0; v < Cell.Vertices.Num(); ++v)
		{
			if (v > 0)
			{
				Points += TEXT(" ");
			}
			Points += FString::Printf(TEXT("%.1f,%.1f"), SX(Cell.Vertices[v].X), SY(Cell.Vertices[v].Y));
		}

		Svg += FString::Printf(
			TEXT("<polygon points=\"%s\" fill=\"%s\" stroke=\"#000000\" stroke-width=\"1\" stroke-opacity=\"0.4\"/>\n"), *Points, StateFill(State));
	}

	// Placed room rotated-rect outlines (green=start, red=end, gray=middle).
	for (int32 Idx = 0; Idx < R.Rooms.Num(); ++Idx)
	{
		const FCellPlacedRoom& Room = R.Rooms[Idx];

		const TCHAR* Stroke = TEXT("#9aa0a6"); // gray (middle)
		if (Room.TypeIndex == -2)
		{
			Stroke = TEXT("#33d17a"); // green (start)
		}
		else if (Room.TypeIndex == -3)
		{
			Stroke = TEXT("#e01b24"); // red (end)
		}

		const float HalfX = Room.Footprint.X * 0.5f;
		const float HalfY = Room.Footprint.Y * 0.5f;

		const float Rad = FMath::DegreesToRadians(Room.RotationDeg);
		const float CosR = FMath::Cos(Rad);
		const float SinR = FMath::Sin(Rad);

		// Local corners (CCW) rotated and translated to world, then projected to screen.
		const FVector2D Local[4] = { FVector2D(-HalfX, -HalfY), FVector2D(HalfX, -HalfY), FVector2D(HalfX, HalfY), FVector2D(-HalfX, HalfY) };

		FString Points;
		for (int32 c = 0; c < 4; ++c)
		{
			const float Wx = Room.Center.X + (Local[c].X * CosR - Local[c].Y * SinR);
			const float Wy = Room.Center.Y + (Local[c].X * SinR + Local[c].Y * CosR);
			if (c > 0)
			{
				Points += TEXT(" ");
			}
			Points += FString::Printf(TEXT("%.1f,%.1f"), SX(Wx), SY(Wy));
		}

		Svg += FString::Printf(TEXT("<polygon points=\"%s\" fill=\"none\" stroke=\"%s\" stroke-width=\"4\"/>\n"), *Points, Stroke);

		// Doorways: short outward tick marks.
		const int32 NumDoors = FMath::Min(Room.DoorwayPos.Num(), Room.DoorwayDir.Num());
		for (int32 d = 0; d < NumDoors; ++d)
		{
			const FVector2D P = Room.DoorwayPos[d];
			const FVector2D Dir = Room.DoorwayDir[d];
			const FVector2D Tip = P + Dir * 120.f;
			Svg += FString::Printf(TEXT("<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" y2=\"%.1f\" stroke=\"#f6d32d\" stroke-width=\"3\"/>\n"),
				SX(P.X),
				SY(P.Y),
				SX(Tip.X),
				SY(Tip.Y));
		}
	}

	// Corridor paths drawn as polylines through cell sites.
	for (int32 Idx = 0; Idx < R.CorridorPaths.Num(); ++Idx)
	{
		const TArray<int32>& Path = R.CorridorPaths[Idx];
		if (Path.Num() < 2)
		{
			continue;
		}

		FString Points;
		bool	bAny = false;
		for (int32 j = 0; j < Path.Num(); ++j)
		{
			const int32 CellIdx = Path[j];
			if (CellIdx < 0 || CellIdx >= NumCells)
			{
				continue;
			}
			const FVector2D Site = R.Diagram.Cells[CellIdx].SiteLocation;
			if (bAny)
			{
				Points += TEXT(" ");
			}
			Points += FString::Printf(TEXT("%.1f,%.1f"), SX(Site.X), SY(Site.Y));
			bAny = true;
		}

		if (bAny)
		{
			Svg += FString::Printf(
				TEXT("<polyline points=\"%s\" fill=\"none\" stroke=\"#62a0ea\" stroke-width=\"2\" stroke-opacity=\"0.8\"/>\n"), *Points);
		}
	}

	// Title text with the VALID line.
	Svg += FString::Printf(TEXT("<text x=\"%.1f\" y=\"%.1f\" fill=\"#ffffff\" font-family=\"monospace\" font-size=\"%.1f\">%s</text>\n"),
		Pad * 0.25f,
		Pad * 0.6f,
		Pad * 0.4f,
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
