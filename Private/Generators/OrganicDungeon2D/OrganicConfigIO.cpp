// Fill out your copyright notice in the Description page of Project Settings.

#include "Generators/OrganicDungeon2D/OrganicConfigIO.h"

#include "Generators/OrganicDungeon2D/OrganicDungeonConfig.h"

#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"

FString FOrganicConfigIO::ToJson(const FOrganicDungeonConfig& Config)
{
	FString Out;
	FJsonObjectConverter::UStructToJsonObjectString(FOrganicDungeonConfig::StaticStruct(), &Config, Out, /*CheckFlags=*/0, /*SkipFlags=*/0);
	return Out;
}

bool FOrganicConfigIO::FromJson(const FString& Json, FOrganicDungeonConfig& OutConfig)
{
	FOrganicDungeonConfig Parsed;
	if (!FJsonObjectConverter::JsonObjectStringToUStruct(Json, &Parsed, /*CheckFlags=*/0, /*SkipFlags=*/0))
	{
		return false;
	}
	OutConfig = MoveTemp(Parsed);
	return true;
}

bool FOrganicConfigIO::SaveToFile(const FOrganicDungeonConfig& Config, const FString& AbsPath)
{
	const FString Json = ToJson(Config);
	return !Json.IsEmpty() && FFileHelper::SaveStringToFile(Json, *AbsPath);
}

bool FOrganicConfigIO::LoadFromFile(const FString& AbsPath, FOrganicDungeonConfig& OutConfig)
{
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *AbsPath))
	{
		return false;
	}
	return FromJson(Json, OutConfig);
}
