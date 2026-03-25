#include "LevelReplacer.h"

#include "CadImporterEditor.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/Level.h"
#include "EngineUtils.h"
#include "ScopedTransaction.h"

namespace
{
	FString ExtractActorNameFromPath(const FString& ActorPath)
	{
		if (ActorPath.IsEmpty())
		{
			return FString();
		}

		int32 SeparatorIndex = INDEX_NONE;
		if (!ActorPath.FindLastChar(TEXT('.'), SeparatorIndex))
		{
			ActorPath.FindLastChar(TEXT(':'), SeparatorIndex);
		}

		return (SeparatorIndex != INDEX_NONE && SeparatorIndex + 1 < ActorPath.Len())
			? ActorPath.Mid(SeparatorIndex + 1)
			: ActorPath;
	}

	AActor* FindMasterActorInEditorWorld(const FString& MasterActorPath)
	{
		if (!GEditor)
		{
			return nullptr;
		}

		if (AActor* FoundByPath = FindObject<AActor>(nullptr, *MasterActorPath))
		{
			return FoundByPath;
		}

		UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
		if (!EditorWorld)
		{
			return nullptr;
		}

		const FString TargetActorName = ExtractActorNameFromPath(MasterActorPath);
		for (TActorIterator<AActor> It(EditorWorld); It; ++It)
		{
			AActor* Candidate = *It;
			if (!Candidate)
			{
				continue;
			}

			if (Candidate->GetPathName() == MasterActorPath)
			{
				return Candidate;
			}

			if (!TargetActorName.IsEmpty() && Candidate->GetName() == TargetActorName)
			{
				return Candidate;
			}
		}

		return nullptr;
	}

	void CollectMasterHierarchyActors(AActor* RootActor, TArray<AActor*>& OutActors)
	{
		OutActors.Reset();
		if (!RootActor)
		{
			return;
		}

		OutActors.Add(RootActor);

		TArray<AActor*> Descendants;
		RootActor->GetAttachedActors(Descendants, true, true);
		for (AActor* Descendant : Descendants)
		{
			if (Descendant)
			{
				OutActors.AddUnique(Descendant);
			}
		}

		OutActors.Sort([RootActor](const AActor& Left, const AActor& Right)
		{
			auto ComputeDepth = [RootActor](const AActor& Actor) -> int32
			{
				int32 Depth = 0;
				const AActor* Current = &Actor;
				while (Current && Current != RootActor)
				{
					Current = Current->GetAttachParentActor();
					++Depth;
				}
				return Depth;
			};

			return ComputeDepth(Left) > ComputeDepth(Right);
		});
	}
}

namespace CadLevelReplacer
{
	bool TryReplaceMasterHierarchyWithBlueprints(
		const FCadMasterDoc& MasterDocument,
		UBlueprint* MasterBlueprint,
		const TMap<FString, UBlueprint*>& ChildBlueprintsByChildName,
		FCadLevelReplaceResult& OutResult,
		FString& OutError)
	{
		OutResult = FCadLevelReplaceResult();
		OutError.Reset();

		if (!GEditor)
		{
			OutError = TEXT("Editor context is not available.");
			return false;
		}

		UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
		if (!EditorWorld)
		{
			OutError = TEXT("Editor world is not available.");
			return false;
		}

		if (!MasterBlueprint || !MasterBlueprint->GeneratedClass)
		{
			OutError = TEXT("Master blueprint class is invalid.");
			return false;
		}

		AActor* MasterActor = FindMasterActorInEditorWorld(MasterDocument.MasterActorPath);
		if (!MasterActor)
		{
			OutError = FString::Printf(TEXT("Master actor not found in level: %s"), *MasterDocument.MasterActorPath);
			return false;
		}

		TArray<AActor*> ActorsToDelete;
		CollectMasterHierarchyActors(MasterActor, ActorsToDelete);
		if (ActorsToDelete.Num() == 0)
		{
			OutError = TEXT("No level actors were found for replacement.");
			return false;
		}

		for (const FCadChildEntry& ChildEntry : MasterDocument.Children)
		{
			const FString ChildName = ChildEntry.ActorName.TrimStartAndEnd();
			if (ChildName.IsEmpty())
			{
				OutError = TEXT("Master json contains a child with empty actor_name.");
				return false;
			}

			UBlueprint* const* ChildBlueprintPtr = ChildBlueprintsByChildName.Find(ChildName);
			if (!ChildBlueprintPtr || !(*ChildBlueprintPtr) || !(*ChildBlueprintPtr)->GeneratedClass)
			{
				OutError = FString::Printf(TEXT("Child blueprint was not built or is invalid for child '%s'."), *ChildName);
				return false;
			}
		}

		FScopedTransaction Transaction(
			TEXT("CadImporterEditor"),
			FText::FromString(TEXT("CAD Master Workflow Replace Level Actors")),
			MasterActor,
			GEditor->CanTransact());
		if (ULevel* CurrentLevel = EditorWorld->GetCurrentLevel())
		{
			CurrentLevel->Modify();
		}
		MasterActor->Modify();
		for (AActor* ActorToDelete : ActorsToDelete)
		{
			if (ActorToDelete)
			{
				ActorToDelete->Modify();
			}
		}

		const FTransform SpawnTransform = MasterActor->GetActorTransform();
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.Name = NAME_None;
		SpawnParams.ObjectFlags |= RF_Transactional;

		AActor* SpawnedMasterActor = EditorWorld->SpawnActor<AActor>(MasterBlueprint->GeneratedClass, SpawnTransform, SpawnParams);
		if (!SpawnedMasterActor)
		{
			OutError = TEXT("Failed to spawn master blueprint actor in the level.");
			Transaction.Cancel();
			return false;
		}
		SpawnedMasterActor->SetFlags(RF_Transactional);
		SpawnedMasterActor->Modify();

		const FString SpawnLabelBase = MasterDocument.MasterName.IsEmpty()
			? TEXT("CadRobot")
			: FString::Printf(TEXT("%s_BP"), *MasterDocument.MasterName);
		SpawnedMasterActor->SetActorLabel(SpawnLabelBase, true);

		int32 SpawnedChildActorCount = 0;
		for (const FCadChildEntry& ChildEntry : MasterDocument.Children)
		{
			const FString ChildName = ChildEntry.ActorName.TrimStartAndEnd();
			UBlueprint* const* ChildBlueprintPtr = ChildBlueprintsByChildName.Find(ChildName);
			const FTransform ChildWorldTransform = ChildEntry.RelativeTransform * SpawnTransform;
			AActor* SpawnedChildActor = EditorWorld->SpawnActor<AActor>((*ChildBlueprintPtr)->GeneratedClass, ChildWorldTransform, SpawnParams);
			if (!SpawnedChildActor)
			{
				OutError = FString::Printf(TEXT("Failed to spawn child blueprint actor for '%s'."), *ChildName);
				Transaction.Cancel();
				return false;
			}

			SpawnedChildActor->SetFlags(RF_Transactional);
			SpawnedChildActor->Modify();
			SpawnedChildActor->AttachToActor(SpawnedMasterActor, FAttachmentTransformRules::KeepWorldTransform);
			SpawnedChildActor->SetActorRelativeTransform(ChildEntry.RelativeTransform);
			SpawnedChildActor->SetActorLabel(FString::Printf(TEXT("BP_%s"), *ChildName), true);
			UE_LOG(
				LogCadImporter,
				Display,
				TEXT("Spawned child blueprint actor. child=%s actor=%s relative_location=%s relative_rotation=%s relative_scale=%s"),
				*ChildName,
				*SpawnedChildActor->GetPathName(),
				*ChildEntry.RelativeTransform.GetLocation().ToString(),
				*ChildEntry.RelativeTransform.Rotator().ToString(),
				*ChildEntry.RelativeTransform.GetScale3D().ToString());
			++SpawnedChildActorCount;
		}

		int32 DeletedActorCount = 0;
		for (AActor* ActorToDelete : ActorsToDelete)
		{
			if (!ActorToDelete || ActorToDelete == SpawnedMasterActor)
			{
				continue;
			}

			if (EditorWorld->EditorDestroyActor(ActorToDelete, true))
			{
				++DeletedActorCount;
			}
			else
			{
				Transaction.Cancel();
				UE_LOG(LogCadImporter, Warning, TEXT("Failed to delete actor during replacement: %s"), *ActorToDelete->GetPathName());
				OutError = FString::Printf(TEXT("Failed to delete actor during replacement: %s"), *ActorToDelete->GetPathName());
				return false;
			}
		}

		OutResult.MasterActorPath = MasterDocument.MasterActorPath;
		OutResult.SpawnedActorPath = SpawnedMasterActor->GetPathName();
		OutResult.SpawnedChildActorCount = SpawnedChildActorCount;
		OutResult.DeletedActorCount = DeletedActorCount;
		OutResult.bUsedTransaction = GEditor->CanTransact();

		UE_LOG(
			LogCadImporter,
			Display,
			TEXT("Master workflow replacement complete. master=%s spawned_master=%s spawned_children=%d deleted=%d"),
			*OutResult.MasterActorPath,
			*OutResult.SpawnedActorPath,
			OutResult.SpawnedChildActorCount,
			OutResult.DeletedActorCount);
		return true;
	}
}
