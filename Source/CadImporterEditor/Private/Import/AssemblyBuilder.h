#pragma once

#include "CoreMinimal.h"
#include "ImportModelTypes.h"

class UBlueprint;

class FCadBlueprintBuilder
{
public:
	UBlueprint* BuildBlueprint(
		const FCadImportModel& Model,
		const FCadImportPaths& Paths,
		const FCadImportResult& ImportResult,
		FString& OutError);

private:
	bool BuildComponents(
		UBlueprint* Blueprint,
		const FCadImportModel& Model,
		const FCadImportResult& ImportResult,
		FString& OutError);
};
