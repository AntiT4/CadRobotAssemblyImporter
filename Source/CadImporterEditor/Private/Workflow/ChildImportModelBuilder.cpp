#include "Workflow/ChildImportModelBuilder.h"

namespace
{
	void FillImportModelUnitsDefaults(FCadImportModel& InOutModel)
	{
		InOutModel.Units.Length = TEXT("centimeter");
		InOutModel.Units.Angle = TEXT("degree");
		InOutModel.Units.UpAxis = TEXT("z");
		InOutModel.Units.FrontAxis = TEXT("x");
		InOutModel.Units.Handedness = TEXT("left");
		InOutModel.Units.EulerOrder = TEXT("xyz");
		InOutModel.Units.MeshScale = 1.0f;
	}

	FString ResolveChildRootLinkName(const FCadChildDoc& ChildDocument, const FCadChildEntry& ChildEntry)
	{
		for (const FCadChildLinkDef& LinkTemplate : ChildDocument.Links)
		{
			const FString LinkName = LinkTemplate.LinkName.TrimStartAndEnd();
			if (!LinkName.IsEmpty())
			{
				return LinkName;
			}
		}

		const FString ChildName = ChildDocument.ChildActorName.TrimStartAndEnd();
		if (!ChildName.IsEmpty())
		{
			return ChildName;
		}

		return ChildEntry.ActorName.TrimStartAndEnd();
	}

	void AppendChildVisualsToLink(const TArray<FCadChildVisual>& ChildVisuals, FCadImportLink& OutLink)
	{
		for (const FCadChildVisual& ChildVisual : ChildVisuals)
		{
			FCadImportVisual Visual;
			Visual.MeshPath = ChildVisual.MeshPath;
			Visual.Transform = ChildVisual.RelativeTransform;
			Visual.MaterialPath = ChildVisual.MaterialPath;
			Visual.MaterialName = ChildVisual.MaterialName;
			OutLink.Visuals.Add(MoveTemp(Visual));
		}
	}

	FString ExtractMasterActorNameFromPathForPlacement(const FString& ActorPath)
	{
		const FString TrimmedPath = ActorPath.TrimStartAndEnd();
		if (TrimmedPath.IsEmpty())
		{
			return FString();
		}

		int32 SeparatorIndex = INDEX_NONE;
		if (!TrimmedPath.FindLastChar(TEXT('.'), SeparatorIndex))
		{
			TrimmedPath.FindLastChar(TEXT(':'), SeparatorIndex);
		}

		return (SeparatorIndex != INDEX_NONE && SeparatorIndex + 1 < TrimmedPath.Len())
			? TrimmedPath.Mid(SeparatorIndex + 1).TrimStartAndEnd()
			: TrimmedPath;
	}

	void FillRootPlacementHint(
		const FCadMasterDoc& MasterDocument,
		const FCadChildEntry& ChildEntry,
		FCadImportModel& OutModel)
	{
		OutModel.RootPlacement.bHasWorldTransform = true;
		OutModel.RootPlacement.WorldTransform = ChildEntry.RelativeTransform * MasterDocument.MasterWorldTransform;
		OutModel.RootPlacement.ParentActorName = ExtractMasterActorNameFromPathForPlacement(MasterDocument.MasterActorPath);
		if (OutModel.RootPlacement.ParentActorName.IsEmpty())
		{
			OutModel.RootPlacement.ParentActorName = MasterDocument.MasterName.TrimStartAndEnd();
		}
	}
}

namespace CadChildImportModelBuilder
{
	bool TryBuildImportModel(
		const FCadMasterDoc& MasterDocument,
		const FCadChildEntry& ChildEntry,
		const FCadChildDoc& ChildDocument,
		const FString& ChildJsonFolderPath,
		const FString& OutputRootPath,
		FCadImportModel& OutModel,
		FString& OutError)
	{
		OutModel = FCadImportModel();
		OutError.Reset();

		const FString RootLinkName = ResolveChildRootLinkName(ChildDocument, ChildEntry);
		if (RootLinkName.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Child '%s' has no valid root link name."), *ChildEntry.ActorName);
			return false;
		}

		OutModel.Profile = (ChildDocument.ActorType == ECadMasterChildActorType::Movable)
			? ECadImportModelProfile::DynamicRobot
			: ECadImportModelProfile::FixedAssembly;
		OutModel.RobotName = ChildEntry.ActorName.TrimStartAndEnd().IsEmpty()
			? RootLinkName
			: ChildEntry.ActorName.TrimStartAndEnd();
		OutModel.OutputRootPath = OutputRootPath.TrimStartAndEnd();
		OutModel.RootLinkName = RootLinkName;
		OutModel.SourceDirectory = ChildJsonFolderPath;
		FillImportModelUnitsDefaults(OutModel);
		FillRootPlacementHint(MasterDocument, ChildEntry, OutModel);

		if (ChildDocument.Links.Num() > 0)
		{
			for (const FCadChildLinkDef& LinkTemplate : ChildDocument.Links)
			{
				const FString LinkName = LinkTemplate.LinkName.TrimStartAndEnd();
				if (LinkName.IsEmpty())
				{
					OutError = FString::Printf(TEXT("Child '%s' has an empty link_name in links[]."), *ChildEntry.ActorName);
					return false;
				}

				FCadImportLink Link;
				Link.Name = LinkName;
				Link.Transform = LinkTemplate.RelativeTransform;
				Link.Physics = ChildDocument.Physics;
				AppendChildVisualsToLink(LinkTemplate.Visuals, Link);
				OutModel.Links.Add(MoveTemp(Link));
			}
		}
		else
		{
			FCadImportLink RootLink;
			RootLink.Name = RootLinkName;
			RootLink.Transform = FTransform::Identity;
			RootLink.Physics = ChildDocument.Physics;
			AppendChildVisualsToLink(ChildDocument.Visuals, RootLink);
			OutModel.Links.Add(MoveTemp(RootLink));
		}

		TMap<FString, FCadImportLink> LinksByName;
		for (const FCadImportLink& Link : OutModel.Links)
		{
			if (LinksByName.Contains(Link.Name))
			{
				OutError = FString::Printf(TEXT("Child '%s' has duplicate link name '%s'."), *ChildEntry.ActorName, *Link.Name);
				return false;
			}
			LinksByName.Add(Link.Name, Link);
		}

		for (const FCadChildJointDef& JointTemplate : ChildDocument.Joints)
		{
			const FString RawParentName = JointTemplate.ParentActorName.TrimStartAndEnd();
			const FString ParentName = RawParentName.IsEmpty() ? TEXT("master") : RawParentName;
			if (ParentName.Equals(TEXT("master"), ESearchCase::IgnoreCase))
			{
				continue;
			}

			const FString ChildName = JointTemplate.ChildActorName.TrimStartAndEnd().IsEmpty()
				? RootLinkName
				: JointTemplate.ChildActorName.TrimStartAndEnd();
			if (!LinksByName.Contains(ParentName))
			{
				OutError = FString::Printf(TEXT("Child '%s' joint parent link '%s' was not found."), *ChildEntry.ActorName, *ParentName);
				return false;
			}
			if (!LinksByName.Contains(ChildName))
			{
				OutError = FString::Printf(TEXT("Child '%s' joint child link '%s' was not found."), *ChildEntry.ActorName, *ChildName);
				return false;
			}

			FCadImportJoint Joint;
			Joint.Name = JointTemplate.JointName.TrimStartAndEnd().IsEmpty()
				? FString::Printf(TEXT("%s_to_%s"), *ParentName, *ChildName)
				: JointTemplate.JointName;
			Joint.Parent = ParentName;
			Joint.Child = ChildName;
			Joint.ComponentName1 = ParentName;
			Joint.ComponentName2 = ChildName;
			Joint.Type = JointTemplate.JointType;
			Joint.Axis = JointTemplate.Axis.GetSafeNormal();
			if (Joint.Axis.IsNearlyZero())
			{
				Joint.Axis = FVector::UpVector;
			}
			Joint.Limit = JointTemplate.Limit;

			if (const FCadImportLink* ChildLink = LinksByName.Find(ChildName))
			{
				Joint.Transform = ChildLink->Transform;
			}
			else
			{
				Joint.Transform = FTransform::Identity;
			}

			if (ChildDocument.ActorType == ECadMasterChildActorType::Movable)
			{
				Joint.Drive.bHasDrive = true;
				Joint.Drive.bEnabled = true;
				Joint.Drive.Mode = ECadImportJointDriveMode::Position;
			}

			OutModel.Joints.Add(MoveTemp(Joint));
		}

		return true;
	}
}
