#pragma once

#include "ImportModelTypes.h"
#include "ImportOptions.h"

class UBlueprint;

namespace CadImportExecutor
{
	bool TryImportModel(
		const FCadImportModel& Model,
		const FString& SourceLabel,
		const FCadFbxImportOptions& ImportOptions,
		UBlueprint** OutBuiltBlueprint,
		FString& OutError);
}
