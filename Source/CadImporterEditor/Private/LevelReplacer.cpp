#include "LevelReplacer.h"

#include "CadMasterActor.h"
#include "ChildDocExporter.h"
#include "CadImporterEditor.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/Level.h"
#include "EngineUtils.h"
#include "ScopedTransaction.h"

namespace
{
	struct FGeneratedHierarchyMatchData
	{
		TSet<FString> ActorNames;
		TSet<FString> ActorPaths;
	};

	struct FLevelReplaceExecutionPlan
	{
		AActor* MasterActor = nullptr;
		AActor* MasterParentActor = nullptr;
		FString RootChildJsonFolderPath;
		TArray<FCadMasterHierarchyNode> HierarchyRoots;
		TArray<AActor*> ActorsToDelete;
		TArray<AActor*> PreservedDirectChildren;
		FCadLevelReplacePlan DebugPlan;
	};

	FString NormalizeActorName(const FString& ActorName)
	{
		return ActorName.TrimStartAndEnd();
	}

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

	bool DoesActorMatchChildEntry(const AActor* Actor, const FCadChildEntry& ChildEntry)
	{
		if (!Actor)
		{
			return false;
		}

		const FString EntryActorName = NormalizeActorName(ChildEntry.ActorName);
		if (!EntryActorName.IsEmpty() && (Actor->GetActorNameOrLabel() == EntryActorName || Actor->GetName() == EntryActorName))
		{
			return true;
		}

		return !ChildEntry.ActorPath.IsEmpty() && Actor->GetPathName() == ChildEntry.ActorPath;
	}

	bool DoesActorMatchHierarchyNode(const AActor* Actor, const FCadMasterHierarchyNode& Node)
	{
		if (!Actor)
		{
			return false;
		}

		const FString EntryActorName = NormalizeActorName(Node.ActorName);
		if (!EntryActorName.IsEmpty() && (Actor->GetActorNameOrLabel() == EntryActorName || Actor->GetName() == EntryActorName))
		{
			return true;
		}

		return !Node.ActorPath.IsEmpty() && Actor->GetPathName() == Node.ActorPath;
	}

	FCadMasterHierarchyNode BuildLevelReplacerHierarchyNodeFromChildEntry(const FCadChildEntry& ChildEntry)
	{
		FCadMasterHierarchyNode Node;
		Node.ActorName = ChildEntry.ActorName;
		Node.ActorPath = ChildEntry.ActorPath;
		Node.RelativeTransform = ChildEntry.RelativeTransform;
		Node.NodeType = CadMasterNodeTypeFromChildActorType(ChildEntry.ActorType);
		Node.ChildJsonFileName = ChildEntry.ChildJsonFileName;
		return Node;
	}

	TArray<FCadMasterHierarchyNode> ResolveHierarchyRoots(const FCadMasterDoc& MasterDocument)
	{
		if (MasterDocument.HierarchyChildren.Num() > 0)
		{
			return MasterDocument.HierarchyChildren;
		}

		TArray<FCadMasterHierarchyNode> HierarchyRoots;
		HierarchyRoots.Reserve(MasterDocument.Children.Num());
		for (const FCadChildEntry& ChildEntry : MasterDocument.Children)
		{
			if (!CadMasterChildActorTypeShouldGenerateJson(ChildEntry.ActorType))
			{
				continue;
			}

			HierarchyRoots.Add(BuildLevelReplacerHierarchyNodeFromChildEntry(ChildEntry));
		}

		return HierarchyRoots;
	}

	void CollectHierarchyNodesRecursive(const FCadMasterHierarchyNode& Node, TArray<const FCadMasterHierarchyNode*>& OutNodes)
	{
		OutNodes.Add(&Node);
		for (const FCadMasterHierarchyNode& ChildNode : Node.Children)
		{
			CollectHierarchyNodesRecursive(ChildNode, OutNodes);
		}
	}

	FString ResolveDocumentChildJsonFolderPath(const FCadMasterDoc& MasterDocument);

	bool TryCollectGeneratedHierarchyMatchDataRecursive(
		const FCadMasterDoc& MasterDocument,
		const FString& ChildJsonFolderPath,
		FGeneratedHierarchyMatchData& InOutMatchData,
		TSet<FString>& InOutVisitedMasterJsonPaths,
		FString& OutError);

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

	void CollectActorHierarchyActors(AActor* RootActor, TArray<AActor*>& OutActors)
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

	void CollectDirectAttachedChildren(AActor* RootActor, TArray<AActor*>& OutChildren)
	{
		OutChildren.Reset();
		if (!RootActor)
		{
			return;
		}

		RootActor->GetAttachedActors(OutChildren, true, false);
	}

	void RemoveActorSubtreeFromDeleteList(AActor* RootActor, TArray<AActor*>& InOutActorsToDelete)
	{
		if (!RootActor)
		{
			return;
		}

		TArray<AActor*> PreservedActors;
		CollectActorHierarchyActors(RootActor, PreservedActors);
		InOutActorsToDelete.RemoveAll([&PreservedActors](AActor* Candidate)
		{
			return Candidate && PreservedActors.Contains(Candidate);
		});
	}

	void PrepareActorsForDeletion(const TArray<AActor*>& ActorsToDelete)
	{
		TSet<AActor*> DeleteSet;
		for (AActor* ActorToDelete : ActorsToDelete)
		{
			if (ActorToDelete)
			{
				DeleteSet.Add(ActorToDelete);
			}
		}

		for (AActor* ActorToDelete : ActorsToDelete)
		{
			if (!ActorToDelete)
			{
				continue;
			}

			if (AActor* ParentActor = ActorToDelete->GetAttachParentActor())
			{
				if (!DeleteSet.Contains(ParentActor))
				{
					ActorToDelete->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
				}
			}

			TArray<AActor*> DirectChildren;
			ActorToDelete->GetAttachedActors(DirectChildren, true, false);
			for (AActor* DirectChild : DirectChildren)
			{
				if (!DirectChild || DeleteSet.Contains(DirectChild))
				{
					continue;
				}

				DirectChild->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			}
		}
	}

	bool CollectPreservedDirectChildren(
		AActor* MasterActor,
		const FCadMasterDoc& MasterDocument,
		TArray<AActor*>& OutPreservedChildren,
		FString& OutError)
	{
		OutPreservedChildren.Reset();
		OutError.Reset();
		if (!MasterActor)
		{
			return true;
		}

		TArray<AActor*> DirectChildren;
		CollectDirectAttachedChildren(MasterActor, DirectChildren);
		FGeneratedHierarchyMatchData GeneratedMatchData;
		TSet<FString> VisitedMasterJsonPaths;
		if (!TryCollectGeneratedHierarchyMatchDataRecursive(
			MasterDocument,
			ResolveDocumentChildJsonFolderPath(MasterDocument),
			GeneratedMatchData,
			VisitedMasterJsonPaths,
			OutError))
		{
			return false;
		}

		for (AActor* DirectChild : DirectChildren)
		{
			if (!DirectChild)
			{
				continue;
			}

			const FString ActorLabel = NormalizeActorName(DirectChild->GetActorNameOrLabel());
			const FString ActorObjectName = NormalizeActorName(DirectChild->GetName());
			const FString ActorPath = DirectChild->GetPathName();
			const bool bWillBeGenerated =
				(!ActorLabel.IsEmpty() && GeneratedMatchData.ActorNames.Contains(ActorLabel)) ||
				(!ActorObjectName.IsEmpty() && GeneratedMatchData.ActorNames.Contains(ActorObjectName)) ||
				(!ActorPath.IsEmpty() && GeneratedMatchData.ActorPaths.Contains(ActorPath));
			if (!bWillBeGenerated)
			{
				OutPreservedChildren.Add(DirectChild);
				UE_LOG(
					LogCadImporter,
					Display,
					TEXT("Preserving unmatched direct child during replacement. actor=%s label=%s"),
					*ActorPath,
					*ActorLabel);
			}
		}

		return true;
	}

	ECadMasterPlacementNodeType ToLevelReplacerPlacementNodeType(const ECadMasterNodeType NodeType)
	{
		switch (NodeType)
		{
		case ECadMasterNodeType::Background:
			return ECadMasterPlacementNodeType::Background;
		case ECadMasterNodeType::Robot:
			return ECadMasterPlacementNodeType::Robot;
		case ECadMasterNodeType::Master:
			return ECadMasterPlacementNodeType::Master;
		case ECadMasterNodeType::Static:
		default:
			return ECadMasterPlacementNodeType::Static;
		}
	}

	void AppendLevelReplacerPlacementNodesRecursive(
		const FCadMasterHierarchyNode& SourceNode,
		const int32 ParentNodeIndex,
		TArray<FCadMasterPlacementNodeRecord>& OutNodes)
	{
		FCadMasterPlacementNodeRecord PlacementNode;
		PlacementNode.NodeName = SourceNode.ActorName;
		PlacementNode.NodeType = ToLevelReplacerPlacementNodeType(SourceNode.NodeType);
		PlacementNode.ChildJsonFileName = SourceNode.ChildJsonFileName;
		PlacementNode.MasterJsonFileName = SourceNode.MasterJsonFileName;
		PlacementNode.RelativeTransform = SourceNode.RelativeTransform;
		PlacementNode.ParentNodeIndex = ParentNodeIndex;
		const int32 CurrentNodeIndex = OutNodes.Add(MoveTemp(PlacementNode));
		for (const FCadMasterHierarchyNode& ChildNode : SourceNode.Children)
		{
			AppendLevelReplacerPlacementNodesRecursive(ChildNode, CurrentNodeIndex, OutNodes);
		}
	}

	void AppendLeafPlacementsRecursive(
		const FCadMasterHierarchyNode& Node,
		const FTransform& ParentAccumulatedTransform,
		TArray<FCadMasterChildPlacement>& OutPlacements)
	{
		const FTransform AccumulatedTransform = Node.RelativeTransform * ParentAccumulatedTransform;
		if (CadMasterNodeTypeUsesChildJson(Node.NodeType))
		{
			FCadMasterChildPlacement Placement;
			Placement.ChildName = Node.ActorName;
			Placement.ChildJsonFileName = Node.ChildJsonFileName;
			Placement.RelativeTransform = AccumulatedTransform;
			Placement.bMovable = (Node.NodeType == ECadMasterNodeType::Robot);
			OutPlacements.Add(MoveTemp(Placement));
			return;
		}

		for (const FCadMasterHierarchyNode& ChildNode : Node.Children)
		{
			AppendLeafPlacementsRecursive(ChildNode, AccumulatedTransform, OutPlacements);
		}
	}

	void ConfigureSpawnedMasterNodeActor(
		ACadMasterActor& SpawnedActor,
		const FCadMasterDoc& MasterDocument)
	{
		SpawnedActor.Metadata.MasterName = MasterDocument.MasterName;
		SpawnedActor.Metadata.WorkspaceFolder = MasterDocument.WorkspaceFolder;
		SpawnedActor.Metadata.SchemaVersion = TEXT("master_json_v2");
		SpawnedActor.HierarchyNodes.Reset();
		SpawnedActor.ChildPlacements.Reset();

		const TArray<FCadMasterHierarchyNode> HierarchyRoots = ResolveHierarchyRoots(MasterDocument);
		for (const FCadMasterHierarchyNode& ChildNode : HierarchyRoots)
		{
			AppendLevelReplacerPlacementNodesRecursive(ChildNode, INDEX_NONE, SpawnedActor.HierarchyNodes);
			AppendLeafPlacementsRecursive(ChildNode, FTransform::Identity, SpawnedActor.ChildPlacements);
		}
	}

	FString ResolveDocumentChildJsonFolderPath(const FCadMasterDoc& MasterDocument)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(MasterDocument.WorkspaceFolder, MasterDocument.ChildJsonFolderName));
	}

	FString BuildChildJsonLookupKey(const FString& ChildJsonFolderPath, const FString& ChildJsonFileName)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(ChildJsonFolderPath, ChildJsonFileName));
	}

	void AppendGeneratedHierarchyMatchData(
		const FCadMasterHierarchyNode& Node,
		FGeneratedHierarchyMatchData& InOutMatchData)
	{
		const FString NormalizedActorName = NormalizeActorName(Node.ActorName);
		if (!NormalizedActorName.IsEmpty())
		{
			InOutMatchData.ActorNames.Add(NormalizedActorName);
		}

		if (!Node.ActorPath.IsEmpty())
		{
			InOutMatchData.ActorPaths.Add(Node.ActorPath);
		}
	}

	void CollectDirectLeafChildrenForReplace(const FCadMasterDoc& MasterDocument, TArray<FCadChildEntry>& OutChildren)
	{
		OutChildren.Reset();
		if (MasterDocument.HierarchyChildren.Num() == 0)
		{
			OutChildren = MasterDocument.Children;
			return;
		}

		for (const FCadMasterHierarchyNode& Node : MasterDocument.HierarchyChildren)
		{
			if (!CadMasterNodeTypeUsesChildJson(Node.NodeType))
			{
				continue;
			}

			FCadChildEntry ChildEntry;
			ChildEntry.ActorName = Node.ActorName;
			ChildEntry.ActorPath = Node.ActorPath;
			ChildEntry.RelativeTransform = Node.RelativeTransform;
			ChildEntry.ActorType = CadMasterChildActorTypeFromNodeType(Node.NodeType);
			ChildEntry.ChildJsonFileName = Node.ChildJsonFileName;
			OutChildren.Add(MoveTemp(ChildEntry));
		}
	}

	FCadMasterDoc BuildInlineNestedMasterDocumentForReplace(
		const FCadMasterDoc& ParentDocument,
		const FCadMasterHierarchyNode& MasterNode,
		const FString& ParentChildJsonFolderPath)
	{
		FCadMasterDoc NestedDocument;
		NestedDocument.MasterName = MasterNode.ActorName;
		NestedDocument.MasterActorPath = MasterNode.ActorPath;
		NestedDocument.MasterWorldTransform = MasterNode.RelativeTransform * ParentDocument.MasterWorldTransform;
		NestedDocument.WorkspaceFolder = ParentChildJsonFolderPath;
		NestedDocument.ChildJsonFolderName = MasterNode.ChildJsonFolderName.TrimStartAndEnd().IsEmpty()
			? FPaths::MakeValidFileName(MasterNode.ActorName)
			: MasterNode.ChildJsonFolderName;
		if (NestedDocument.ChildJsonFolderName.IsEmpty())
		{
			NestedDocument.ChildJsonFolderName = TEXT("Master");
		}
		NestedDocument.ContentRootPath = ParentDocument.ContentRootPath.TrimStartAndEnd().IsEmpty()
			? FString::Printf(TEXT("/Game/%s"), *NestedDocument.ChildJsonFolderName)
			: FString::Printf(TEXT("%s/%s"), *ParentDocument.ContentRootPath, *NestedDocument.ChildJsonFolderName);
		NestedDocument.HierarchyChildren = MasterNode.Children;
		CollectDirectLeafChildrenForReplace(NestedDocument, NestedDocument.Children);
		return NestedDocument;
	}

	bool TryResolveNestedMasterDocumentForReplace(
		const FCadMasterDoc& ParentDocument,
		const FString& ParentChildJsonFolderPath,
		const FCadMasterHierarchyNode& MasterNode,
		FCadMasterDoc& OutNestedDocument,
		FString& OutNestedMasterJsonPath,
		FString& OutError)
	{
		OutNestedDocument = FCadMasterDoc();
		OutNestedMasterJsonPath.Reset();
		if (!MasterNode.MasterJsonFileName.TrimStartAndEnd().IsEmpty())
		{
			OutNestedMasterJsonPath = BuildChildJsonLookupKey(ParentChildJsonFolderPath, MasterNode.MasterJsonFileName);
			return CadChildDocExporter::TryParseMasterDocument(OutNestedMasterJsonPath, OutNestedDocument, OutError);
		}

		if (MasterNode.Children.Num() == 0)
		{
			OutError = FString::Printf(TEXT("Nested master '%s' has no master_json_file_name and no inline children."), *MasterNode.ActorName);
			return false;
		}

		OutNestedMasterJsonPath = FPaths::Combine(ParentChildJsonFolderPath, FString::Printf(TEXT("%s.json"), *FPaths::MakeValidFileName(MasterNode.ActorName)));
		OutNestedDocument = BuildInlineNestedMasterDocumentForReplace(ParentDocument, MasterNode, ParentChildJsonFolderPath);
		return true;
	}

	bool TryCollectGeneratedHierarchyMatchDataRecursive(
		const FCadMasterDoc& MasterDocument,
		const FString& ChildJsonFolderPath,
		FGeneratedHierarchyMatchData& InOutMatchData,
		TSet<FString>& InOutVisitedMasterJsonPaths,
		FString& OutError)
	{
		for (const FCadMasterHierarchyNode& Node : ResolveHierarchyRoots(MasterDocument))
		{
			AppendGeneratedHierarchyMatchData(Node, InOutMatchData);

			if (Node.NodeType != ECadMasterNodeType::Master)
			{
				continue;
			}

			FCadMasterDoc NestedMasterDocument;
			FString NestedMasterJsonPath;
			if (!TryResolveNestedMasterDocumentForReplace(
				MasterDocument,
				ChildJsonFolderPath,
				Node,
				NestedMasterDocument,
				NestedMasterJsonPath,
				OutError))
			{
				return false;
			}

			const FString NormalizedNestedMasterJsonPath = FPaths::ConvertRelativePathToFull(NestedMasterJsonPath);
			if (!NormalizedNestedMasterJsonPath.IsEmpty())
			{
				if (InOutVisitedMasterJsonPaths.Contains(NormalizedNestedMasterJsonPath))
				{
					continue;
				}

				InOutVisitedMasterJsonPaths.Add(NormalizedNestedMasterJsonPath);
			}

			if (!TryCollectGeneratedHierarchyMatchDataRecursive(
				NestedMasterDocument,
				ResolveDocumentChildJsonFolderPath(NestedMasterDocument),
				InOutMatchData,
				InOutVisitedMasterJsonPaths,
				OutError))
			{
				return false;
			}
		}

		return true;
	}

	bool ValidateHierarchyNodeBlueprints(
		const FCadMasterDoc& MasterDocument,
		const FString& ChildJsonFolderPath,
		const FCadMasterHierarchyNode& Node,
		const TMap<FString, UBlueprint*>& MasterBlueprintsByJsonPath,
		const TMap<FString, UBlueprint*>& ChildBlueprintsByJsonPath,
		FString& OutError)
	{
		const FString NodeName = Node.ActorName.TrimStartAndEnd();
		if (NodeName.IsEmpty())
		{
			OutError = TEXT("Master json contains a hierarchy node with empty actor_name.");
			return false;
		}

		if (Node.NodeType == ECadMasterNodeType::Master)
		{
			FCadMasterDoc NestedMasterDocument;
			FString NestedMasterJsonPath;
			if (!TryResolveNestedMasterDocumentForReplace(
				MasterDocument,
				ChildJsonFolderPath,
				Node,
				NestedMasterDocument,
				NestedMasterJsonPath,
				OutError))
			{
				return false;
			}

			UBlueprint* const* NestedMasterBlueprintPtr = MasterBlueprintsByJsonPath.Find(FPaths::ConvertRelativePathToFull(NestedMasterJsonPath));
			if (!NestedMasterBlueprintPtr || !(*NestedMasterBlueprintPtr) || !(*NestedMasterBlueprintPtr)->GeneratedClass)
			{
				OutError = FString::Printf(TEXT("Nested master blueprint was not built or is invalid for node '%s' (%s)."), *NodeName, *NestedMasterJsonPath);
				return false;
			}

			const FString NestedChildJsonFolderPath = ResolveDocumentChildJsonFolderPath(NestedMasterDocument);
			for (const FCadMasterHierarchyNode& ChildNode : NestedMasterDocument.HierarchyChildren)
			{
				if (!ValidateHierarchyNodeBlueprints(NestedMasterDocument, NestedChildJsonFolderPath, ChildNode, MasterBlueprintsByJsonPath, ChildBlueprintsByJsonPath, OutError))
				{
					return false;
				}
			}

			return true;
		}

		const FString ChildJsonLookupKey = BuildChildJsonLookupKey(ChildJsonFolderPath, Node.ChildJsonFileName);
		UBlueprint* const* ChildBlueprintPtr = ChildBlueprintsByJsonPath.Find(ChildJsonLookupKey);
		if (!ChildBlueprintPtr || !(*ChildBlueprintPtr) || !(*ChildBlueprintPtr)->GeneratedClass)
		{
			OutError = FString::Printf(TEXT("Child blueprint was not built or is invalid for node '%s' (%s)."), *NodeName, *ChildJsonLookupKey);
			return false;
		}

		return true;
	}

	bool SpawnHierarchyNodeRecursive(
		UWorld& EditorWorld,
		AActor& ParentActor,
		const FTransform& ParentWorldTransform,
		const FCadMasterDoc& MasterDocument,
		const FString& ChildJsonFolderPath,
		const FCadMasterHierarchyNode& Node,
		const TMap<FString, UBlueprint*>& MasterBlueprintsByJsonPath,
		const TMap<FString, UBlueprint*>& ChildBlueprintsByJsonPath,
		const FActorSpawnParameters& SpawnParams,
		int32& InOutSpawnedActorCount,
		FString& OutError)
	{
		const FString NodeName = Node.ActorName.TrimStartAndEnd();
		const FTransform NodeWorldTransform = Node.RelativeTransform * ParentWorldTransform;

		if (Node.NodeType == ECadMasterNodeType::Master)
		{
			FCadMasterDoc NestedMasterDocument;
			FString NestedMasterJsonPath;
			if (!TryResolveNestedMasterDocumentForReplace(
				MasterDocument,
				ChildJsonFolderPath,
				Node,
				NestedMasterDocument,
				NestedMasterJsonPath,
				OutError))
			{
				return false;
			}

			UBlueprint* const* NestedMasterBlueprintPtr = MasterBlueprintsByJsonPath.Find(FPaths::ConvertRelativePathToFull(NestedMasterJsonPath));
			if (!NestedMasterBlueprintPtr || !(*NestedMasterBlueprintPtr) || !(*NestedMasterBlueprintPtr)->GeneratedClass)
			{
				OutError = FString::Printf(TEXT("Nested master blueprint was not built or is invalid for node '%s' (%s)."), *NodeName, *NestedMasterJsonPath);
				return false;
			}

			ACadMasterActor* SpawnedMasterNodeActor = EditorWorld.SpawnActor<ACadMasterActor>(
				(*NestedMasterBlueprintPtr)->GeneratedClass,
				NodeWorldTransform,
				SpawnParams);
			if (!SpawnedMasterNodeActor)
			{
				OutError = FString::Printf(TEXT("Failed to spawn nested master actor for '%s'."), *NodeName);
				return false;
			}

			SpawnedMasterNodeActor->SetFlags(RF_Transactional);
			SpawnedMasterNodeActor->Modify();
			SpawnedMasterNodeActor->AttachToActor(&ParentActor, FAttachmentTransformRules::KeepWorldTransform);
			SpawnedMasterNodeActor->SetActorRelativeTransform(Node.RelativeTransform);
			SpawnedMasterNodeActor->SetActorLabel(FString::Printf(TEXT("Master_%s"), *NodeName), true);
			ConfigureSpawnedMasterNodeActor(*SpawnedMasterNodeActor, NestedMasterDocument);
			++InOutSpawnedActorCount;

			const FString NestedChildJsonFolderPath = ResolveDocumentChildJsonFolderPath(NestedMasterDocument);
			for (const FCadMasterHierarchyNode& ChildNode : NestedMasterDocument.HierarchyChildren)
			{
				if (!SpawnHierarchyNodeRecursive(
					EditorWorld,
					*SpawnedMasterNodeActor,
					NodeWorldTransform,
					NestedMasterDocument,
					NestedChildJsonFolderPath,
					ChildNode,
					MasterBlueprintsByJsonPath,
					ChildBlueprintsByJsonPath,
					SpawnParams,
					InOutSpawnedActorCount,
					OutError))
				{
					return false;
				}
			}

			return true;
		}

		const FString ChildJsonLookupKey = BuildChildJsonLookupKey(ChildJsonFolderPath, Node.ChildJsonFileName);
		UBlueprint* const* ChildBlueprintPtr = ChildBlueprintsByJsonPath.Find(ChildJsonLookupKey);
		if (!ChildBlueprintPtr || !(*ChildBlueprintPtr) || !(*ChildBlueprintPtr)->GeneratedClass)
		{
			OutError = FString::Printf(TEXT("Child blueprint was not built or is invalid for node '%s' (%s)."), *NodeName, *ChildJsonLookupKey);
			return false;
		}

		AActor* SpawnedChildActor = EditorWorld.SpawnActor<AActor>((*ChildBlueprintPtr)->GeneratedClass, NodeWorldTransform, SpawnParams);
		if (!SpawnedChildActor)
		{
			OutError = FString::Printf(TEXT("Failed to spawn child blueprint actor for '%s'."), *NodeName);
			return false;
		}

		SpawnedChildActor->SetFlags(RF_Transactional);
		SpawnedChildActor->Modify();
		SpawnedChildActor->AttachToActor(&ParentActor, FAttachmentTransformRules::KeepWorldTransform);
		SpawnedChildActor->SetActorRelativeTransform(Node.RelativeTransform);
		SpawnedChildActor->SetActorLabel(FString::Printf(TEXT("BP_%s"), *NodeName), true);
		UE_LOG(
			LogCadImporter,
			Display,
			TEXT("Spawned hierarchy blueprint actor. node=%s type=%d actor=%s relative_location=%s relative_rotation=%s relative_scale=%s"),
			*NodeName,
			static_cast<int32>(Node.NodeType),
			*SpawnedChildActor->GetPathName(),
			*Node.RelativeTransform.GetLocation().ToString(),
			*Node.RelativeTransform.Rotator().ToString(),
			*Node.RelativeTransform.GetScale3D().ToString());
		++InOutSpawnedActorCount;
		return true;
	}

	bool TryBuildExecutionPlan(
		const FCadMasterDoc& MasterDocument,
		const TMap<FString, UBlueprint*>& MasterBlueprintsByJsonPath,
		const TMap<FString, UBlueprint*>& ChildBlueprintsByJsonPath,
		FLevelReplaceExecutionPlan& OutPlan,
		FString& OutError)
	{
		OutPlan = FLevelReplaceExecutionPlan();
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

		OutPlan.MasterActor = FindMasterActorInEditorWorld(MasterDocument.MasterActorPath);
		if (!OutPlan.MasterActor)
		{
			OutError = FString::Printf(TEXT("Master actor not found in level: %s"), *MasterDocument.MasterActorPath);
			return false;
		}

		OutPlan.MasterParentActor = OutPlan.MasterActor->GetAttachParentActor();
		CollectActorHierarchyActors(OutPlan.MasterActor, OutPlan.ActorsToDelete);
		if (OutPlan.ActorsToDelete.Num() == 0)
		{
			OutError = TEXT("No level actors were found for replacement.");
			return false;
		}

		if (!CollectPreservedDirectChildren(OutPlan.MasterActor, MasterDocument, OutPlan.PreservedDirectChildren, OutError))
		{
			return false;
		}
		for (AActor* PreservedChild : OutPlan.PreservedDirectChildren)
		{
			RemoveActorSubtreeFromDeleteList(PreservedChild, OutPlan.ActorsToDelete);
		}

		OutPlan.HierarchyRoots = ResolveHierarchyRoots(MasterDocument);
		OutPlan.RootChildJsonFolderPath = ResolveDocumentChildJsonFolderPath(MasterDocument);
		for (const FCadMasterHierarchyNode& HierarchyRoot : OutPlan.HierarchyRoots)
		{
			if (!ValidateHierarchyNodeBlueprints(
				MasterDocument,
				OutPlan.RootChildJsonFolderPath,
				HierarchyRoot,
				MasterBlueprintsByJsonPath,
				ChildBlueprintsByJsonPath,
				OutError))
			{
				return false;
			}
		}

		OutPlan.DebugPlan.MasterActorPath = OutPlan.MasterActor->GetPathName();
		OutPlan.DebugPlan.MasterParentActorPath = OutPlan.MasterParentActor ? OutPlan.MasterParentActor->GetPathName() : FString();
		OutPlan.DebugPlan.ChildJsonRootPath = OutPlan.RootChildJsonFolderPath;
		OutPlan.DebugPlan.HierarchyRootCount = OutPlan.HierarchyRoots.Num();
		for (AActor* ActorToDelete : OutPlan.ActorsToDelete)
		{
			if (ActorToDelete)
			{
				OutPlan.DebugPlan.CandidateDeleteActorPaths.Add(ActorToDelete->GetPathName());
			}
		}
		for (AActor* PreservedChild : OutPlan.PreservedDirectChildren)
		{
			if (PreservedChild)
			{
				OutPlan.DebugPlan.PreservedDirectChildPaths.Add(PreservedChild->GetPathName());
			}
		}
		OutPlan.DebugPlan.CandidateDeleteActorCount = OutPlan.DebugPlan.CandidateDeleteActorPaths.Num();
		OutPlan.DebugPlan.PreservedDirectChildCount = OutPlan.DebugPlan.PreservedDirectChildPaths.Num();
		OutPlan.DebugPlan.bHasDestructiveChanges = OutPlan.DebugPlan.CandidateDeleteActorCount > 0;

		return true;
	}

	bool ValidateExecutionPlanForApply(const FLevelReplaceExecutionPlan& ExecutionPlan, FString& OutError)
	{
		OutError.Reset();
		if (!ExecutionPlan.MasterActor)
		{
			OutError = TEXT("Replacement plan is invalid: master actor is null.");
			return false;
		}

		for (AActor* PreservedChild : ExecutionPlan.PreservedDirectChildren)
		{
			if (!PreservedChild)
			{
				continue;
			}

			if (ExecutionPlan.ActorsToDelete.Contains(PreservedChild))
			{
				OutError = FString::Printf(
					TEXT("Replacement plan is invalid: preserved child is still marked for deletion: %s"),
					*PreservedChild->GetPathName());
				return false;
			}
		}

		return true;
	}
}

namespace CadLevelReplacer
{
	bool TryBuildReplacementPlan(
		const FCadMasterDoc& MasterDocument,
		const TMap<FString, UBlueprint*>& MasterBlueprintsByJsonPath,
		const TMap<FString, UBlueprint*>& ChildBlueprintsByJsonPath,
		FCadLevelReplacePlan& OutPlan,
		FString& OutError)
	{
		FLevelReplaceExecutionPlan ExecutionPlan;
		const bool bSuccess = TryBuildExecutionPlan(
			MasterDocument,
			MasterBlueprintsByJsonPath,
			ChildBlueprintsByJsonPath,
			ExecutionPlan,
			OutError);
		OutPlan = bSuccess ? ExecutionPlan.DebugPlan : FCadLevelReplacePlan();
		return bSuccess;
	}

	bool TryReplaceMasterHierarchyWithBlueprints(
		const FCadMasterDoc& MasterDocument,
		UBlueprint* MasterBlueprint,
		const TMap<FString, UBlueprint*>& MasterBlueprintsByJsonPath,
		const TMap<FString, UBlueprint*>& ChildBlueprintsByJsonPath,
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

		FLevelReplaceExecutionPlan ExecutionPlan;
		if (!TryBuildExecutionPlan(
			MasterDocument,
			MasterBlueprintsByJsonPath,
			ChildBlueprintsByJsonPath,
			ExecutionPlan,
			OutError))
		{
			return false;
		}
		if (!ValidateExecutionPlanForApply(ExecutionPlan, OutError))
		{
			return false;
		}

		AActor* const MasterActor = ExecutionPlan.MasterActor;
		AActor* const MasterParentActor = ExecutionPlan.MasterParentActor;
		const TArray<FCadMasterHierarchyNode>& HierarchyRoots = ExecutionPlan.HierarchyRoots;
		const FString& RootChildJsonFolderPath = ExecutionPlan.RootChildJsonFolderPath;
		TArray<AActor*>& ActorsToDelete = ExecutionPlan.ActorsToDelete;
		TArray<AActor*>& PreservedDirectChildren = ExecutionPlan.PreservedDirectChildren;
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
		if (MasterParentActor)
		{
			MasterParentActor->Modify();
		}
		for (AActor* ActorToDelete : ActorsToDelete)
		{
			if (ActorToDelete)
			{
				ActorToDelete->Modify();
			}
		}
		for (AActor* PreservedChild : PreservedDirectChildren)
		{
			if (PreservedChild)
			{
				PreservedChild->Modify();
			}
		}

		const FTransform SpawnTransform = MasterActor->GetActorTransform();
		const FTransform SpawnRelativeTransform = MasterParentActor
			? SpawnTransform.GetRelativeTransform(MasterParentActor->GetActorTransform())
			: SpawnTransform;
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
		if (MasterParentActor)
		{
			SpawnedMasterActor->AttachToActor(MasterParentActor, FAttachmentTransformRules::KeepWorldTransform);
			SpawnedMasterActor->SetActorRelativeTransform(SpawnRelativeTransform);
		}
		if (ACadMasterActor* SpawnedCadMasterActor = Cast<ACadMasterActor>(SpawnedMasterActor))
		{
			ConfigureSpawnedMasterNodeActor(*SpawnedCadMasterActor, MasterDocument);
		}

		int32 SpawnedChildActorCount = 0;
		for (const FCadMasterHierarchyNode& HierarchyRoot : HierarchyRoots)
		{
			if (!SpawnHierarchyNodeRecursive(
				*EditorWorld,
				*SpawnedMasterActor,
				SpawnTransform,
				MasterDocument,
				RootChildJsonFolderPath,
				HierarchyRoot,
				MasterBlueprintsByJsonPath,
				ChildBlueprintsByJsonPath,
				SpawnParams,
				SpawnedChildActorCount,
				OutError))
			{
				Transaction.Cancel();
				return false;
			}
		}

		int32 PreservedChildActorCount = 0;
		for (AActor* PreservedChild : PreservedDirectChildren)
		{
			if (!PreservedChild)
			{
				continue;
			}

			const bool bReparented = MasterParentActor
				? PreservedChild->AttachToActor(MasterParentActor, FAttachmentTransformRules::KeepWorldTransform)
				: (PreservedChild->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform), true);
			if (!bReparented)
			{
				Transaction.Cancel();
				OutError = FString::Printf(TEXT("Failed to preserve excluded child actor during replacement: %s"), *PreservedChild->GetPathName());
				return false;
			}

			++PreservedChildActorCount;
		}

		PrepareActorsForDeletion(ActorsToDelete);

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
			else if (EditorWorld->DestroyActor(ActorToDelete, true, true))
			{
				++DeletedActorCount;
				UE_LOG(LogCadImporter, Warning, TEXT("EditorDestroyActor failed, but DestroyActor fallback succeeded: %s"), *ActorToDelete->GetPathName());
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
		OutResult.PreservedChildActorCount = PreservedChildActorCount;
		OutResult.DeletedActorCount = DeletedActorCount;
		OutResult.bUsedTransaction = GEditor->CanTransact();

		UE_LOG(
			LogCadImporter,
			Display,
			TEXT("Master workflow replacement complete. master=%s spawned_master=%s spawned_children=%d preserved=%d deleted=%d"),
			*OutResult.MasterActorPath,
			*OutResult.SpawnedActorPath,
			OutResult.SpawnedChildActorCount,
			OutResult.PreservedChildActorCount,
			OutResult.DeletedActorCount);
		return true;
	}
}
