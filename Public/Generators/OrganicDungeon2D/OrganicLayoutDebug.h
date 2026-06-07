// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

struct FOrganicDungeonGridData;

/**
 * Seed-deterministic layout diagnostics for the gridless OrganicDungeon generator.
 *
 * Produces a machine-readable TEXT dump (rooms, junctions, few-point corridor centerlines, doorways) and an SVG
 * render of the 2D vector layout — rooms as oriented rects, junctions as deformed-circle hubs, and corridors as
 * smooth Catmull-Rom curves through their few control points. There is no rasterized grid and no per-corridor
 * role: every corridor draws as one wavy curve. Intended for two workflows:
 *   - A designer hits a bad layout, sends the seed + config; the exact layout is regenerated and dumped.
 *   - Headless e2e testing: a test/commandlet generates from a config + seed and writes the dump so the
 *     resulting layout can be inspected (and rendered) without opening the editor.
 *
 * The TEXT dump's "VALID " line carries the gridless invariants (rooms placed vs requested, connected-region
 * count, and corridor↔room-body crossings) for the e2e loop to grep. Connectivity is corridor adjacency over the
 * room-cell network (rooms + junctions), mirroring the runtime room-cell diagram — not a grid flood-fill.
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
