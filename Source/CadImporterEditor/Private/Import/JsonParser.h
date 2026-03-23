#pragma once

#include "CoreMinimal.h"
#include "ImportTypes.h"

class FJsonObject;

class FCadImportJsonParser
{
public:
	bool ParseFromFile(const FString& JsonPath, FCadImportModel& OutModel, FString& OutError) const;

private:
	bool ParseRoot(const TSharedPtr<FJsonObject>& RootObject, FCadImportModel& OutModel, FString& OutError) const;
};
