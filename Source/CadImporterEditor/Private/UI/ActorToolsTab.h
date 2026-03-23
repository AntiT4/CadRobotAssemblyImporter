#pragma once

#include "CoreMinimal.h"

class SWidget;

struct FCadActorToolsTabArgs
{
	FCadActorToolsTabArgs(
		TSharedRef<FString> InInspectorText,
		TSharedRef<FString> InJsonPreviewText,
		TFunction<void()> InSaveJson)
		: InspectorText(InInspectorText)
		, JsonPreviewText(InJsonPreviewText)
		, SaveJson(MoveTemp(InSaveJson))
	{
	}

	TSharedRef<FString> InspectorText;
	TSharedRef<FString> JsonPreviewText;
	TFunction<void()> SaveJson;
};

namespace CadActorToolsTab
{
	TSharedRef<SWidget> BuildActorToolsTabContent(const FCadActorToolsTabArgs& Args);
}
