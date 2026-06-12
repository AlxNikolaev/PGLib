#pragma once

#include "CoreMinimal.h"

namespace PGSeed
{
	/**
	 * Case-sensitive, stable seed-string hash. GetTypeHash(FString) is a case-insensitive Strihash and
	 * collides distinct seeds that differ only in case (e.g. "Cave01" / "cave01"), so seeded generation
	 * must hash through FCrc::StrCrc32 over the raw TCHAR data instead.
	 */
	FORCEINLINE uint32 HashSeedString(const FString& Seed)
	{
		return FCrc::StrCrc32(*Seed);
	}

	/** Deterministic, platform-stable stream-key mixer for hierarchical sub-streams. */
	FORCEINLINE uint32 Mix(uint32 Base, int32 A, int32 B = 0)
	{
		uint32 H = HashCombine(Base, static_cast<uint32>(A));
		H = HashCombine(H, static_cast<uint32>(B));
		return H;
	}
} // namespace PGSeed
