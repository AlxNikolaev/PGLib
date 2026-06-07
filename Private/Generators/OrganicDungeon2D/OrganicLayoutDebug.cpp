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
		if (OutMin.X > OutMax.X) // empty layout
		{
			OutMin = FVector2D::ZeroVector;
			OutMax = FVector2D(1.0f, 1.0f);
		}
	}
} // namespace OrganicLayoutDebugImpl

FString FOrganicLayoutDebug::ToText(const FOrganicDungeonGridData& Grid)
{
	using namespace OrganicLayoutDebugImpl;

	int32 SpineN = 0, LoopN = 0, LinkN = 0;
	for (const FOrganicCorridor& Cor : Grid.Corridors)
	{
		SpineN += Cor.bIsSpine ? 1 : 0;
		LoopN += Cor.bIsLoop ? 1 : 0;
		LinkN += Cor.bIsLink ? 1 : 0;
	}

	FString Out;
	Out += TEXT("# OD layout dump v1\n");
	Out += FString::Printf(
		TEXT("GRID %d %d %.1f %.1f %.1f\n"), Grid.GridWidth, Grid.GridHeight, Grid.CellSize, Grid.GridOriginWorld.X, Grid.GridOriginWorld.Y);
	Out += FString::Printf(TEXT("START %d\n"), Grid.StartRoomIndex);
	Out += FString::Printf(TEXT("END %d\n"), Grid.EndRoomIndex);
	Out += FString::Printf(
		TEXT("STATS rooms=%d corridors=%d spine=%d loops=%d links=%d\n"), Grid.Rooms.Num(), Grid.Corridors.Num(), SpineN, LoopN, LinkN);

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

	for (int32 i = 0; i < Grid.Corridors.Num(); ++i)
	{
		const FOrganicCorridor& Cor = Grid.Corridors[i];
		Out += FString::Printf(TEXT("CORR %d spine=%d loop=%d link=%d aType=%d bType=%d npts=%d\n"),
			i,
			Cor.bIsSpine ? 1 : 0,
			Cor.bIsLoop ? 1 : 0,
			Cor.bIsLink ? 1 : 0,
			static_cast<int32>(Cor.AnchorA.Type),
			static_cast<int32>(Cor.AnchorB.Type),
			Cor.Centerline.Num());
		for (int32 p = 0; p < Cor.Centerline.Num(); ++p)
		{
			const float R = Cor.Radii.IsValidIndex(p) ? Cor.Radii[p] : 0.0f;
			Out += FString::Printf(TEXT("PT %d %d %.1f %.1f %.1f\n"), i, p, Cor.Centerline[p].X, Cor.Centerline[p].Y, R);
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

	// Corridors (under rooms): color by role.
	for (const FOrganicCorridor& Cor : Grid.Corridors)
	{
		if (Cor.Centerline.Num() < 2)
		{
			continue;
		}
		const TCHAR* Color = Cor.bIsLink ? TEXT("#4fa3ff") : (Cor.bIsLoop ? TEXT("#37c837") : (Cor.bIsSpine ? TEXT("#ff9d2e") : TEXT("#888")));
		const float	 AvgR = Cor.Radii.Num() > 0 ? Cor.Radii[0] : Stroke;
		FString		 Pts;
		for (const FVector2D& P : Cor.Centerline)
		{
			Pts += FString::Printf(TEXT("%.0f,%.0f "), X(P.X), Y(P.Y));
		}
		Svg += FString::Printf(TEXT("<polyline points=\"%s\" fill=\"none\" stroke=\"%s\" stroke-width=\"%.0f\" stroke-opacity=\"0.5\" "
									"stroke-linecap=\"round\" stroke-linejoin=\"round\"/>\n"),
			*Pts,
			Color,
			FMath::Max(Stroke, AvgR * 2.0f));
		// Centerline overlay (thin) so the route is visible inside the width band.
		Svg += FString::Printf(TEXT("<polyline points=\"%s\" fill=\"none\" stroke=\"%s\" stroke-width=\"%.0f\"/>\n"), *Pts, Color, Stroke);
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
