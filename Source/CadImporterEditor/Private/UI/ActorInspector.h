#pragma once

#include "CoreMinimal.h"

class SWidget;

namespace CadActorInspector
{
	bool TryBuildSelectionHierarchyPreview(FString& OutPreview, FString& OutError);
	TSharedRef<SWidget> BuildInspectorTabContent(const TSharedRef<FString>& InspectorText);
}
