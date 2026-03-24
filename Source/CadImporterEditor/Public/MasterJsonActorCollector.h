#pragma once

#include "CoreMinimal.h"
#include "WorkflowTypes.h"

class AActor;

enum class ECadHierarchyIssue : uint8
{
	InvalidSelection,
	MissingDirectChildren,
	NestedChildActor,
	DuplicateChildActorName
};

struct FCadHierarchyIssueInfo
{
	ECadHierarchyIssue Issue = ECadHierarchyIssue::InvalidSelection;
	FString Message;
	FString ActorName;
	FString ActorPath;
};

struct FCadMasterSelection
{
	TWeakObjectPtr<AActor> MasterActor;
	TArray<FCadMasterChildEntry> Children;
	TArray<FCadHierarchyIssueInfo> Issues;

	bool IsValid() const
	{
		return MasterActor.IsValid() && Issues.Num() == 0;
	}
};

namespace CadMasterSelection
{
	bool TryCollectFromSelection(FCadMasterSelection& OutResult, FString& OutError);
	bool TryCollectFromMasterActor(AActor* MasterActor, FCadMasterSelection& OutResult, FString& OutError);
}
