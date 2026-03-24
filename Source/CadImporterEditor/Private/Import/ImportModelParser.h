#pragma once

#include "CoreMinimal.h"
#include "ImportModelTypes.h"

class FJsonObject;

class FCadJsonParser
{
public:
	bool ParseFromFile(const FString& JsonPath, FCadImportModel& OutModel, FString& OutError) const;

private:
	bool ParseRoot(const TSharedPtr<FJsonObject>& RootObject, FCadImportModel& OutModel, FString& OutError) const;
};
