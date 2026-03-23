#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FCadImporterRunner;

class SCadImporterPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCadImporterPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	void HandleRunImport();
	void HandleClearImportState();
	void HandleSaveSelectionJson();

	TSharedPtr<FCadImporterRunner> Runner;

	TSharedPtr<FString> SelectedJsonPath;
	TSharedPtr<FString> LinkPreviewText;
	TSharedPtr<FString> InspectorText;
	TSharedPtr<FString> JsonPreviewText;
	TSharedPtr<bool> bHasValidPreview;
	TSharedPtr<int32> ActiveTabIndex;
	TSharedPtr<struct FCadFbxImportOptions> SelectedImportOptions;

	TSharedPtr<FString> UniformScaleText;
	TSharedPtr<FString> TranslationXText;
	TSharedPtr<FString> TranslationYText;
	TSharedPtr<FString> TranslationZText;
	TSharedPtr<FString> RotationPitchText;
	TSharedPtr<FString> RotationYawText;
	TSharedPtr<FString> RotationRollText;
};
