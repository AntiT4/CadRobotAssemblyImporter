#include "Editor/ActorHierarchyUtils.h"

#include "GameFramework/Actor.h"
#include "UObject/UObjectGlobals.h"

namespace CadActorHierarchyUtils
{
	void GetSortedAttachedChildren(
		AActor* Actor,
		TArray<AActor*>& OutChildren,
		const bool bRecursive)
	{
		OutChildren.Reset();
		if (!Actor)
		{
			return;
		}

		Actor->GetAttachedActors(OutChildren, false, bRecursive);
		OutChildren.Sort([](const AActor& Left, const AActor& Right)
		{
			return Left.GetActorNameOrLabel() < Right.GetActorNameOrLabel();
		});
	}

	void CollectHierarchy(
		AActor* RootActor,
		TArray<AActor*>& OutActors)
	{
		OutActors.Reset();
		if (!RootActor)
		{
			return;
		}

		OutActors.Add(RootActor);

		TArray<AActor*> AttachedActors;
		RootActor->GetAttachedActors(AttachedActors, true, true);
		for (AActor* AttachedActor : AttachedActors)
		{
			if (AttachedActor)
			{
				OutActors.Add(AttachedActor);
			}
		}
	}

	AActor* FindByPath(const FString& ActorPath)
	{
		const FString TrimmedPath = ActorPath.TrimStartAndEnd();
		if (TrimmedPath.IsEmpty())
		{
			return nullptr;
		}

		return FindObject<AActor>(nullptr, *TrimmedPath);
	}

	void SaveVisibilitySnapshot(
		const TArray<FCadChildEntry>& ChildEntries,
		TMap<FString, bool>& OutVisibilitySnapshot)
	{
		OutVisibilitySnapshot.Reset();
		TArray<AActor*> HierarchyActors;

		for (const FCadChildEntry& ChildEntry : ChildEntries)
		{
			AActor* ChildActor = FindByPath(ChildEntry.ActorPath);
			if (!ChildActor)
			{
				continue;
			}

			CollectHierarchy(ChildActor, HierarchyActors);
			for (AActor* HierarchyActor : HierarchyActors)
			{
				if (!HierarchyActor)
				{
					continue;
				}

				const FString ActorPath = HierarchyActor->GetPathName();
				if (OutVisibilitySnapshot.Contains(ActorPath))
				{
					continue;
				}

				OutVisibilitySnapshot.Add(ActorPath, HierarchyActor->IsTemporarilyHiddenInEditor());
			}
		}
	}

	void ApplyVisibilityIsolation(
		const TArray<FCadChildEntry>& ChildEntries,
		const int32 VisibleChildIndex)
	{
		TArray<AActor*> HierarchyActors;

		for (int32 ChildIndex = 0; ChildIndex < ChildEntries.Num(); ++ChildIndex)
		{
			AActor* ChildActor = FindByPath(ChildEntries[ChildIndex].ActorPath);
			if (!ChildActor)
			{
				continue;
			}

			const bool bHideInEditor = (ChildIndex != VisibleChildIndex);
			CollectHierarchy(ChildActor, HierarchyActors);
			for (AActor* HierarchyActor : HierarchyActors)
			{
				if (HierarchyActor)
				{
					HierarchyActor->SetIsTemporarilyHiddenInEditor(bHideInEditor);
				}
			}
		}
	}

	void RestoreVisibilitySnapshot(const TMap<FString, bool>& VisibilitySnapshot)
	{
		for (const TPair<FString, bool>& SnapshotEntry : VisibilitySnapshot)
		{
			AActor* Actor = FindByPath(SnapshotEntry.Key);
			if (Actor)
			{
				Actor->SetIsTemporarilyHiddenInEditor(SnapshotEntry.Value);
			}
		}
	}
}
