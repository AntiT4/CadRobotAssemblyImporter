#pragma once

#include "CoreMinimal.h"
#include "WorkflowTypes.h"

class AActor;

struct FCadHierarchyBranchStats
{
	FString BranchName;
	FString BranchPath;
	bool bRootHasPayload = false;
	bool bCanFlattenOneLevel = false;
	int32 DirectChildCount = 0;
	int32 DescendantCount = 0;
	int32 MaxDepthFromRoot = 0;
	int32 NestedStaticMeshActorCount = 0;
};

struct FCadHierarchyFlattenResult
{
	int32 RequestedBranchCount = 0;
	int32 ProcessedBranchCount = 0;
	int32 FlattenedBranchCount = 0;
	int32 MaxDepthBefore = 0;
	int32 MovedActorCount = 0;
	int32 DeletedHelperCount = 0;
	bool bHierarchyChanged = false;
	TArray<FCadHierarchyBranchStats> BranchStats;
};

namespace CadActorHierarchyUtils
{
	FString GetActorDisplayName(const AActor* Actor);

	bool HasStaticMeshContent(AActor* Actor);

	bool HasMeaningfulNonSceneComponents(AActor* Actor);

	void GetSortedAttachedChildren(
		AActor* Actor,
		TArray<AActor*>& OutChildren,
		bool bRecursive = false);

	void CollectHierarchy(
		AActor* RootActor,
		TArray<AActor*>& OutActors);

	AActor* FindByPath(const FString& ActorPath);

	bool CanActorFlattenOneLevel(
		AActor* Actor,
		int32* OutDirectChildCount = nullptr);

	void AnalyzeDirectChildBranches(
		AActor* MasterActor,
		TArray<FCadHierarchyBranchStats>& OutBranchStats);

	bool TryFlattenSelectedDirectChildBranchesOneLevel(
		AActor* MasterActor,
		const TArray<FString>& BranchActorPaths,
		FCadHierarchyFlattenResult& OutResult,
		FString& OutError);

	void SaveVisibilitySnapshot(
		const TArray<FCadChildEntry>& ChildEntries,
		TMap<FString, bool>& OutVisibilitySnapshot);

	void ApplyVisibilityIsolation(
		const TArray<FCadChildEntry>& ChildEntries,
		int32 VisibleChildIndex);

	void RestoreVisibilitySnapshot(const TMap<FString, bool>& VisibilitySnapshot);
}
