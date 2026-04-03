#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/** Module-wide log category for ProceduralGeometry. */
PROCEDURALGEOMETRY_API DECLARE_LOG_CATEGORY_EXTERN(LogRoguelikeGeometry, Log, All);

class FProceduralGeometryModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
