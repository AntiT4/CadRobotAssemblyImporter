#pragma once

#include "CoreMinimal.h"
#include "WorkflowTypes.h"

class AActor;

namespace CadChildVisualCollector
{
	void CollectStaticChildVisuals(
		AActor* ChildRootActor,
		TArray<FCadChildVisual>& OutVisuals);

	void CollectRootLinkVisuals(
		AActor* ChildRootActor,
		TArray<FCadChildVisual>& OutVisuals);
}
