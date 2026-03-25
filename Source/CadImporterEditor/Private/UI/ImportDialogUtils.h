#pragma once

#include "CoreMinimal.h"
#include "ImportModelTypes.h"

namespace CadImportDialogUtils
{
	FString JointTypeToString(ECadImportJointType Type);
	FString FormatVector(const FVector& Value);
	FString FormatRotator(const FRotator& Value);
	FString FormatColor(const FLinearColor& Value);

	void SyncImportedAssetsInContentBrowser(const FCadImportResult& ImportResult);
	void LogModel(const FCadImportModel& Model, const FString& JsonPath);
}
