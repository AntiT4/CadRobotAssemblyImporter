#pragma once

#include "CoreMinimal.h"
#include "ImportTypes.h"

class AActor;

enum class ECadMasterHierarchyIssue : uint8
{
	InvalidSelection,
	MissingDirectChildren,
	NestedChildActor,
	DuplicateChildActorName
};

struct FCadMasterHierarchyViolation
{
	ECadMasterHierarchyIssue Issue = ECadMasterHierarchyIssue::InvalidSelection;
	FString Message;
	FString ActorName;
	FString ActorPath;
};

struct FCadMasterActorSelectionResult
{
	TWeakObjectPtr<AActor> MasterCandidateActor;
	TArray<FCadMasterChildEntry> DirectChildren;
	TArray<FCadMasterHierarchyViolation> Violations;

	bool IsValid() const
	{
		return MasterCandidateActor.IsValid() && Violations.Num() == 0;
	}
};

namespace CadMasterJsonActorCollector
{
	bool TryCollectFromSelection(FCadMasterActorSelectionResult& OutResult, FString& OutError);
	bool TryCollectFromMasterActor(AActor* MasterActor, FCadMasterActorSelectionResult& OutResult, FString& OutError);
}
