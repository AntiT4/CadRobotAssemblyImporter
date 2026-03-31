#include "Editor/ActorHierarchyUtils.h"

#include "CadImporterEditor.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "ScopedTransaction.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	FString GetHierarchyUtilsActorDisplayName(const AActor* Actor)
	{
		return Actor ? Actor->GetActorNameOrLabel() : TEXT("(none)");
	}

	bool HasStaticMeshContent(AActor* Actor)
	{
		if (!Actor)
		{
			return false;
		}

		TInlineComponentArray<UStaticMeshComponent*> MeshComponents(Actor);
		for (const UStaticMeshComponent* MeshComponent : MeshComponents)
		{
			if (MeshComponent && MeshComponent->GetStaticMesh())
			{
				return true;
			}
		}

		return false;
	}

	bool HasMeaningfulNonSceneComponents(AActor* Actor)
	{
		if (!Actor)
		{
			return false;
		}

		TInlineComponentArray<UActorComponent*> ActorComponents(Actor);
		for (const UActorComponent* ActorComponent : ActorComponents)
		{
			if (!ActorComponent || ActorComponent->IsA<USceneComponent>())
			{
				continue;
			}

			return true;
		}

		return false;
	}

	bool IsEmptyHelperActor(AActor* Actor)
	{
		if (!Actor)
		{
			return false;
		}

		return !HasStaticMeshContent(Actor) && !HasMeaningfulNonSceneComponents(Actor);
	}

	int32 ComputeDepthRelativeToRoot(AActor* Actor, AActor* RootActor)
	{
		int32 Depth = 0;
		for (AActor* Current = Actor; Current && Current != RootActor; Current = Current->GetAttachParentActor())
		{
			++Depth;
		}

		return Depth;
	}

	bool IsActorUnderRoot(AActor* Actor, AActor* RootActor)
	{
		if (!Actor || !RootActor)
		{
			return false;
		}

		if (Actor == RootActor)
		{
			return true;
		}

		for (AActor* Current = Actor->GetAttachParentActor(); Current; Current = Current->GetAttachParentActor())
		{
			if (Current == RootActor)
			{
				return true;
			}
		}

		return false;
	}

	int32 ComputeMaxDepthFromHierarchyActor(AActor* RootActor)
	{
		if (!RootActor)
		{
			return 0;
		}

		TArray<AActor*> DirectChildren;
		CadActorHierarchyUtils::GetSortedAttachedChildren(RootActor, DirectChildren, false);
		int32 MaxDepth = 0;
		for (AActor* ChildActor : DirectChildren)
		{
			MaxDepth = FMath::Max(MaxDepth, 1 + ComputeMaxDepthFromHierarchyActor(ChildActor));
		}

		return MaxDepth;
	}

	void CollectBranchStatsRecursive(
		AActor* CurrentActor,
		const int32 CurrentDepthFromRoot,
		FCadHierarchyBranchStats& InOutStats)
	{
		TArray<AActor*> Children;
		CadActorHierarchyUtils::GetSortedAttachedChildren(CurrentActor, Children, false);
		if (CurrentDepthFromRoot == 0)
		{
			InOutStats.DirectChildCount = Children.Num();
		}

		for (AActor* ChildActor : Children)
		{
			if (!ChildActor)
			{
				continue;
			}

			const int32 ChildDepthFromRoot = CurrentDepthFromRoot + 1;
			++InOutStats.DescendantCount;
			InOutStats.MaxDepthFromRoot = FMath::Max(InOutStats.MaxDepthFromRoot, ChildDepthFromRoot);

			if (ChildDepthFromRoot > 1 && ChildActor->IsA<AStaticMeshActor>())
			{
				++InOutStats.NestedStaticMeshActorCount;
			}

			CollectBranchStatsRecursive(ChildActor, ChildDepthFromRoot, InOutStats);
		}
	}

}

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

	bool CanActorFlattenOneLevel(
		AActor* Actor,
		int32* OutDirectChildCount)
	{
		if (OutDirectChildCount)
		{
			*OutDirectChildCount = 0;
		}

		if (!Actor)
		{
			return false;
		}

		TArray<AActor*> DirectChildren;
		GetSortedAttachedChildren(Actor, DirectChildren, false);
		if (OutDirectChildCount)
		{
			*OutDirectChildCount = DirectChildren.Num();
		}

		return IsEmptyHelperActor(Actor) && DirectChildren.Num() > 0 && ComputeMaxDepthFromHierarchyActor(Actor) > 1;
	}

	void AnalyzeDirectChildBranches(
		AActor* MasterActor,
		TArray<FCadHierarchyBranchStats>& OutBranchStats)
	{
		OutBranchStats.Reset();
		if (!MasterActor)
		{
			return;
		}

		TArray<AActor*> DirectChildren;
		GetSortedAttachedChildren(MasterActor, DirectChildren, false);
		for (AActor* ChildActor : DirectChildren)
		{
			if (!ChildActor)
			{
				continue;
			}

			FCadHierarchyBranchStats BranchStats;
			BranchStats.BranchName = GetHierarchyUtilsActorDisplayName(ChildActor);
			BranchStats.BranchPath = ChildActor->GetPathName();
			BranchStats.bRootHasPayload = !IsEmptyHelperActor(ChildActor);
			CollectBranchStatsRecursive(ChildActor, 0, BranchStats);
			BranchStats.bCanFlattenOneLevel = !BranchStats.bRootHasPayload && BranchStats.DirectChildCount > 0 && BranchStats.MaxDepthFromRoot > 1;
			OutBranchStats.Add(MoveTemp(BranchStats));
		}
	}

	bool TryFlattenSelectedDirectChildBranchesOneLevel(
		AActor* MasterActor,
		const TArray<FString>& BranchActorPaths,
		FCadHierarchyFlattenResult& OutResult,
		FString& OutError)
	{
		OutResult = FCadHierarchyFlattenResult();
		OutError.Reset();

		if (!MasterActor)
		{
			OutError = TEXT("Master actor is null.");
			return false;
		}

		UWorld* World = MasterActor->GetWorld();
		if (!World)
		{
			OutError = TEXT("Master actor world is unavailable.");
			return false;
		}

		AnalyzeDirectChildBranches(MasterActor, OutResult.BranchStats);
		OutResult.RequestedBranchCount = BranchActorPaths.Num();
		OutResult.ProcessedBranchCount = OutResult.BranchStats.Num();
		for (const FCadHierarchyBranchStats& BranchStats : OutResult.BranchStats)
		{
			OutResult.MaxDepthBefore = FMath::Max(OutResult.MaxDepthBefore, BranchStats.MaxDepthFromRoot);
		}

		TSet<FString> RequestedPaths;
		for (const FString& BranchPath : BranchActorPaths)
		{
			const FString TrimmedPath = BranchPath.TrimStartAndEnd();
			if (!TrimmedPath.IsEmpty())
			{
				RequestedPaths.Add(TrimmedPath);
			}
		}

		if (RequestedPaths.Num() == 0)
		{
			return true;
		}

		FScopedTransaction Transaction(NSLOCTEXT("CadImporter", "FlattenSelectedMasterChildBranches", "Flatten selected CAD master child branches"));
		MasterActor->Modify();

		TArray<AActor*> BranchRoots;
		for (const FString& RequestedPath : RequestedPaths)
		{
			AActor* BranchRoot = FindByPath(RequestedPath);
			if (!BranchRoot)
			{
				Transaction.Cancel();
				OutError = FString::Printf(TEXT("Selected actor was not found: %s"), *RequestedPath);
				return false;
			}

			if (!IsActorUnderRoot(BranchRoot, MasterActor))
			{
				Transaction.Cancel();
				OutError = FString::Printf(TEXT("Selected actor is not attached under the master: %s"), *RequestedPath);
				return false;
			}

			int32 DirectChildCount = 0;
			if (!CanActorFlattenOneLevel(BranchRoot, &DirectChildCount))
			{
				Transaction.Cancel();
				OutError = FString::Printf(TEXT("Selected actor cannot be unpacked safely in one step: %s"), *BranchRoot->GetPathName());
				return false;
			}

			BranchRoots.Add(BranchRoot);
		}

		BranchRoots.Sort([MasterActor](const AActor& Left, const AActor& Right)
		{
			return ComputeDepthRelativeToRoot(const_cast<AActor*>(&Left), MasterActor) < ComputeDepthRelativeToRoot(const_cast<AActor*>(&Right), MasterActor);
		});

		for (AActor* BranchRoot : BranchRoots)
		{
			TArray<AActor*> PromotedActors;
			GetSortedAttachedChildren(BranchRoot, PromotedActors, false);
			if (PromotedActors.Num() == 0)
			{
				continue;
			}

			BranchRoot->Modify();
			int32 MovedCountForBranch = 0;
			for (AActor* PromotedActor : PromotedActors)
			{
				if (!PromotedActor || PromotedActor->GetAttachParentActor() != BranchRoot)
				{
					continue;
				}

				if (AActor* PreviousParent = PromotedActor->GetAttachParentActor())
				{
					PreviousParent->Modify();
				}

				PromotedActor->Modify();
				if (PromotedActor->AttachToActor(MasterActor, FAttachmentTransformRules::KeepWorldTransform))
				{
					++MovedCountForBranch;
				}
				else
				{
					UE_LOG(
						LogCadImporter,
						Warning,
						TEXT("Failed to promote grandchild actor during branch flatten. branch='%s' actor='%s'"),
						*BranchRoot->GetPathName(),
						*PromotedActor->GetPathName());
				}
			}

			int32 DeletedHelpersForBranch = 0;
			TArray<AActor*> RemainingChildren;
			GetSortedAttachedChildren(BranchRoot, RemainingChildren, false);
			if (RemainingChildren.Num() == 0)
			{
				if (!IsEmptyHelperActor(BranchRoot))
				{
					Transaction.Cancel();
					OutError = FString::Printf(
						TEXT("Selected branch root still has payload and cannot be deleted after flatten: %s"),
						*BranchRoot->GetPathName());
					return false;
				}

				if (World->EditorDestroyActor(BranchRoot, true))
				{
					DeletedHelpersForBranch = 1;
				}
				else
				{
					Transaction.Cancel();
					OutError = FString::Printf(TEXT("Failed to delete flattened branch root: %s"), *BranchRoot->GetPathName());
					return false;
				}
			}

			if (MovedCountForBranch > 0)
			{
				++OutResult.FlattenedBranchCount;
			}

			OutResult.MovedActorCount += MovedCountForBranch;
			OutResult.DeletedHelperCount += DeletedHelpersForBranch;
			if (MovedCountForBranch > 0 || DeletedHelpersForBranch > 0)
			{
				OutResult.bHierarchyChanged = true;
			}
		}

		return true;
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
