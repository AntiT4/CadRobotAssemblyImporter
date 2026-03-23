#pragma once

#include "CoreMinimal.h"
#include "ImportTypes.h"

class UBlueprint;

class FCadImportAssemblyBuilder
{
public:
	UBlueprint* BuildRobotBlueprint(
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
