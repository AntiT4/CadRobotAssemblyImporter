#pragma once

#include "CoreMinimal.h"
#include "WorkflowTypes.h"

class AActor;

namespace CadActorHierarchyUtils
{
	void GetSortedAttachedChildren(
		AActor* Actor,
		TArray<AActor*>& OutChildren,
		bool bRecursive = false);

	void CollectHierarchy(
		AActor* RootActor,
		TArray<AActor*>& OutActors);

	AActor* FindByPath(const FString& ActorPath);

	void SaveVisibilitySnapshot(
		const TArray<FCadChildEntry>& ChildEntries,
		TMap<FString, bool>& OutVisibilitySnapshot);

	void ApplyVisibilityIsolation(
		const TArray<FCadChildEntry>& ChildEntries,
		int32 VisibleChildIndex);

	void RestoreVisibilitySnapshot(const TMap<FString, bool>& VisibilitySnapshot);
}
