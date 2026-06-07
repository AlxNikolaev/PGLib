// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

struct FOrganicDungeonGridData;

/**
 * Seed-deterministic layout diagnostics for the OrganicDungeon generator.
 *
 * Produces a machine-readable TEXT dump (rooms, corridor centerline splines, doorways, layout stats)
 * and an SVG render of the 2D layout (rooms as oriented rects, corridors as polylines colored by
 * role, start/end rooms marked). Intended for two workflows:
 *   - A designer hits a bad layout, sends the seed + config; the exact layout is regenerated and dumped.
 *   - Headless e2e testing: a test/commandlet generates from a config + seed and writes the dump so the
 *     resulting layout can be inspected (and rendered) without opening the editor.
 *
 * Pure data → string; no UObject / world access. File writing uses the Engine file helpers.
 */
struct PROCEDURALGEOMETRY_API FOrganicLayoutDebug
{
	/** Line-oriented, easy-to-parse text dump of the layout (one record per line; see .cpp for the grammar). */
	static FString ToText(const FOrganicDungeonGridData& Grid);

	/** Standalone SVG document rendering the 2D layout (rooms, corridors, start/end). */
	static FString ToSvg(const FOrganicDungeonGridData& Grid);

	/**
	 * Writes <ProjectSavedDir>/Logs/OD/<BaseName>.txt and .svg.
	 * @return absolute path of the .txt file (empty on failure).
	 */
	static FString DumpToFiles(const FOrganicDungeonGridData& Grid, const FString& BaseName);
};
