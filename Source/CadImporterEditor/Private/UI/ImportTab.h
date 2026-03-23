#pragma once

#include "CoreMinimal.h"
#include "ImportTypes.h"

class SWidget;
class SWindow;

struct FCadImportTabBuildArgs
{
	FCadImportTabBuildArgs(
		TFunction<bool(FString&)> InSelectJsonFile,
		TFunction<void()> InRunImport,
		TFunction<void()> InClearState,
		TSharedRef<FString> InSelectedJsonPath,
		TSharedRef<FString> InLinkPreviewText,
		TSharedRef<bool> InHasValidPreview,
		TSharedRef<FCadFbxImportOptions> InSelectedImportOptions,
		TSharedRef<FString> InUniformScaleText,
		TSharedRef<FString> InTranslationXText,
		TSharedRef<FString> InTranslationYText,
		TSharedRef<FString> InTranslationZText,
		TSharedRef<FString> InRotationPitchText,
		TSharedRef<FString> InRotationYawText,
		TSharedRef<FString> InRotationRollText)
		: SelectJsonFile(MoveTemp(InSelectJsonFile))
		, RunImport(MoveTemp(InRunImport))
		, ClearState(MoveTemp(InClearState))
		, SelectedJsonPath(InSelectedJsonPath)
		, LinkPreviewText(InLinkPreviewText)
		, bHasValidPreview(InHasValidPreview)
		, SelectedImportOptions(InSelectedImportOptions)
		, UniformScaleText(InUniformScaleText)
		, TranslationXText(InTranslationXText)
		, TranslationYText(InTranslationYText)
		, TranslationZText(InTranslationZText)
		, RotationPitchText(InRotationPitchText)
		, RotationYawText(InRotationYawText)
		, RotationRollText(InRotationRollText)
	{
	}

	TFunction<bool(FString&)> SelectJsonFile;
	TFunction<void()> RunImport;
	TFunction<void()> ClearState;
	TSharedRef<FString> SelectedJsonPath;
	TSharedRef<FString> LinkPreviewText;
	TSharedRef<bool> bHasValidPreview;
	TSharedRef<FCadFbxImportOptions> SelectedImportOptions;
	TSharedRef<FString> UniformScaleText;
	TSharedRef<FString> TranslationXText;
	TSharedRef<FString> TranslationYText;
	TSharedRef<FString> TranslationZText;
	TSharedRef<FString> RotationPitchText;
	TSharedRef<FString> RotationYawText;
	TSharedRef<FString> RotationRollText;
};

namespace CadImportTabBuilder
{
	TSharedRef<SWidget> BuildImportTabContent(const FCadImportTabBuildArgs& Args);
}
