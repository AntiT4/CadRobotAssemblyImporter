#pragma once

#include "ImportModelTypes.h"

class UBlueprint;

namespace CadImportExecutor
{
	bool TryImportModel(
		const FCadImportModel& Model,
		const FString& SourceLabel,
		UBlueprint** OutBuiltBlueprint,
		FString& OutError);
}
