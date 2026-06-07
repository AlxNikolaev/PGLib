// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

struct FOrganicDungeonConfig;

/**
 * Plain-text (JSON) export/import for an OrganicDungeon config (FOrganicDungeonConfig).
 *
 * Uses reflection (FJsonObjectConverter) so the whole authored config round-trips: room types with
 * weight/min/max, footprint overrides + baked basement-marker footprints, baked doorways, and all
 * corridor/topology/realization params. TSoftObjectPtr<UWorld> room levels serialize as their asset
 * path string (we cannot export uassets — the footprint sizes + doorways carried in the config are what
 * a headless generation run needs).
 *
 * Enables an e2e loop: a designer exports a Location's config to a file and shares it; a headless test
 * imports it (+ a seed) and generates the exact layout for inspection.
 */
struct PROCEDURALGEOMETRY_API FOrganicConfigIO
{
	/** Serializes a config to a pretty-printed JSON string. */
	static FString ToJson(const FOrganicDungeonConfig& Config);

	/** Parses a JSON string into a config. Returns false on malformed input (OutConfig left untouched). */
	static bool FromJson(const FString& Json, FOrganicDungeonConfig& OutConfig);

	/** ToJson + write to AbsPath. Returns false on serialize/IO failure. */
	static bool SaveToFile(const FOrganicDungeonConfig& Config, const FString& AbsPath);

	/** Read AbsPath + FromJson. Returns false if the file is missing or malformed. */
	static bool LoadFromFile(const FString& AbsPath, FOrganicDungeonConfig& OutConfig);
};
