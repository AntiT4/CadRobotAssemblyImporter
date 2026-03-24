#pragma once

#include "CoreMinimal.h"
#include "ImportModelTypes.h"

class FCadImportJsonWriter
{
public:
	bool WriteToString(const FCadImportModel& Model, FString& OutJson, FString& OutError) const;
	bool WriteToFile(const FCadImportModel& Model, const FString& OutputPath, FString& OutError) const;
};
