#pragma once

#include "CoreMinimal.h"

class SWidget;

namespace CadJsonPreview
{
	bool TryBuildSelectionJsonPreview(FString& OutPreview, FString& OutError);
	bool TrySaveSelectionJson(const FString& OutputPath, FString& OutError);
	TSharedRef<SWidget> BuildJsonPreviewTabContent(const TSharedRef<FString>& PreviewText, TFunction<void()> SaveJson);
}
