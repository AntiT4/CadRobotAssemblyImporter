#pragma once

#include "CoreMinimal.h"
#include "ImportModelTypes.h"
#include "ImportOptions.h"

namespace CadImportDialogUtils
{
	FString JointTypeToString(ECadImportJointType Type);
	FString FormatVector(const FVector& Value);
	FString FormatRotator(const FRotator& Value);
	FString FormatColor(const FLinearColor& Value);

	void FillImportOptionTextFields(
		const FCadFbxImportOptions& Options,
		FString& OutScaleText,
		FString& OutTxText,
		FString& OutTyText,
		FString& OutTzText,
		FString& OutPitchText,
		FString& OutYawText,
		FString& OutRollText);

	bool TryParseImportOptionTextFields(
		const FString& InScaleText,
		const FString& InTxText,
		const FString& InTyText,
		const FString& InTzText,
		const FString& InPitchText,
		const FString& InYawText,
		const FString& InRollText,
		FCadFbxImportOptions& InOutOptions,
		FString& OutError);

	bool TryBuildPreviewFromJson(
		const FString& JsonPath,
		FString& OutPreview,
		FString& OutError);

	void SyncImportedAssetsInContentBrowser(const FCadImportResult& ImportResult);
	void LogModel(const FCadImportModel& Model, const FString& JsonPath);
}
