#include "MasterJsonActorCollector.h"

#include "CadImporterEditor.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Misc/Paths.h"

namespace
{
	FString GetMasterWorkflowActorDisplayName(const AActor* Actor)
	{
		return Actor ? Actor->GetActorNameOrLabel() : TEXT("(none)");
	}

	void GetSortedDirectChildren(AActor* ParentActor, TArray<AActor*>& OutChildren)
	{
		OutChildren.Reset();
		if (!ParentActor)
		{
			return;
		}

		ParentActor->GetAttachedActors(OutChildren, false, false);
		OutChildren.Sort([](const AActor& Left, const AActor& Right)
		{
			return Left.GetActorNameOrLabel() < Right.GetActorNameOrLabel();
		});
	}

	void AddViolation(
		FCadMasterActorSelectionResult& InOutResult,
		const ECadMasterHierarchyIssue Issue,
		const FString& Message,
		const AActor* Actor)
	{
		FCadMasterHierarchyViolation Violation;
		Violation.Issue = Issue;
		Violation.Message = Message;
		Violation.ActorName = GetMasterWorkflowActorDisplayName(Actor);
		Violation.ActorPath = Actor ? Actor->GetPathName() : FString();
		InOutResult.Violations.Add(MoveTemp(Violation));
	}

	void ValidateDuplicateNames(const TArray<FCadMasterChildEntry>& DirectChildren, FCadMasterActorSelectionResult& InOutResult)
	{
		TMap<FString, int32> NameUseCount;
		for (const FCadMasterChildEntry& ChildEntry : DirectChildren)
		{
			int32& UseCount = NameUseCount.FindOrAdd(ChildEntry.ActorName);
			++UseCount;
		}

		for (const FCadMasterChildEntry& ChildEntry : DirectChildren)
		{
			const int32* UseCount = NameUseCount.Find(ChildEntry.ActorName);
			if (!UseCount || *UseCount <= 1)
			{
				continue;
			}

			FCadMasterHierarchyViolation Violation;
			Violation.Issue = ECadMasterHierarchyIssue::DuplicateChildActorName;
			Violation.ActorName = ChildEntry.ActorName;
			Violation.ActorPath = ChildEntry.ActorPath;
			Violation.Message = FString::Printf(
				TEXT("Duplicate child actor name '%s' detected (%d). Child actor names must be unique under the same master."),
				*ChildEntry.ActorName,
				*UseCount);
			InOutResult.Violations.Add(MoveTemp(Violation));
		}
	}

	FString ToIssueLabel(const ECadMasterHierarchyIssue Issue)
	{
		switch (Issue)
		{
		case ECadMasterHierarchyIssue::InvalidSelection:
			return TEXT("invalid_selection");
		case ECadMasterHierarchyIssue::MissingDirectChildren:
			return TEXT("missing_direct_children");
		case ECadMasterHierarchyIssue::NestedChildActor:
			return TEXT("nested_child_actor");
		case ECadMasterHierarchyIssue::DuplicateChildActorName:
			return TEXT("duplicate_child_actor_name");
		default:
			return TEXT("unknown");
		}
	}

	void AccumulateIssueCount(const ECadMasterHierarchyIssue Issue, int32& OutInvalidSelectionCount, int32& OutMissingDirectChildrenCount, int32& OutDuplicateNameCount)
	{
		switch (Issue)
		{
		case ECadMasterHierarchyIssue::InvalidSelection:
			++OutInvalidSelectionCount;
			return;
		case ECadMasterHierarchyIssue::MissingDirectChildren:
			++OutMissingDirectChildrenCount;
			return;
		case ECadMasterHierarchyIssue::DuplicateChildActorName:
			++OutDuplicateNameCount;
			return;
		default:
			return;
		}
	}

	FString BuildHierarchyValidationErrorText(const FCadMasterActorSelectionResult& Result)
	{
		const int32 TotalIssues = Result.Violations.Num();
		int32 InvalidSelectionCount = 0;
		int32 MissingDirectChildrenCount = 0;
		int32 DuplicateNameCount = 0;
		for (const FCadMasterHierarchyViolation& Violation : Result.Violations)
		{
			AccumulateIssueCount(Violation.Issue, InvalidSelectionCount, MissingDirectChildrenCount, DuplicateNameCount);
		}

		const AActor* MasterActor = Result.MasterCandidateActor.Get();
		FString ErrorText = FString::Printf(
			TEXT("Master actor hierarchy validation failed with %d issue(s).\n")
			TEXT("Master candidate: %s\n")
			TEXT("Master path: %s\n")
			TEXT("Issue summary: missing_direct_children=%d, duplicate_child_actor_name=%d, invalid_selection=%d"),
			TotalIssues,
			*GetMasterWorkflowActorDisplayName(MasterActor),
			MasterActor ? *MasterActor->GetPathName() : TEXT("(none)"),
			MissingDirectChildrenCount,
			DuplicateNameCount,
			InvalidSelectionCount);

		constexpr int32 MaxErrorSamples = 20;
		const int32 SampleCount = FMath::Min(TotalIssues, MaxErrorSamples);
		if (SampleCount > 0)
		{
			ErrorText += FString::Printf(TEXT("\nTop %d issue samples:"), SampleCount);
			for (int32 Index = 0; Index < SampleCount; ++Index)
			{
				const FCadMasterHierarchyViolation& Violation = Result.Violations[Index];
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

namespace CadMasterJsonActorCollector
{
	bool TryCollectFromSelection(FCadMasterActorSelectionResult& OutResult, FString& OutError)
	{
		OutResult = FCadMasterActorSelectionResult();
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

	bool TryCollectFromMasterActor(AActor* MasterActor, FCadMasterActorSelectionResult& OutResult, FString& OutError)
	{
		OutResult = FCadMasterActorSelectionResult();
		OutError.Reset();

		if (!MasterActor)
		{
			OutError = TEXT("Master candidate actor is null.");
			return false;
		}

		OutResult.MasterCandidateActor = MasterActor;

		TArray<AActor*> DirectChildren;
		GetSortedDirectChildren(MasterActor, DirectChildren);

		for (AActor* ChildActor : DirectChildren)
		{
			if (!ChildActor)
			{
				continue;
			}

			FCadMasterChildEntry ChildEntry;
			ChildEntry.ActorName = GetMasterWorkflowActorDisplayName(ChildActor);
			ChildEntry.ActorPath = ChildActor->GetPathName();
			ChildEntry.RelativeTransform = ChildActor->GetActorTransform().GetRelativeTransform(MasterActor->GetActorTransform());
			ChildEntry.ChildJsonFileName = FString::Printf(TEXT("%s.json"), *FPaths::MakeValidFileName(ChildEntry.ActorName));
			OutResult.DirectChildren.Add(MoveTemp(ChildEntry));
		}

		if (OutResult.DirectChildren.Num() == 0)
		{
			AddViolation(
				OutResult,
				ECadMasterHierarchyIssue::MissingDirectChildren,
				TEXT("Selected master actor has no direct children."),
				MasterActor);
		}

		ValidateDuplicateNames(OutResult.DirectChildren, OutResult);

		if (!OutResult.IsValid())
		{
			OutError = BuildHierarchyValidationErrorText(OutResult);

			constexpr int32 MaxWarningLogs = 20;
			const int32 TotalIssues = OutResult.Violations.Num();
			const int32 WarningCount = FMath::Min(TotalIssues, MaxWarningLogs);
			UE_LOG(LogCadImporter, Warning, TEXT("Master hierarchy validation failed: total_issues=%d, sample_count=%d"), TotalIssues, WarningCount);
			for (int32 Index = 0; Index < WarningCount; ++Index)
			{
				const FCadMasterHierarchyViolation& Violation = OutResult.Violations[Index];
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
