#include "MasterSelectionCollector.h"

#include "CadMasterActor.h"
#include "CadRobotActor.h"
#include "CadImporterEditor.h"
#include "Editor/ActorHierarchyUtils.h"
#include "Editor.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Selection.h"
#include "Misc/Paths.h"

namespace
{
	bool IsGroupingActor(AActor* Actor, const TArray<AActor*>& DirectChildren)
	{
		return Actor &&
			DirectChildren.Num() > 0 &&
			!CadActorHierarchyUtils::HasStaticMeshContent(Actor) &&
			!CadActorHierarchyUtils::HasMeaningfulNonSceneComponents(Actor) &&
			!Actor->IsA<AStaticMeshActor>();
	}

	ECadMasterNodeType InferNodeType(AActor* Actor, const TArray<AActor*>& DirectChildren)
	{
		if (!Actor)
		{
			return ECadMasterNodeType::Static;
		}

		if (Actor->IsA<ACadRobotActor>())
		{
			return ECadMasterNodeType::Robot;
		}

		if (Actor->IsA<ACadMasterActor>() || IsGroupingActor(Actor, DirectChildren))
		{
			return ECadMasterNodeType::Master;
		}

		return ECadMasterNodeType::Static;
	}

	void CollectHierarchyNodeRecursive(
		AActor* Actor,
		const FTransform& ParentWorldTransform,
		const FTransform& ParentAccumulatedTransform,
		FCadMasterHierarchyNode& OutNode,
		TArray<FCadChildEntry>& OutFlatChildren)
	{
		OutNode = FCadMasterHierarchyNode();
		if (!Actor)
		{
			return;
		}

		TArray<AActor*> DirectChildren;
		CadActorHierarchyUtils::GetSortedAttachedChildren(Actor, DirectChildren, false);

		OutNode.ActorName = CadActorHierarchyUtils::GetActorDisplayName(Actor);
		OutNode.ActorPath = Actor->GetPathName();
		OutNode.RelativeTransform = Actor->GetActorTransform().GetRelativeTransform(ParentWorldTransform);
		OutNode.NodeType = InferNodeType(Actor, DirectChildren);

		const FTransform AccumulatedRelativeTransform = OutNode.RelativeTransform * ParentAccumulatedTransform;
		if (CadMasterNodeTypeUsesChildJson(OutNode.NodeType))
		{
			const FString SafeActorName = FPaths::MakeValidFileName(OutNode.ActorName);
			OutNode.ChildJsonFileName = FString::Printf(TEXT("%s.json"), SafeActorName.IsEmpty() ? TEXT("Child") : *SafeActorName);

			FCadChildEntry ChildEntry;
			ChildEntry.ActorName = OutNode.ActorName;
			ChildEntry.ActorPath = OutNode.ActorPath;
			ChildEntry.RelativeTransform = AccumulatedRelativeTransform;
			ChildEntry.ActorType = CadMasterChildActorTypeFromNodeType(OutNode.NodeType);
			ChildEntry.ChildJsonFileName = OutNode.ChildJsonFileName;
			OutFlatChildren.Add(MoveTemp(ChildEntry));
			return;
		}

		for (AActor* ChildActor : DirectChildren)
		{
			if (!ChildActor)
			{
				continue;
			}

			FCadMasterHierarchyNode ChildNode;
			CollectHierarchyNodeRecursive(
				ChildActor,
				Actor->GetActorTransform(),
				AccumulatedRelativeTransform,
				ChildNode,
				OutFlatChildren);
			OutNode.Children.Add(MoveTemp(ChildNode));
		}
	}

	void AddViolation(
		FCadMasterSelection& InOutResult,
		const ECadHierarchyIssue Issue,
		const FString& Message,
		const AActor* Actor)
	{
		FCadHierarchyIssueInfo Violation;
		Violation.Issue = Issue;
		Violation.Message = Message;
		Violation.ActorName = CadActorHierarchyUtils::GetActorDisplayName(Actor);
		Violation.ActorPath = Actor ? Actor->GetPathName() : FString();
		InOutResult.Issues.Add(MoveTemp(Violation));
	}

	void ValidateDuplicateNames(const TArray<FCadChildEntry>& Children, FCadMasterSelection& InOutResult)
	{
		TMap<FString, int32> NameUseCount;
		for (const FCadChildEntry& ChildEntry : Children)
		{
			int32& UseCount = NameUseCount.FindOrAdd(ChildEntry.ActorName);
			++UseCount;
		}

		for (const FCadChildEntry& ChildEntry : Children)
		{
			const int32* UseCount = NameUseCount.Find(ChildEntry.ActorName);
			if (!UseCount || *UseCount <= 1)
			{
				continue;
			}

			FCadHierarchyIssueInfo Violation;
			Violation.Issue = ECadHierarchyIssue::DuplicateChildActorName;
			Violation.ActorName = ChildEntry.ActorName;
			Violation.ActorPath = ChildEntry.ActorPath;
			Violation.Message = FString::Printf(
				TEXT("Duplicate child actor name '%s' detected (%d). Child actor names must be unique under the same master."),
				*ChildEntry.ActorName,
				*UseCount);
			InOutResult.Issues.Add(MoveTemp(Violation));
		}
	}

	FString ToIssueLabel(const ECadHierarchyIssue Issue)
	{
		switch (Issue)
		{
		case ECadHierarchyIssue::InvalidSelection:
			return TEXT("invalid_selection");
		case ECadHierarchyIssue::MissingDirectChildren:
			return TEXT("missing_direct_children");
		case ECadHierarchyIssue::NestedChildActor:
			return TEXT("nested_child_actor");
		case ECadHierarchyIssue::DuplicateChildActorName:
			return TEXT("duplicate_child_actor_name");
		default:
			return TEXT("unknown");
		}
	}

	void AccumulateIssueCount(const ECadHierarchyIssue Issue, int32& OutInvalidSelectionCount, int32& OutMissingChildrenCount, int32& OutDuplicateNameCount)
	{
		switch (Issue)
		{
		case ECadHierarchyIssue::InvalidSelection:
			++OutInvalidSelectionCount;
			return;
		case ECadHierarchyIssue::MissingDirectChildren:
			++OutMissingChildrenCount;
			return;
		case ECadHierarchyIssue::DuplicateChildActorName:
			++OutDuplicateNameCount;
			return;
		default:
			return;
		}
	}

	FString BuildHierarchyValidationErrorText(const FCadMasterSelection& Result)
	{
		const int32 TotalIssues = Result.Issues.Num();
		int32 InvalidSelectionCount = 0;
		int32 MissingChildrenCount = 0;
		int32 DuplicateNameCount = 0;
		for (const FCadHierarchyIssueInfo& Violation : Result.Issues)
		{
			AccumulateIssueCount(Violation.Issue, InvalidSelectionCount, MissingChildrenCount, DuplicateNameCount);
		}

		const AActor* MasterActor = Result.MasterActor.Get();
		FString ErrorText = FString::Printf(
			TEXT("Master actor hierarchy validation failed with %d issue(s).\n")
			TEXT("Master candidate: %s\n")
			TEXT("Master path: %s\n")
			TEXT("Issue summary: missing_direct_children=%d, duplicate_child_actor_name=%d, invalid_selection=%d"),
			TotalIssues,
			*CadActorHierarchyUtils::GetActorDisplayName(MasterActor),
			MasterActor ? *MasterActor->GetPathName() : TEXT("(none)"),
			MissingChildrenCount,
			DuplicateNameCount,
			InvalidSelectionCount);

		constexpr int32 MaxErrorSamples = 20;
		const int32 SampleCount = FMath::Min(TotalIssues, MaxErrorSamples);
		if (SampleCount > 0)
		{
			ErrorText += FString::Printf(TEXT("\nTop %d issue samples:"), SampleCount);
			for (int32 Index = 0; Index < SampleCount; ++Index)
			{
				const FCadHierarchyIssueInfo& Violation = Result.Issues[Index];
				ErrorText += FString::Printf(
					TEXT("\n- [%s] actor='%s' path='%s' message='%s'"),
					*ToIssueLabel(Violation.Issue),
					*Violation.ActorName,
					*Violation.ActorPath,
					*Violation.Message);
			}
		}

		return ErrorText;
	}
}

namespace CadMasterSelectionCollector
{
	bool TryCollectFromSelection(FCadMasterSelection& OutResult, FString& OutError)
	{
		OutResult = FCadMasterSelection();
		OutError.Reset();

		if (!GEditor)
		{
			OutError = TEXT("Editor context is not available.");
			return false;
		}

		USelection* SelectedActors = GEditor->GetSelectedActors();
		if (!SelectedActors)
		{
			OutError = TEXT("Selected actors are not available.");
			return false;
		}

		AActor* SelectedActor = nullptr;
		int32 SelectedActorCount = 0;
		for (FSelectionIterator It(*SelectedActors); It; ++It)
		{
			if (AActor* CandidateActor = Cast<AActor>(*It))
			{
				SelectedActor = CandidateActor;
				++SelectedActorCount;
			}
		}

		if (SelectedActorCount == 0 || !SelectedActor)
		{
			OutError = TEXT("Select one actor in the level first.");
			return false;
		}

		if (SelectedActorCount > 1)
		{
			OutError = TEXT("Select exactly one actor. Multi-selection is not supported for master workflow.");
			return false;
		}

		return TryCollectFromMasterActor(SelectedActor, OutResult, OutError);
	}

	bool TryCollectFromMasterActor(AActor* MasterActor, FCadMasterSelection& OutResult, FString& OutError)
	{
		OutResult = FCadMasterSelection();
		OutError.Reset();

		if (!MasterActor)
		{
			OutError = TEXT("Master candidate actor is null.");
			return false;
		}

		OutResult.MasterActor = MasterActor;

		TArray<AActor*> Children;
		CadActorHierarchyUtils::GetSortedAttachedChildren(MasterActor, Children, false);

		for (AActor* ChildActor : Children)
		{
			if (!ChildActor)
			{
				continue;
			}

			FCadMasterHierarchyNode ChildNode;
			CollectHierarchyNodeRecursive(
				ChildActor,
				MasterActor->GetActorTransform(),
				FTransform::Identity,
				ChildNode,
				OutResult.Children);
			OutResult.HierarchyChildren.Add(MoveTemp(ChildNode));
		}

		if (Children.Num() == 0)
		{
			AddViolation(
				OutResult,
				ECadHierarchyIssue::MissingDirectChildren,
				TEXT("Selected master actor has no direct children."),
				MasterActor);
		}

		ValidateDuplicateNames(OutResult.Children, OutResult);

		if (!OutResult.IsValid())
		{
			OutError = BuildHierarchyValidationErrorText(OutResult);

			constexpr int32 MaxWarningLogs = 20;
			const int32 TotalIssues = OutResult.Issues.Num();
			const int32 WarningCount = FMath::Min(TotalIssues, MaxWarningLogs);
			UE_LOG(LogCadImporter, Warning, TEXT("Master hierarchy validation failed: total_issues=%d, sample_count=%d"), TotalIssues, WarningCount);
			for (int32 Index = 0; Index < WarningCount; ++Index)
			{
				const FCadHierarchyIssueInfo& Violation = OutResult.Issues[Index];
				UE_LOG(
					LogCadImporter,
					Warning,
					TEXT("Master hierarchy issue [%d/%d] type=%s actor='%s' path='%s' message='%s'"),
					Index + 1,
					TotalIssues,
					*ToIssueLabel(Violation.Issue),
					*Violation.ActorName,
					*Violation.ActorPath,
					*Violation.Message);
			}

			return false;
		}

		return true;
	}
}
