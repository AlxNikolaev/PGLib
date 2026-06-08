#pragma once

// CellDungeonDebug.h
//
// Headless debug/inspection helpers for CellDungeon results.
//
// Provides text and SVG renderings of a FCellDungeonResult and a file dumper that writes
// both to <ProjectSavedDir>/Logs/CellDungeon/. Used by the automation test and manual
// verification to inspect generated layouts without the runtime pipeline or editor.

#include "CoreMinimal.h"
#include "Generators/CellDungeon2D/CellDungeonGenerator2D.h"

/**
 * Static utilities for serializing a CellDungeon result to human-readable text and SVG.
 */
struct PROCEDURALGEOMETRY_API FCellDungeonDebug
{
	// Returns a human-readable text summary of the result (rooms, cells, corridors, validity).
	static FString ToText(const FCellDungeonResult& R);

	// Returns an SVG rendering of the cell substrate, rooms, doorways, and corridors.
	static FString ToSvg(const FCellDungeonResult& R);

	// Writes <ProjectSavedDir>/Logs/CellDungeon/<BaseName>.txt and .svg.
	// Returns the absolute path of the .txt (empty string on failure).
	static FString DumpToFiles(const FCellDungeonResult& R, const FString& BaseName);
};
