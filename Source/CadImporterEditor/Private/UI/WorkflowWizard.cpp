#include "UI/WorkflowWizard.h"

#include "CadImporterEditorUserSettings.h"
#include "ChildDocParser.h"
#include "ImportService.h"
#include "CadImporterEditor.h"
#include "DesktopPlatformModule.h"
#include "Editor/ActorHierarchyUtils.h"
#include "Editor.h"
#include "Components/SceneComponent.h"
#include "Engine/Selection.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformFileManager.h"
#include "IDesktopPlatform.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "DrawDebugHelpers.h"
#include "Workflow/WorkspaceUtils.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	bool IsMovableChildActorType(const ECadMasterChildActorType ActorType)
	{
		return ActorType == ECadMasterChildActorType::Movable;
	}

	FString JointTypeToPreviewString(const ECadImportJointType JointType)
	{
		switch (JointType)
		{
		case ECadImportJointType::Revolute:
			return TEXT("revolute");
		case ECadImportJointType::Prismatic:
			return TEXT("prismatic");
		case ECadImportJointType::Fixed:
		default:
			return TEXT("fixed");
		}
	}

	FString JointLimitToPreviewString(const FCadImportJointLimit& Limit)
	{
		if (!Limit.bHasLimit)
		{
			return TEXT("none");
		}

		return FString::Printf(
			TEXT("lower=%.3f upper=%.3f effort=%.3f velocity=%.3f"),
			Limit.Lower,
			Limit.Upper,
			Limit.Effort,
			Limit.Velocity);
	}

	FString MasterChildActorTypeToUiString(const ECadMasterChildActorType ActorType)
	{
		switch (ActorType)
		{
		case ECadMasterChildActorType::None:
			return TEXT("none");
		case ECadMasterChildActorType::Movable:
			return TEXT("movable");
		case ECadMasterChildActorType::Static:
		default:
			return TEXT("static");
		}
	}

	bool TryParseMasterChildActorTypeFromUiString(const FString& RawType, ECadMasterChildActorType& OutType)
	{
		if (RawType.Equals(TEXT("none"), ESearchCase::IgnoreCase))
		{
			OutType = ECadMasterChildActorType::None;
			return true;
		}

		if (RawType.Equals(TEXT("movable"), ESearchCase::IgnoreCase))
		{
			OutType = ECadMasterChildActorType::Movable;
			return true;
		}

		if (RawType.Equals(TEXT("static"), ESearchCase::IgnoreCase))
		{
			OutType = ECadMasterChildActorType::Static;
			return true;
		}

		return false;
	}

	bool TryParseJointTypeFromUiString(const FString& RawType, ECadImportJointType& OutType)
	{
		if (RawType.Equals(TEXT("revolute"), ESearchCase::IgnoreCase))
		{
			OutType = ECadImportJointType::Revolute;
			return true;
		}

		if (RawType.Equals(TEXT("prismatic"), ESearchCase::IgnoreCase))
		{
			OutType = ECadImportJointType::Prismatic;
			return true;
		}

		if (RawType.Equals(TEXT("fixed"), ESearchCase::IgnoreCase))
		{
			OutType = ECadImportJointType::Fixed;
			return true;
		}

		return false;
	}

	bool JointTypeUsesAxis(const ECadImportJointType JointType)
	{
		return JointType == ECadImportJointType::Revolute || JointType == ECadImportJointType::Prismatic;
	}

	FString BuildAutoJointName(const FCadChildJointDef& JointDef)
	{
		const FString ParentName = JointDef.ParentActorName.TrimStartAndEnd();
		const FString ChildName = JointDef.ChildActorName.TrimStartAndEnd();
		if (ChildName.IsEmpty())
		{
			return TEXT("joint");
		}

		return ParentName.IsEmpty()
			? FString::Printf(TEXT("World_to_%s"), *ChildName)
			: FString::Printf(TEXT("%s_to_%s"), *ParentName, *ChildName);
	}

	FColor JointTypeToPreviewColor(const UCadImporterEditorUserSettings* Settings, const ECadImportJointType JointType)
	{
		if (!Settings)
		{
			switch (JointType)
			{
			case ECadImportJointType::Revolute:
				return FColor(255, 170, 0);
			case ECadImportJointType::Prismatic:
				return FColor(0, 200, 255);
			case ECadImportJointType::Fixed:
			default:
				return FColor(160, 160, 160);
			}
		}

		switch (JointType)
		{
		case ECadImportJointType::Revolute:
			return Settings->RevoluteJointPreviewColor.ToFColor(true);
		case ECadImportJointType::Prismatic:
			return Settings->PrismaticJointPreviewColor.ToFColor(true);
		case ECadImportJointType::Fixed:
		default:
			return Settings->FixedJointPreviewColor.ToFColor(true);
		}
	}

	void CollectNonStaticDescendantActorLocations(
		AActor* CurrentActor,
		AActor* RootActor,
		TMap<FString, FVector>& OutLocations)
	{
		if (!CurrentActor)
		{
			return;
		}

		if (CurrentActor != RootActor)
		{
			OutLocations.Add(CurrentActor->GetActorNameOrLabel(), CurrentActor->GetActorLocation());
		}

		TArray<AActor*> Children;
		CadActorHierarchyUtils::GetSortedAttachedChildren(CurrentActor, Children, false);
		for (AActor* ChildActor : Children)
		{
			if (!ChildActor || ChildActor->IsA<AStaticMeshActor>())
			{
				continue;
			}

			CollectNonStaticDescendantActorLocations(ChildActor, RootActor, OutLocations);
		}
	}

	AActor* FindNonStaticDescendantActorByName(AActor* RootActor, const FString& ActorName)
	{
		if (!RootActor || ActorName.TrimStartAndEnd().IsEmpty())
		{
			return nullptr;
		}

		TArray<AActor*> Descendants;
		CadActorHierarchyUtils::GetSortedAttachedChildren(RootActor, Descendants, true);
		for (AActor* DescendantActor : Descendants)
		{
			if (!DescendantActor || DescendantActor->IsA<AStaticMeshActor>())
			{
				continue;
			}

			if (DescendantActor->GetActorNameOrLabel().Equals(ActorName, ESearchCase::IgnoreCase))
			{
				return DescendantActor;
			}
		}

		return nullptr;
	}

	void BuildAxisBasis(const FVector& AxisDirection, FVector& OutAxisX, FVector& OutAxisY)
	{
		const FVector SafeAxis = AxisDirection.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		const FVector Reference = FMath::Abs(SafeAxis.Z) < 0.99f ? FVector::UpVector : FVector::ForwardVector;
		OutAxisX = FVector::CrossProduct(SafeAxis, Reference).GetSafeNormal();
		if (OutAxisX.IsNearlyZero())
		{
			OutAxisX = FVector::RightVector;
		}
		OutAxisY = FVector::CrossProduct(SafeAxis, OutAxisX).GetSafeNormal();
		if (OutAxisY.IsNearlyZero())
		{
			OutAxisY = FVector::ForwardVector;
		}
	}

	void DrawDebugArc(
		UWorld* World,
		const FVector& Center,
		const FVector& AxisDirection,
		const float Radius,
		const float StartAngleDegrees,
		const float EndAngleDegrees,
		const FColor& Color,
		const bool bPersistent,
		const float LifeTime,
		const uint8 DepthPriority,
		const float Thickness)
	{
		if (!World || Radius <= UE_SMALL_NUMBER)
		{
			return;
		}

		FVector BasisX = FVector::RightVector;
		FVector BasisY = FVector::ForwardVector;
		BuildAxisBasis(AxisDirection, BasisX, BasisY);

		const float AngleSpanDegrees = EndAngleDegrees - StartAngleDegrees;
		const int32 SegmentCount = FMath::Max(12, FMath::CeilToInt(FMath::Abs(AngleSpanDegrees) / 12.0f));
		FVector PreviousPoint = Center
			+ (BasisX * Radius * FMath::Cos(FMath::DegreesToRadians(StartAngleDegrees)))
			+ (BasisY * Radius * FMath::Sin(FMath::DegreesToRadians(StartAngleDegrees)));

		for (int32 SegmentIndex = 1; SegmentIndex <= SegmentCount; ++SegmentIndex)
		{
			const float Alpha = static_cast<float>(SegmentIndex) / static_cast<float>(SegmentCount);
			const float AngleDegrees = FMath::Lerp(StartAngleDegrees, EndAngleDegrees, Alpha);
			const float AngleRadians = FMath::DegreesToRadians(AngleDegrees);
			const FVector CurrentPoint = Center
				+ (BasisX * Radius * FMath::Cos(AngleRadians))
				+ (BasisY * Radius * FMath::Sin(AngleRadians));
			DrawDebugLine(World, PreviousPoint, CurrentPoint, Color, bPersistent, LifeTime, DepthPriority, Thickness);
			PreviousPoint = CurrentPoint;
		}
	}
}

void SCadWorkflowWizard::Construct(const FArguments& InArgs)
{
	Runner = InArgs._Runner;
	WorkspaceFolder = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
	StatusMessage = TEXT("Step 1: Set workspace folder.");
	SelectionPreviewText = TEXT("선택된 액터 없음");
	MovableJointPreviewText = TEXT("No movable child actors are ready for joint setup preview yet.");
	DryRunPreviewFolderPath.Reset();
	StepIndex = 0;
	ChildTypeItems.Reset();
	ChildTypeItems.Add(MakeShared<FString>(TEXT("static")));
	ChildTypeItems.Add(MakeShared<FString>(TEXT("movable")));
	ChildTypeItems.Add(MakeShared<FString>(TEXT("none")));
	JointTypeItems.Reset();
	JointTypeItems.Add(MakeShared<FString>(TEXT("fixed")));
	JointTypeItems.Add(MakeShared<FString>(TEXT("revolute")));
	JointTypeItems.Add(MakeShared<FString>(TEXT("prismatic")));
	ImportOptions = FCadFbxImportOptions();
	ImportOptions.bShowDialog = false;
	SelectionKey.Reset();
	LastBuildReplaceResult = FCadLevelReplaceResult();
	bCanRevertLastBuild = false;
	RegisterActiveTimer(1.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SCadWorkflowWizard::PollSelection));
	RegisterActiveTimer(1.0f / 30.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SCadWorkflowWizard::UpdateJointTestAnimation));

	ChildSlot
	[
		SNew(SBorder)
		.Padding(12.0f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 10.0f)
			[
				SNew(STextBlock)
				.Text_Lambda([this]()
				{
					static const TCHAR* Labels[] =
					{
						TEXT("1/5 Workspace"),
						TEXT("2/5 Confirm Master Actor"),
						TEXT("3/5 Child Type"),
						TEXT("4/5 Joint Setup + Generate JSON"),
						TEXT("5/5 Build Actor"),
						TEXT("Completed")
					};
					const int32 SafeIndex = FMath::Clamp(StepIndex, 0, 5);
					return FText::FromString(FString::Printf(TEXT("Master Workflow Wizard - %s"), Labels[SafeIndex]));
				})
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SWidgetSwitcher)
				.WidgetIndex_Lambda([this]()
				{
					return StepIndex;
				})

				+ SWidgetSwitcher::Slot()
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("Set the workspace folder where master/child json files will be created.")))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						SAssignNew(WorkspaceTextBox, SEditableTextBox)
						.Text_Lambda([this]()
						{
							return FText::FromString(WorkspaceFolder);
						})
						.OnTextChanged_Lambda([this](const FText& NewText)
						{
							WorkspaceFolder = NewText.ToString();
						})
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(SButton)
							.Text(FText::FromString(TEXT("Browse Workspace")))
							.OnClicked(this, &SCadWorkflowWizard::HandleBrowseWorkspace)
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SButton)
							.Text(FText::FromString(TEXT("Apply Workspace")))
							.OnClicked(this, &SCadWorkflowWizard::HandleApplyWorkspace)
						]
					]
				]

				+ SWidgetSwitcher::Slot()
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("Select one actor that will become the master in the level, then confirm it.")))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("선택 액터 / 직계 자손 (1초 간격 자동 갱신, 선택 변경시에만 자손 재검색)")))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						SNew(SBox)
						.MinDesiredHeight(120.0f)
						[
							SNew(SMultiLineEditableTextBox)
							.IsReadOnly(true)
							.Text_Lambda([this]()
							{
								return FText::FromString(SelectionPreviewText);
							})
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 12.0f)
					[
						SNew(STextBlock)
						.Text_Lambda([this]()
						{
							return FText::FromString(FString::Printf(TEXT("Workspace: %s"), *WorkspaceFolder));
						})
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(SButton)
							.Text(FText::FromString(TEXT("Back")))
							.OnClicked(this, &SCadWorkflowWizard::HandleBack)
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SButton)
							.Text(FText::FromString(TEXT("Master Actor Confirm")))
							.OnClicked(this, &SCadWorkflowWizard::ConfirmMaster)
						]
					]
				]

				+ SWidgetSwitcher::Slot()
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("Set child actor types in UI, then proceed to joint setup. Choose 'none' to exclude a child.")))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 12.0f)
					[
						SNew(STextBlock)
						.Text_Lambda([this]()
						{
							const AActor* MasterActor = ConfirmedSelection.MasterActor.Get();
							const FString MasterName = MasterActor ? MasterActor->GetActorNameOrLabel() : TEXT("(not confirmed)");
							return FText::FromString(FString::Printf(TEXT("Confirmed Master: %s"), *MasterName));
						})
					]

					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					.Padding(0.0f, 0.0f, 0.0f, 12.0f)
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()
						[
							SAssignNew(ChildTypeRowsBox, SVerticalBox)
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(SButton)
							.Text(FText::FromString(TEXT("Back")))
							.OnClicked(this, &SCadWorkflowWizard::HandleBack)
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SButton)
							.Text(FText::FromString(TEXT("Next: Joint Setup")))
							.OnClicked(this, &SCadWorkflowWizard::ProceedToJointSetup)
						]
					]
				]

				+ SWidgetSwitcher::Slot()
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("Edit the joint definitions read back from temporary dry-run child JSON files for movable child actors. Joint names are auto-generated from parent/child link selections.")))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 12.0f)
					[
						SNew(STextBlock)
						.Text_Lambda([this]()
						{
							return FText::FromString(FString::Printf(TEXT("Workspace: %s"), *WorkspaceFolder));
						})
					]

					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					.Padding(0.0f, 0.0f, 0.0f, 12.0f)
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()
						[
							SAssignNew(JointEditorRowsBox, SVerticalBox)
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(SButton)
							.Text(FText::FromString(TEXT("Back")))
							.OnClicked(this, &SCadWorkflowWizard::HandleBack)
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SButton)
							.Text(FText::FromString(TEXT("Continue")))
							.OnClicked(this, &SCadWorkflowWizard::ContinueFromJointSetupPreview)
						]
					]
				]

				+ SWidgetSwitcher::Slot()
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("Build blueprint from master/child json and replace actors in level.")))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 12.0f)
					[
						SNew(STextBlock)
						.Text_Lambda([this]()
						{
							return FText::FromString(FString::Printf(TEXT("Child Folder: %s"), *BuildInput.ChildJsonFolderPath));
						})
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(SButton)
							.Text(FText::FromString(TEXT("Back")))
							.OnClicked(this, &SCadWorkflowWizard::HandleBack)
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SButton)
							.Text(FText::FromString(TEXT("Build Actor")))
							.OnClicked(this, &SCadWorkflowWizard::BuildAssembly)
						]
					]
				]

				+ SWidgetSwitcher::Slot()
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("Master workflow completed.")))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(SButton)
							.IsEnabled_Lambda([this]()
							{
								return bCanRevertLastBuild;
							})
							.Text(FText::FromString(TEXT("Revert")))
							.OnClicked(this, &SCadWorkflowWizard::RevertLastBuild)
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SButton)
							.Text(FText::FromString(TEXT("Restart Workflow")))
							.OnClicked(this, &SCadWorkflowWizard::RestartWorkflow)
						]
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 10.0f, 0.0f, 0.0f)
			[
				SNew(SBox)
				.MinDesiredHeight(120.0f)
				[
					SNew(SMultiLineEditableTextBox)
					.IsReadOnly(true)
					.Text_Lambda([this]()
					{
						return FText::FromString(StatusMessage);
					})
				]
			]
		]
	];

	RebuildChildRows();
	ResetJointEditorState();
}

EActiveTimerReturnType SCadWorkflowWizard::PollSelection(double CurrentTime, float DeltaTime)
{
	static_cast<void>(CurrentTime);
	static_cast<void>(DeltaTime);
	RefreshSelectionPreview();
	return EActiveTimerReturnType::Continue;
}

void SCadWorkflowWizard::RefreshSelectionPreview()
{
	FString CurrentSignature;
	AActor* SingleSelectedActor = nullptr;
	int32 SelectedActorCount = 0;

	if (!GEditor)
	{
		CurrentSignature = TEXT("selection:no_editor");
	}
	else
	{
		USelection* SelectedActors = GEditor->GetSelectedActors();
		if (!SelectedActors)
		{
			CurrentSignature = TEXT("selection:unavailable");
		}
		else
		{
			TArray<AActor*> ActorSelection;
			for (FSelectionIterator It(*SelectedActors); It; ++It)
			{
				if (AActor* SelectedActor = Cast<AActor>(*It))
				{
					ActorSelection.Add(SelectedActor);
				}
			}

			SelectedActorCount = ActorSelection.Num();
			if (SelectedActorCount == 1)
			{
				SingleSelectedActor = ActorSelection[0];
				CurrentSignature = FString::Printf(TEXT("selection:single:%s"), *SingleSelectedActor->GetPathName());
			}
			else if (SelectedActorCount == 0)
			{
				CurrentSignature = TEXT("selection:none");
			}
			else
			{
				ActorSelection.Sort([](const AActor& Left, const AActor& Right)
				{
					return Left.GetActorNameOrLabel() < Right.GetActorNameOrLabel();
				});

				const FString FirstActorPath = ActorSelection[0] ? ActorSelection[0]->GetPathName() : TEXT("(none)");
				CurrentSignature = FString::Printf(TEXT("selection:multi:%d:%s"), SelectedActorCount, *FirstActorPath);
			}
		}
	}

	if (CurrentSignature == SelectionKey)
	{
		// Skip descendant lookup when selected actor has not changed.
		return;
	}

	SelectionKey = CurrentSignature;

	if (!GEditor)
	{
		SelectionPreviewText = TEXT("에디터 컨텍스트를 찾을 수 없습니다.");
		return;
	}

	if (SelectedActorCount == 0 || !SingleSelectedActor)
	{
		if (SelectedActorCount > 1)
		{
			SelectionPreviewText = FString::Printf(TEXT("여러 액터가 선택됨 (%d)\n단일 액터를 선택해 주세요."), SelectedActorCount);
		}
		else
		{
			SelectionPreviewText = TEXT("선택된 액터 없음");
		}
		return;
	}

	TArray<AActor*> DirectChildren;
	CadActorHierarchyUtils::GetSortedAttachedChildren(SingleSelectedActor, DirectChildren, false);

	FString Result = FString::Printf(
		TEXT("Selected: %s\nPath: %s\nDirect Children: %d"),
		*SingleSelectedActor->GetActorNameOrLabel(),
		*SingleSelectedActor->GetPathName(),
		DirectChildren.Num());

	for (AActor* ChildActor : DirectChildren)
	{
		if (!ChildActor)
		{
			continue;
		}

		Result += FString::Printf(TEXT("\n- %s"), *ChildActor->GetActorNameOrLabel());
	}

	SelectionPreviewText = MoveTemp(Result);
}

void SCadWorkflowWizard::ResetJointEditorState()
{
	StopJointTestAnimation(true);
	EditableJointChildren.Reset();
	MovableJointPreviewText = TEXT("No movable child actors are ready for joint setup preview yet.");
	ClearJointPreviewLines();
	RebuildJointEditorRows();
}

EActiveTimerReturnType SCadWorkflowWizard::UpdateJointTestAnimation(double CurrentTime, float DeltaTime)
{
	if (!JointTestAnimation.IsSet())
	{
		return EActiveTimerReturnType::Continue;
	}

	FJointTestAnimationState& AnimationState = JointTestAnimation.GetValue();
	AActor* TargetActor = AnimationState.TargetActor.Get();
	USceneComponent* TargetRootComponent = TargetActor ? TargetActor->GetRootComponent() : nullptr;
	if (!TargetRootComponent)
	{
		JointTestAnimation.Reset();
		return EActiveTimerReturnType::Continue;
	}

	const float SegmentDurationSeconds = 0.45f;
	const float TotalDurationSeconds = SegmentDurationSeconds * 3.0f;
	AnimationState.ElapsedSeconds = FMath::Min(AnimationState.ElapsedSeconds + DeltaTime, TotalDurationSeconds);

	const auto EvaluateAnimatedValue = [SegmentDurationSeconds](const float ElapsedSeconds, const float Lower, const float Upper)
	{
		const auto EaseAlpha = [](const float Alpha)
		{
			return FMath::InterpEaseInOut(0.0f, 1.0f, FMath::Clamp(Alpha, 0.0f, 1.0f), 2.0f);
		};

		if (ElapsedSeconds <= SegmentDurationSeconds)
		{
			return FMath::Lerp(0.0f, Lower, EaseAlpha(ElapsedSeconds / SegmentDurationSeconds));
		}

		if (ElapsedSeconds <= SegmentDurationSeconds * 2.0f)
		{
			return FMath::Lerp(Lower, Upper, EaseAlpha((ElapsedSeconds - SegmentDurationSeconds) / SegmentDurationSeconds));
		}

		return FMath::Lerp(Upper, 0.0f, EaseAlpha((ElapsedSeconds - (SegmentDurationSeconds * 2.0f)) / SegmentDurationSeconds));
	};

	const FVector LocalAxis = AnimationState.LocalAxis.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	const float AnimatedValue = EvaluateAnimatedValue(AnimationState.ElapsedSeconds, AnimationState.Lower, AnimationState.Upper);
	FTransform AnimatedRelativeTransform = AnimationState.OriginalRelativeTransform;

	if (AnimationState.JointType == ECadImportJointType::Prismatic)
	{
		const FVector LocalOffset = LocalAxis * AnimatedValue;
		const FVector RotatedOffset = AnimationState.OriginalRelativeTransform.GetRotation().RotateVector(LocalOffset);
		AnimatedRelativeTransform.SetLocation(AnimationState.OriginalRelativeTransform.GetLocation() + RotatedOffset);
	}
	else if (AnimationState.JointType == ECadImportJointType::Revolute)
	{
		const FQuat DeltaRotation(LocalAxis, FMath::DegreesToRadians(AnimatedValue));
		AnimatedRelativeTransform.SetRotation((AnimationState.OriginalRelativeTransform.GetRotation() * DeltaRotation).GetNormalized());
	}

	TargetRootComponent->SetRelativeTransform(AnimatedRelativeTransform);
	RedrawJointPreviewLines();
	if (GEditor)
	{
		GEditor->RedrawAllViewports(false);
	}

	if (AnimationState.ElapsedSeconds >= TotalDurationSeconds - KINDA_SMALL_NUMBER)
	{
		StopJointTestAnimation(true);
	}

	return EActiveTimerReturnType::Continue;
}

void SCadWorkflowWizard::LoadEditableJointDocuments(
	const FCadMasterSelection& SelectionForGeneration,
	const TArray<FCadChildDoc>& InChildDocuments)
{
	if (InChildDocuments.Num() == 0)
	{
		MovableJointPreviewText = FString::Printf(
			TEXT("Dry-run folder: %s\nNo movable child JSON documents were produced by the dry-run preview.\nNo joint definitions were read back from temporary files."),
			DryRunPreviewFolderPath.IsEmpty() ? TEXT("(not available)") : *DryRunPreviewFolderPath);
		EditableJointChildren.Reset();
		ClearJointPreviewLines();
		RebuildJointEditorRows();
		return;
	}

	EditableJointChildren.Reset();
	for (const FCadChildDoc& InputDocument : InChildDocuments)
	{
		FEditableJointChildState ChildState;
		ChildState.ChildDocument = InputDocument;
		for (FCadChildJointDef& JointDef : ChildState.ChildDocument.Joints)
		{
			JointDef.JointName = BuildAutoJointName(JointDef);
			ChildState.JointDebugDrawEnabled.Add(false);
		}

		for (const FCadChildEntry& ChildEntry : SelectionForGeneration.Children)
		{
			if (ChildEntry.ActorName == ChildState.ChildDocument.ChildActorName)
			{
				ChildState.ChildEntry = ChildEntry;
				break;
			}
		}

		TSet<FString> UniqueLinkNames;
		for (const FCadChildLinkDef& LinkDef : ChildState.ChildDocument.Links)
		{
			const FString LinkName = LinkDef.LinkName.TrimStartAndEnd();
			if (!LinkName.IsEmpty())
			{
				UniqueLinkNames.Add(LinkName);
			}
		}

		TArray<FString> SortedLinkNames = UniqueLinkNames.Array();
		SortedLinkNames.Sort();
		ChildState.ParentLinkItems.Add(MakeShared<FString>(TEXT("World")));
		for (const FString& LinkName : SortedLinkNames)
		{
			ChildState.ParentLinkItems.Add(MakeShared<FString>(LinkName));
			ChildState.ChildLinkItems.Add(MakeShared<FString>(LinkName));
		}

		EditableJointChildren.Add(MoveTemp(ChildState));
	}

	MovableJointPreviewText = FString::Printf(
		TEXT("Dry-run folder: %s\nMovable child JSON documents parsed: %d"),
		DryRunPreviewFolderPath.IsEmpty() ? TEXT("(not available)") : *DryRunPreviewFolderPath,
		EditableJointChildren.Num());
	RebuildJointEditorRows();
	RedrawJointPreviewLines();
}

void SCadWorkflowWizard::RebuildJointEditorRows()
{
	if (!JointEditorRowsBox.IsValid())
	{
		return;
	}

	JointEditorRowsBox->ClearChildren();

	JointEditorRowsBox->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SNew(STextBlock)
		.Text_Lambda([this]()
		{
			return FText::FromString(MovableJointPreviewText);
		})
	];

	if (EditableJointChildren.Num() == 0)
	{
		return;
	}

	for (int32 ChildDocIndex = 0; ChildDocIndex < EditableJointChildren.Num(); ++ChildDocIndex)
	{
		JointEditorRowsBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 12.0f)
		[
			SNew(SBorder)
			.Padding(8.0f)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(STextBlock)
					.Text_Lambda([this, ChildDocIndex]()
					{
						if (!EditableJointChildren.IsValidIndex(ChildDocIndex))
						{
							return FText::FromString(TEXT("(invalid child)"));
						}
						return FText::FromString(FString::Printf(TEXT("[Movable] %s"), *EditableJointChildren[ChildDocIndex].ChildDocument.ChildActorName));
					})
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(STextBlock)
					.Text_Lambda([this, ChildDocIndex]()
					{
						if (!EditableJointChildren.IsValidIndex(ChildDocIndex))
						{
							return FText::GetEmpty();
						}
						const FEditableJointChildState& ChildState = EditableJointChildren[ChildDocIndex];
						return FText::FromString(FString::Printf(
							TEXT("Source Path: %s | Available Links: %d | Joint Count: %d"),
							*ChildState.ChildDocument.SourceActorPath,
							ChildState.ChildLinkItems.Num(),
							ChildState.ChildDocument.Joints.Num()));
					})
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 8.0f)
				[
					SNew(STextBlock)
					.Text_Lambda([this, ChildDocIndex]()
					{
						if (!EditableJointChildren.IsValidIndex(ChildDocIndex))
						{
							return FText::GetEmpty();
						}

						TArray<FString> LinkNames;
						for (const TSharedPtr<FString>& LinkItem : EditableJointChildren[ChildDocIndex].ChildLinkItems)
						{
							if (LinkItem.IsValid())
							{
								LinkNames.Add(*LinkItem);
							}
						}
						return FText::FromString(FString::Printf(TEXT("Links: %s"), *FString::Join(LinkNames, TEXT(", "))));
					})
				]
			]
		];

		if (EditableJointChildren[ChildDocIndex].ChildDocument.Joints.Num() == 0)
		{
			JointEditorRowsBox->AddSlot()
			.AutoHeight()
			.Padding(12.0f, 0.0f, 0.0f, 12.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("No joints found in this child JSON.")))
			];
			continue;
		}

		for (int32 JointIndex = 0; JointIndex < EditableJointChildren[ChildDocIndex].ChildDocument.Joints.Num(); ++JointIndex)
		{
			JointEditorRowsBox->AddSlot()
			.AutoHeight()
			.Padding(12.0f, 0.0f, 0.0f, 10.0f)
			[
				SNew(SBorder)
				.Padding(8.0f)
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 6.0f)
					[
						SNew(STextBlock)
						.Text_Lambda([this, ChildDocIndex, JointIndex]()
						{
							if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
								!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
							{
								return FText::FromString(TEXT("Joint"));
							}

							return FText::FromString(FString::Printf(
								TEXT("Joint %d | Name: %s"),
								JointIndex + 1,
								*EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].JointName));
						})
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 6.0f)
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(SButton)
							.Text_Lambda([this, ChildDocIndex, JointIndex]()
							{
								const bool bIsEnabled = EditableJointChildren.IsValidIndex(ChildDocIndex)
									&& EditableJointChildren[ChildDocIndex].JointDebugDrawEnabled.IsValidIndex(JointIndex)
									&& EditableJointChildren[ChildDocIndex].JointDebugDrawEnabled[JointIndex];
								return FText::FromString(bIsEnabled ? TEXT("Hide Debug Draw") : TEXT("Show Debug Draw"));
							})
							.OnClicked_Lambda([this, ChildDocIndex, JointIndex]()
							{
								const bool bCurrent = EditableJointChildren.IsValidIndex(ChildDocIndex)
									&& EditableJointChildren[ChildDocIndex].JointDebugDrawEnabled.IsValidIndex(JointIndex)
									&& EditableJointChildren[ChildDocIndex].JointDebugDrawEnabled[JointIndex];
								SetJointDebugDrawEnabled(ChildDocIndex, JointIndex, !bCurrent);
								return FReply::Handled();
							})
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SButton)
							.IsEnabled_Lambda([this, ChildDocIndex, JointIndex]()
							{
								return EditableJointChildren.IsValidIndex(ChildDocIndex) &&
									EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex) &&
									JointTypeUsesAxis(EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].JointType);
							})
							.Text(FText::FromString(TEXT("Test")))
							.OnClicked_Lambda([this, ChildDocIndex, JointIndex]()
							{
								RunJointLimitTest(ChildDocIndex, JointIndex);
								return FReply::Handled();
							})
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 6.0f)
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(SVerticalBox)

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("Parent Link")))
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SComboBox<TSharedPtr<FString>>)
								.OptionsSource(&EditableJointChildren[ChildDocIndex].ParentLinkItems)
								.OnGenerateWidget_Lambda([](const TSharedPtr<FString> Item)
								{
									return SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? *Item : TEXT("")));
								})
								.OnSelectionChanged_Lambda([this, ChildDocIndex, JointIndex](const TSharedPtr<FString> SelectedItem, ESelectInfo::Type)
								{
									if (SelectedItem.IsValid())
									{
										SetJointParentLink(ChildDocIndex, JointIndex, *SelectedItem);
									}
								})
								[
									SNew(STextBlock)
									.Text_Lambda([this, ChildDocIndex, JointIndex]()
									{
										if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
											!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
										{
											return FText::GetEmpty();
										}
										const FString ParentName = EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].ParentActorName.TrimStartAndEnd();
										return FText::FromString(ParentName.IsEmpty() ? TEXT("World") : ParentName);
									})
								]
							]
						]

						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(SVerticalBox)

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("Child Link")))
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SComboBox<TSharedPtr<FString>>)
								.OptionsSource(&EditableJointChildren[ChildDocIndex].ChildLinkItems)
								.OnGenerateWidget_Lambda([](const TSharedPtr<FString> Item)
								{
									return SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? *Item : TEXT("")));
								})
								.OnSelectionChanged_Lambda([this, ChildDocIndex, JointIndex](const TSharedPtr<FString> SelectedItem, ESelectInfo::Type)
								{
									if (SelectedItem.IsValid())
									{
										SetJointChildLink(ChildDocIndex, JointIndex, *SelectedItem);
									}
								})
								[
									SNew(STextBlock)
									.Text_Lambda([this, ChildDocIndex, JointIndex]()
									{
										if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
											!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
										{
											return FText::GetEmpty();
										}
										return FText::FromString(EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].ChildActorName);
									})
								]
							]
						]

						+ SHorizontalBox::Slot()
						.FillWidth(0.8f)
						[
							SNew(SVerticalBox)

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("Joint Type")))
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SComboBox<TSharedPtr<FString>>)
								.OptionsSource(&JointTypeItems)
								.OnGenerateWidget_Lambda([](const TSharedPtr<FString> Item)
								{
									return SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? *Item : TEXT("")));
								})
								.OnSelectionChanged_Lambda([this, ChildDocIndex, JointIndex](const TSharedPtr<FString> SelectedItem, ESelectInfo::Type)
								{
									if (SelectedItem.IsValid())
									{
										SetJointType(ChildDocIndex, JointIndex, *SelectedItem);
									}
								})
								[
									SNew(STextBlock)
									.Text_Lambda([this, ChildDocIndex, JointIndex]()
									{
										if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
											!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
										{
											return FText::FromString(TEXT("fixed"));
										}
										return FText::FromString(JointTypeToPreviewString(
											EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].JointType));
									})
								]
							]
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 6.0f)
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 18.0f, 12.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Axis")))
						]

						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(SVerticalBox)

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("X")))
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SNumericEntryBox<float>)
								.AllowSpin(false)
								.IsEnabled_Lambda([this, ChildDocIndex, JointIndex]()
								{
									return EditableJointChildren.IsValidIndex(ChildDocIndex) &&
										EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex) &&
										JointTypeUsesAxis(EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].JointType);
								})
								.Value_Lambda([this, ChildDocIndex, JointIndex]() -> TOptional<float>
								{
									if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
										!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
									{
										return TOptional<float>();
									}
									return EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].Axis.X;
								})
								.OnValueCommitted_Lambda([this, ChildDocIndex, JointIndex](const float NewValue, ETextCommit::Type)
								{
									SetJointAxisX(ChildDocIndex, JointIndex, NewValue);
								})
							]
						]

						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(SVerticalBox)

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("Y")))
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SNumericEntryBox<float>)
								.AllowSpin(false)
								.IsEnabled_Lambda([this, ChildDocIndex, JointIndex]()
								{
									return EditableJointChildren.IsValidIndex(ChildDocIndex) &&
										EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex) &&
										JointTypeUsesAxis(EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].JointType);
								})
								.Value_Lambda([this, ChildDocIndex, JointIndex]() -> TOptional<float>
								{
									if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
										!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
									{
										return TOptional<float>();
									}
									return EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].Axis.Y;
								})
								.OnValueCommitted_Lambda([this, ChildDocIndex, JointIndex](const float NewValue, ETextCommit::Type)
								{
									SetJointAxisY(ChildDocIndex, JointIndex, NewValue);
								})
							]
						]

						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						[
							SNew(SVerticalBox)

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("Z")))
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SNumericEntryBox<float>)
								.AllowSpin(false)
								.IsEnabled_Lambda([this, ChildDocIndex, JointIndex]()
								{
									return EditableJointChildren.IsValidIndex(ChildDocIndex) &&
										EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex) &&
										JointTypeUsesAxis(EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].JointType);
								})
								.Value_Lambda([this, ChildDocIndex, JointIndex]() -> TOptional<float>
								{
									if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
										!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
									{
										return TOptional<float>();
									}
									return EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].Axis.Z;
								})
								.OnValueCommitted_Lambda([this, ChildDocIndex, JointIndex](const float NewValue, ETextCommit::Type)
								{
									SetJointAxisZ(ChildDocIndex, JointIndex, NewValue);
								})
							]
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 6.0f)
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 18.0f, 12.0f, 0.0f)
						[
							SNew(SCheckBox)
							.IsEnabled_Lambda([this, ChildDocIndex, JointIndex]()
							{
								return EditableJointChildren.IsValidIndex(ChildDocIndex) &&
									EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex) &&
									JointTypeUsesAxis(EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].JointType);
							})
							.IsChecked_Lambda([this, ChildDocIndex, JointIndex]()
							{
								if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
									!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
								{
									return ECheckBoxState::Unchecked;
								}
								return EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].Limit.bHasLimit
									? ECheckBoxState::Checked
									: ECheckBoxState::Unchecked;
							})
							.OnCheckStateChanged_Lambda([this, ChildDocIndex, JointIndex](const ECheckBoxState NewState)
							{
								SetJointLimitEnabled(ChildDocIndex, JointIndex, NewState == ECheckBoxState::Checked);
							})
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("Use Limit")))
							]
						]

						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(SVerticalBox)

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("Lower")))
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SNumericEntryBox<float>)
								.AllowSpin(false)
								.IsEnabled_Lambda([this, ChildDocIndex, JointIndex]()
								{
									return EditableJointChildren.IsValidIndex(ChildDocIndex) &&
										EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex) &&
										EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].Limit.bHasLimit;
								})
								.Value_Lambda([this, ChildDocIndex, JointIndex]() -> TOptional<float>
								{
									if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
										!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
									{
										return TOptional<float>();
									}
									return EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].Limit.Lower;
								})
								.OnValueCommitted_Lambda([this, ChildDocIndex, JointIndex](const float NewValue, ETextCommit::Type)
								{
									SetJointLimitLower(ChildDocIndex, JointIndex, NewValue);
								})
							]
						]

						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(SVerticalBox)

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("Upper")))
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SNumericEntryBox<float>)
								.AllowSpin(false)
								.IsEnabled_Lambda([this, ChildDocIndex, JointIndex]()
								{
									return EditableJointChildren.IsValidIndex(ChildDocIndex) &&
										EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex) &&
										EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].Limit.bHasLimit;
								})
								.Value_Lambda([this, ChildDocIndex, JointIndex]() -> TOptional<float>
								{
									if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
										!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
									{
										return TOptional<float>();
									}
									return EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].Limit.Upper;
								})
								.OnValueCommitted_Lambda([this, ChildDocIndex, JointIndex](const float NewValue, ETextCommit::Type)
								{
									SetJointLimitUpper(ChildDocIndex, JointIndex, NewValue);
								})
							]
						]

						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(SVerticalBox)

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("Effort")))
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SNumericEntryBox<float>)
								.AllowSpin(false)
								.IsEnabled_Lambda([this, ChildDocIndex, JointIndex]()
								{
									return EditableJointChildren.IsValidIndex(ChildDocIndex) &&
										EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex) &&
										EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].Limit.bHasLimit;
								})
								.Value_Lambda([this, ChildDocIndex, JointIndex]() -> TOptional<float>
								{
									if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
										!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
									{
										return TOptional<float>();
									}
									return EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].Limit.Effort;
								})
								.OnValueCommitted_Lambda([this, ChildDocIndex, JointIndex](const float NewValue, ETextCommit::Type)
								{
									SetJointLimitEffort(ChildDocIndex, JointIndex, NewValue);
								})
							]
						]

						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						[
							SNew(SVerticalBox)

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("Velocity")))
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SNumericEntryBox<float>)
								.AllowSpin(false)
								.IsEnabled_Lambda([this, ChildDocIndex, JointIndex]()
								{
									return EditableJointChildren.IsValidIndex(ChildDocIndex) &&
										EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex) &&
										EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].Limit.bHasLimit;
								})
								.Value_Lambda([this, ChildDocIndex, JointIndex]() -> TOptional<float>
								{
									if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
										!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
									{
										return TOptional<float>();
									}
									return EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].Limit.Velocity;
								})
								.OnValueCommitted_Lambda([this, ChildDocIndex, JointIndex](const float NewValue, ETextCommit::Type)
								{
									SetJointLimitVelocity(ChildDocIndex, JointIndex, NewValue);
								})
							]
						]
					]
				]
			];
		}
	}
}

void SCadWorkflowWizard::CleanupDryRunPreviewFolder()
{
	if (DryRunPreviewFolderPath.IsEmpty())
	{
		return;
	}

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (PlatformFile.DirectoryExists(*DryRunPreviewFolderPath))
	{
		PlatformFile.DeleteDirectoryRecursively(*DryRunPreviewFolderPath);
	}

	DryRunPreviewFolderPath.Reset();
}

void SCadWorkflowWizard::SetJointParentLink(const int32 ChildDocIndex, const int32 JointIndex, const FString& ParentLinkName)
{
	if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
		!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
	{
		return;
	}

	FCadChildJointDef& JointDef = EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex];
	JointDef.ParentActorName = ParentLinkName.Equals(TEXT("World"), ESearchCase::IgnoreCase)
		? FString()
		: ParentLinkName.TrimStartAndEnd();
	JointDef.JointName = BuildAutoJointName(JointDef);
	RedrawJointPreviewLines();
}

void SCadWorkflowWizard::SetJointChildLink(const int32 ChildDocIndex, const int32 JointIndex, const FString& ChildLinkName)
{
	if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
		!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
	{
		return;
	}

	FCadChildJointDef& JointDef = EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex];
	JointDef.ChildActorName = ChildLinkName.TrimStartAndEnd();
	JointDef.JointName = BuildAutoJointName(JointDef);
	RedrawJointPreviewLines();
}

void SCadWorkflowWizard::SetJointType(const int32 ChildDocIndex, const int32 JointIndex, const FString& JointTypeName)
{
	if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
		!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
	{
		return;
	}

	ECadImportJointType ParsedType = ECadImportJointType::Fixed;
	if (!TryParseJointTypeFromUiString(JointTypeName, ParsedType))
	{
		return;
	}

	EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].JointType = ParsedType;
	RedrawJointPreviewLines();
}

void SCadWorkflowWizard::SetJointAxisX(const int32 ChildDocIndex, const int32 JointIndex, const float Value)
{
	if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
		!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
	{
		return;
	}

	EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].Axis.X = Value;
	RedrawJointPreviewLines();
}

void SCadWorkflowWizard::SetJointAxisY(const int32 ChildDocIndex, const int32 JointIndex, const float Value)
{
	if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
		!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
	{
		return;
	}

	EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].Axis.Y = Value;
	RedrawJointPreviewLines();
}

void SCadWorkflowWizard::SetJointAxisZ(const int32 ChildDocIndex, const int32 JointIndex, const float Value)
{
	if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
		!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
	{
		return;
	}

	EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].Axis.Z = Value;
	RedrawJointPreviewLines();
}

void SCadWorkflowWizard::SetJointLimitEnabled(const int32 ChildDocIndex, const int32 JointIndex, const bool bEnabled)
{
	if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
		!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
	{
		return;
	}

	EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].Limit.bHasLimit = bEnabled;
}

void SCadWorkflowWizard::SetJointLimitLower(const int32 ChildDocIndex, const int32 JointIndex, const float Value)
{
	if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
		!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
	{
		return;
	}

	EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].Limit.Lower = Value;
}

void SCadWorkflowWizard::SetJointLimitUpper(const int32 ChildDocIndex, const int32 JointIndex, const float Value)
{
	if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
		!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
	{
		return;
	}

	EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].Limit.Upper = Value;
}

void SCadWorkflowWizard::SetJointLimitEffort(const int32 ChildDocIndex, const int32 JointIndex, const float Value)
{
	if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
		!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
	{
		return;
	}

	EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].Limit.Effort = Value;
}

void SCadWorkflowWizard::SetJointLimitVelocity(const int32 ChildDocIndex, const int32 JointIndex, const float Value)
{
	if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
		!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
	{
		return;
	}

	EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].Limit.Velocity = Value;
}

bool SCadWorkflowWizard::ValidateEditedJointDefinitions(FString& OutError) const
{
	OutError.Reset();

	for (const FEditableJointChildState& ChildState : EditableJointChildren)
	{
		TSet<FString> AvailableLinks;
		for (const FCadChildLinkDef& LinkDef : ChildState.ChildDocument.Links)
		{
			const FString LinkName = LinkDef.LinkName.TrimStartAndEnd();
			if (!LinkName.IsEmpty())
			{
				AvailableLinks.Add(LinkName);
			}
		}

		for (const FCadChildJointDef& JointDef : ChildState.ChildDocument.Joints)
		{
			const FString ParentName = JointDef.ParentActorName.TrimStartAndEnd();
			const FString ChildName = JointDef.ChildActorName.TrimStartAndEnd();
			if (ChildName.IsEmpty())
			{
				OutError = FString::Printf(TEXT("Child '%s' has a joint with an empty child link."), *ChildState.ChildDocument.ChildActorName);
				return false;
			}
			if (!ParentName.IsEmpty() && ParentName == ChildName)
			{
				OutError = FString::Printf(TEXT("Child '%s' joint '%s' uses the same parent and child link."), *ChildState.ChildDocument.ChildActorName, *BuildAutoJointName(JointDef));
				return false;
			}
			if (!ParentName.IsEmpty() && !AvailableLinks.Contains(ParentName))
			{
				OutError = FString::Printf(TEXT("Child '%s' joint parent link '%s' was not found in links[]."), *ChildState.ChildDocument.ChildActorName, *ParentName);
				return false;
			}
			if (!AvailableLinks.Contains(ChildName))
			{
				OutError = FString::Printf(TEXT("Child '%s' joint child link '%s' was not found in links[]."), *ChildState.ChildDocument.ChildActorName, *ChildName);
				return false;
			}
		}
	}

	return true;
}

bool SCadWorkflowWizard::TryApplyEditedChildJsonOverrides(FString& OutError) const
{
	OutError.Reset();

	if (EditableJointChildren.Num() == 0)
	{
		return true;
	}

	const FString ChildJsonFolderPath = ChildJsonResult.ChildJsonFolderPath.TrimStartAndEnd();
	if (ChildJsonFolderPath.IsEmpty())
	{
		OutError = TEXT("Child json folder path is empty while applying edited joint overrides.");
		return false;
	}

	for (const FEditableJointChildState& ChildState : EditableJointChildren)
	{
		FString ChildFileName = ChildState.ChildEntry.ChildJsonFileName.TrimStartAndEnd();
		if (ChildFileName.IsEmpty())
		{
			const FString SafeActorName = FPaths::MakeValidFileName(ChildState.ChildEntry.ActorName);
			ChildFileName = FString::Printf(TEXT("%s.json"), SafeActorName.IsEmpty() ? TEXT("Child") : *SafeActorName);
		}

		const FString OutputPath = FPaths::Combine(ChildJsonFolderPath, ChildFileName);
		FString SerializedChildJson;
		if (!CadChildDocExporter::TrySerializeChildDocument(ChildState.ChildDocument, SerializedChildJson, OutError))
		{
			OutError = FString::Printf(TEXT("Failed to serialize edited child json for '%s': %s"), *ChildState.ChildDocument.ChildActorName, *OutError);
			return false;
		}
		if (!FFileHelper::SaveStringToFile(SerializedChildJson, *OutputPath))
		{
			OutError = FString::Printf(TEXT("Failed to write edited child json file: %s"), *OutputPath);
			return false;
		}
	}

	return true;
}

void SCadWorkflowWizard::ClearJointPreviewLines()
{
	const AActor* MasterActor = ConfirmedSelection.MasterActor.Get();
	UWorld* World = MasterActor ? MasterActor->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}

	FlushPersistentDebugLines(World);
	if (GEditor)
	{
		GEditor->RedrawAllViewports(false);
	}
}

void SCadWorkflowWizard::SetJointDebugDrawEnabled(const int32 ChildDocIndex, const int32 JointIndex, const bool bEnabled)
{
	if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
		!EditableJointChildren[ChildDocIndex].JointDebugDrawEnabled.IsValidIndex(JointIndex))
	{
		return;
	}

	EditableJointChildren[ChildDocIndex].JointDebugDrawEnabled[JointIndex] = bEnabled;
	RedrawJointPreviewLines();
}

void SCadWorkflowWizard::RunJointLimitTest(const int32 ChildDocIndex, const int32 JointIndex)
{
	if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
		!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
	{
		return;
	}

	const FEditableJointChildState& ChildState = EditableJointChildren[ChildDocIndex];
	const FCadChildJointDef& JointDef = ChildState.ChildDocument.Joints[JointIndex];
	if (!JointTypeUsesAxis(JointDef.JointType))
	{
		return;
	}

	AActor* ChildRootActor = CadActorHierarchyUtils::FindByPath(ChildState.ChildEntry.ActorPath);
	if (!ChildRootActor)
	{
		return;
	}

	const FString ChildName = JointDef.ChildActorName.TrimStartAndEnd();
	AActor* TargetActor = FindNonStaticDescendantActorByName(ChildRootActor, ChildName);
	if (!TargetActor)
	{
		return;
	}

	const float PreviewLength = 40.0f;
	const float PreviewAngle = 180.0f;

	StopJointTestAnimation(true);

	FJointTestAnimationState AnimationState;
	AnimationState.TargetActor = TargetActor;
	USceneComponent* TargetRootComponent = TargetActor->GetRootComponent();
	if (!TargetRootComponent)
	{
		return;
	}

	AnimationState.OriginalRelativeTransform = TargetRootComponent->GetRelativeTransform();
	AnimationState.LocalAxis = JointDef.Axis.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	AnimationState.JointType = JointDef.JointType;
	AnimationState.ElapsedSeconds = 0.0f;

	if (JointDef.Limit.bHasLimit)
	{
		AnimationState.Lower = JointDef.Limit.Lower;
		AnimationState.Upper = JointDef.Limit.Upper;
	}
	else if (JointDef.JointType == ECadImportJointType::Prismatic)
	{
		AnimationState.Lower = -PreviewLength;
		AnimationState.Upper = PreviewLength;
	}
	else
	{
		AnimationState.Lower = -PreviewAngle;
		AnimationState.Upper = PreviewAngle;
	}

	JointTestAnimation = AnimationState;
}

void SCadWorkflowWizard::StopJointTestAnimation(const bool bRestoreTransform)
{
	if (!JointTestAnimation.IsSet())
	{
		return;
	}

	const FJointTestAnimationState AnimationState = JointTestAnimation.GetValue();
	JointTestAnimation.Reset();

	if (bRestoreTransform)
	{
		if (AActor* TargetActor = AnimationState.TargetActor.Get())
		{
			if (USceneComponent* TargetRootComponent = TargetActor->GetRootComponent())
			{
				TargetRootComponent->SetRelativeTransform(AnimationState.OriginalRelativeTransform);
			}
		}
	}

	RedrawJointPreviewLines();
	if (GEditor)
	{
		GEditor->RedrawAllViewports(false);
	}
}

void SCadWorkflowWizard::RedrawJointPreviewLines()
{
	ClearJointPreviewLines();

	const AActor* MasterActor = ConfirmedSelection.MasterActor.Get();
	UWorld* World = MasterActor ? MasterActor->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}

	const UCadImporterEditorUserSettings* Settings = GetDefault<UCadImporterEditorUserSettings>();
	if (Settings && !Settings->bDrawJointPreviewLines)
	{
		return;
	}

	const uint8 DepthPriority = (Settings && Settings->bDrawJointPreviewLinesInForeground) ? 1 : 0;
	const float LineThickness = (Settings && Settings->JointPreviewLineThickness > 0.0f)
		? Settings->JointPreviewLineThickness
		: 4.0f;
	const float AxisDrawLength = 40.0f;
	const float DefaultRevoluteRadius = 35.0f;
	const float DefaultPrismaticHalfLength = 25.0f;
	const float LimitMarkerHalfSize = 8.0f;

	for (const FEditableJointChildState& ChildState : EditableJointChildren)
	{
		AActor* ChildRootActor = CadActorHierarchyUtils::FindByPath(ChildState.ChildEntry.ActorPath);
		if (!ChildRootActor)
		{
			continue;
		}

		TMap<FString, FVector> LinkLocations;
		CollectNonStaticDescendantActorLocations(ChildRootActor, ChildRootActor, LinkLocations);
		TMap<FString, FTransform> LinkTransforms;
		TArray<AActor*> Descendants;
		CadActorHierarchyUtils::GetSortedAttachedChildren(ChildRootActor, Descendants, true);
		for (AActor* DescendantActor : Descendants)
		{
			if (!DescendantActor || DescendantActor->IsA<AStaticMeshActor>())
			{
				continue;
			}

			LinkTransforms.Add(DescendantActor->GetActorNameOrLabel(), DescendantActor->GetActorTransform());
		}
		const FVector WorldAnchorLocation = ChildRootActor->GetActorLocation();

		for (int32 JointIndex = 0; JointIndex < ChildState.ChildDocument.Joints.Num(); ++JointIndex)
		{
			if (!ChildState.JointDebugDrawEnabled.IsValidIndex(JointIndex) || !ChildState.JointDebugDrawEnabled[JointIndex])
			{
				continue;
			}

			const FCadChildJointDef& JointDef = ChildState.ChildDocument.Joints[JointIndex];
			const FString ChildName = JointDef.ChildActorName.TrimStartAndEnd();
			if (ChildName.IsEmpty())
			{
				continue;
			}

			const FVector* ChildLocation = LinkLocations.Find(ChildName);
			if (!ChildLocation)
			{
				continue;
			}

			const FString ParentName = JointDef.ParentActorName.TrimStartAndEnd();
			const FVector* ParentLocation = ParentName.IsEmpty() ? nullptr : LinkLocations.Find(ParentName);
			const FVector Start = ParentLocation ? *ParentLocation : WorldAnchorLocation;
			const FVector End = *ChildLocation;
			DrawDebugLine(
				World,
				Start,
				End,
				JointTypeToPreviewColor(Settings, JointDef.JointType),
				true,
				-1.0f,
				DepthPriority,
				LineThickness);

			if (JointTypeUsesAxis(JointDef.JointType))
			{
				FVector AxisDirection = JointDef.Axis.GetSafeNormal();
				if (AxisDirection.IsNearlyZero())
				{
					AxisDirection = FVector::UpVector;
				}
				if (const FTransform* ChildTransform = LinkTransforms.Find(ChildName))
				{
					AxisDirection = ChildTransform->TransformVectorNoScale(AxisDirection).GetSafeNormal();
					if (AxisDirection.IsNearlyZero())
					{
						AxisDirection = FVector::UpVector;
					}
				}

				DrawDebugDirectionalArrow(
					World,
					End,
					End + (AxisDirection * AxisDrawLength),
					12.0f,
					JointTypeToPreviewColor(Settings, JointDef.JointType),
					true,
					-1.0f,
					DepthPriority,
					LineThickness);

				if (JointDef.JointType == ECadImportJointType::Revolute)
				{
					const float StartAngleDegrees = JointDef.Limit.bHasLimit ? JointDef.Limit.Lower : 0.0f;
					const float EndAngleDegrees = JointDef.Limit.bHasLimit ? JointDef.Limit.Upper : 360.0f;
					DrawDebugArc(
						World,
						End,
						AxisDirection,
						DefaultRevoluteRadius,
						StartAngleDegrees,
						EndAngleDegrees,
						JointTypeToPreviewColor(Settings, JointDef.JointType),
						true,
						-1.0f,
						DepthPriority,
						LineThickness);
				}
				else if (JointDef.JointType == ECadImportJointType::Prismatic)
				{
					const float Lower = JointDef.Limit.bHasLimit ? JointDef.Limit.Lower : -DefaultPrismaticHalfLength;
					const float Upper = JointDef.Limit.bHasLimit ? JointDef.Limit.Upper : DefaultPrismaticHalfLength;
					const FVector RangeStart = End + (AxisDirection * Lower);
					const FVector RangeEnd = End + (AxisDirection * Upper);
					DrawDebugLine(
						World,
						RangeStart,
						RangeEnd,
						JointTypeToPreviewColor(Settings, JointDef.JointType),
						true,
						-1.0f,
						DepthPriority,
						LineThickness);

					FVector MarkerAxisX = FVector::RightVector;
					FVector MarkerAxisY = FVector::ForwardVector;
					BuildAxisBasis(AxisDirection, MarkerAxisX, MarkerAxisY);
					DrawDebugLine(
						World,
						RangeStart - (MarkerAxisX * LimitMarkerHalfSize),
						RangeStart + (MarkerAxisX * LimitMarkerHalfSize),
						JointTypeToPreviewColor(Settings, JointDef.JointType),
						true,
						-1.0f,
						DepthPriority,
						LineThickness);
					DrawDebugLine(
						World,
						RangeEnd - (MarkerAxisX * LimitMarkerHalfSize),
						RangeEnd + (MarkerAxisX * LimitMarkerHalfSize),
						JointTypeToPreviewColor(Settings, JointDef.JointType),
						true,
						-1.0f,
						DepthPriority,
						LineThickness);
					DrawDebugLine(
						World,
						RangeStart - (MarkerAxisY * LimitMarkerHalfSize),
						RangeStart + (MarkerAxisY * LimitMarkerHalfSize),
						JointTypeToPreviewColor(Settings, JointDef.JointType),
						true,
						-1.0f,
						DepthPriority,
						LineThickness);
					DrawDebugLine(
						World,
						RangeEnd - (MarkerAxisY * LimitMarkerHalfSize),
						RangeEnd + (MarkerAxisY * LimitMarkerHalfSize),
						JointTypeToPreviewColor(Settings, JointDef.JointType),
						true,
						-1.0f,
						DepthPriority,
						LineThickness);
				}
			}
		}
	}

	if (GEditor)
	{
		GEditor->RedrawAllViewports(false);
	}
}

bool SCadWorkflowWizard::TryBuildSelectionForGeneration(FCadMasterSelection& OutSelection, FString& OutError) const
{
	OutSelection = FCadMasterSelection();
	OutError.Reset();

	if (!ConfirmedSelection.IsValid())
	{
		OutError = TEXT("Confirm master actor first.");
		return false;
	}

	if (ChildEntries.Num() == 0)
	{
		OutError = TEXT("Confirmed master has no direct children.");
		return false;
	}

	OutSelection = ConfirmedSelection;
	OutSelection.Children.Reset();
	for (const FCadChildEntry& ChildEntry : ChildEntries)
	{
		if (CadMasterChildActorTypeShouldGenerateJson(ChildEntry.ActorType))
		{
			OutSelection.Children.Add(ChildEntry);
		}
	}

	if (OutSelection.Children.Num() == 0)
	{
		OutError = TEXT("All children are set to 'none'. Select at least one 'static' or 'movable' child.");
		return false;
	}

	return true;
}

bool SCadWorkflowWizard::TryBuildDryRunChildDocuments(
	const FCadMasterSelection& SelectionForGeneration,
	TArray<FCadChildDoc>& OutChildDocuments,
	FString& OutError)
{
	OutChildDocuments.Reset();
	OutError.Reset();

	CleanupDryRunPreviewFolder();

	const FString DryRunFolder = FPaths::Combine(
		FPaths::ProjectIntermediateDir(),
		TEXT("CadRobotAssemblyImporter"),
		TEXT("WorkflowDryRun"),
		FGuid::NewGuid().ToString(EGuidFormats::Digits));

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.CreateDirectoryTree(*DryRunFolder))
	{
		OutError = FString::Printf(TEXT("Failed to create dry-run folder: %s"), *DryRunFolder);
		return false;
	}

	const auto CleanupDryRunFolder = [&PlatformFile, &DryRunFolder]()
	{
		PlatformFile.DeleteDirectoryRecursively(*DryRunFolder);
	};

	FCadMasterJsonGenerationResult PreviewMasterResult;
	if (!CadMasterDocExporter::TryGenerateAndWriteFromSelectionResult(
		SelectionForGeneration,
		DryRunFolder,
		PreviewMasterResult,
		OutError))
	{
		OutError = FString::Printf(TEXT("Master JSON dry-run generation failed: %s"), *OutError);
		CleanupDryRunFolder();
		return false;
	}

	FCadChildJsonResult PreviewChildResult;
	if (!CadChildDocExporter::TryExtractChildJsonFilesFromDocument(
		PreviewMasterResult.WorkspacePaths.MasterJsonPath,
		PreviewMasterResult.Document,
		PreviewChildResult,
		OutError))
	{
		OutError = FString::Printf(TEXT("Child JSON dry-run generation failed: %s"), *OutError);
		CleanupDryRunFolder();
		return false;
	}

	for (const FString& ChildJsonPath : PreviewChildResult.GeneratedChildJsonPaths)
	{
		FCadChildDoc ParsedChildDocument;
		if (!CadChildDocParser::TryLoadChildDocumentFromJsonPath(ChildJsonPath, ParsedChildDocument, OutError))
		{
			OutError = FString::Printf(TEXT("Dry-run child json readback failed for '%s': %s"), *ChildJsonPath, *OutError);
			CleanupDryRunFolder();
			return false;
		}

		if (!IsMovableChildActorType(ParsedChildDocument.ActorType))
		{
			continue;
		}

		OutChildDocuments.Add(MoveTemp(ParsedChildDocument));
	}

	DryRunPreviewFolderPath = PreviewMasterResult.WorkspacePaths.WorkspaceFolder;
	return true;
}

void SCadWorkflowWizard::RebuildChildRows()
{
	if (!ChildTypeRowsBox.IsValid())
	{
		return;
	}

	ChildTypeRowsBox->ClearChildren();

	if (ChildEntries.Num() == 0)
	{
		ChildTypeRowsBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("No confirmed direct children.")))
		];
		return;
	}

	for (int32 ChildIndex = 0; ChildIndex < ChildEntries.Num(); ++ChildIndex)
	{
		ChildTypeRowsBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(ChildEntries[ChildIndex].ActorName))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				SNew(SComboBox<TSharedPtr<FString>>)
				.OptionsSource(&ChildTypeItems)
				.OnGenerateWidget_Lambda([](const TSharedPtr<FString> Item)
				{
					return SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? *Item : TEXT("")));
				})
				.OnSelectionChanged_Lambda([this, ChildIndex](const TSharedPtr<FString> SelectedType, ESelectInfo::Type)
				{
					if (!SelectedType.IsValid())
					{
						return;
					}
					SetChildType(ChildIndex, *SelectedType);
				})
				[
					SNew(STextBlock)
					.Text_Lambda([this, ChildIndex]()
					{
						if (!ChildEntries.IsValidIndex(ChildIndex))
						{
							return FText::FromString(TEXT("static"));
						}
						return FText::FromString(MasterChildActorTypeToUiString(ChildEntries[ChildIndex].ActorType));
					})
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.Text_Lambda([this, ChildIndex]()
				{
					return FText::FromString(IsChildIsolated(ChildIndex) ? TEXT("Restore") : TEXT("Show"));
				})
				.OnClicked_Lambda([this, ChildIndex]()
				{
					return ToggleChildVisibility(ChildIndex);
				})
			]
		];
	}
}

void SCadWorkflowWizard::SetChildType(const int32 ChildIndex, const FString& SelectedType)
{
	if (!ChildEntries.IsValidIndex(ChildIndex))
	{
		return;
	}

	ECadMasterChildActorType ParsedType = ECadMasterChildActorType::Static;
	if (!TryParseMasterChildActorTypeFromUiString(SelectedType, ParsedType))
	{
		return;
	}

	ChildEntries[ChildIndex].ActorType = ParsedType;
}

void SCadWorkflowWizard::SaveChildVisibility()
{
	CadActorHierarchyUtils::SaveVisibilitySnapshot(ChildEntries, SavedVisibility);
}

void SCadWorkflowWizard::IsolateChildVisibility(const int32 ChildIndex)
{
	CadActorHierarchyUtils::ApplyVisibilityIsolation(ChildEntries, ChildIndex);

	if (GEditor)
	{
		GEditor->RedrawAllViewports(false);
	}
}

void SCadWorkflowWizard::RestoreChildVisibilityState()
{
	CadActorHierarchyUtils::RestoreVisibilitySnapshot(SavedVisibility);

	SavedVisibility.Reset();
	IsolatedIndex = INDEX_NONE;

	if (GEditor)
	{
		GEditor->RedrawAllViewports(false);
	}
}

bool SCadWorkflowWizard::IsChildIsolated(const int32 ChildIndex) const
{
	return SavedVisibility.Num() > 0 && IsolatedIndex == ChildIndex;
}

FReply SCadWorkflowWizard::ToggleChildVisibility(const int32 ChildIndex)
{
	if (!ChildEntries.IsValidIndex(ChildIndex))
	{
		return FReply::Handled();
	}

	if (IsChildIsolated(ChildIndex))
	{
		RestoreChildVisibilityState();
		const AActor* MasterActor = ConfirmedSelection.MasterActor.Get();
		SetStatus(FString::Printf(
			TEXT("Visibility restored for child actors under master '%s'."),
			MasterActor ? *MasterActor->GetActorNameOrLabel() : TEXT("(none)")));
		return FReply::Handled();
	}

	if (SavedVisibility.Num() == 0)
	{
		SaveChildVisibility();
	}

	IsolateChildVisibility(ChildIndex);
	IsolatedIndex = ChildIndex;
	SetStatus(FString::Printf(TEXT("Isolated child visibility: %s"), *ChildEntries[ChildIndex].ActorName));
	return FReply::Handled();
}

FReply SCadWorkflowWizard::HandleBrowseWorkspace()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		SetStatus(TEXT("Workspace browse failed: Desktop platform module is unavailable."));
		return FReply::Handled();
	}

	const void* ParentWindowHandle = nullptr;
	if (FSlateApplication::IsInitialized())
	{
		ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
	}

	FString PickedFolder;
	const bool bOpened = DesktopPlatform->OpenDirectoryDialog(
		const_cast<void*>(ParentWindowHandle),
		TEXT("Select Master Workflow Workspace"),
		WorkspaceFolder,
		PickedFolder);

	if (bOpened && !PickedFolder.TrimStartAndEnd().IsEmpty())
	{
		WorkspaceFolder = PickedFolder;
		if (WorkspaceTextBox.IsValid())
		{
			WorkspaceTextBox->SetText(FText::FromString(WorkspaceFolder));
		}
		SetStatus(FString::Printf(TEXT("Workspace selected: %s"), *WorkspaceFolder));
	}

	return FReply::Handled();
}

FReply SCadWorkflowWizard::HandleApplyWorkspace()
{
	FString NormalizedWorkspace;
	FString WorkspaceValidationError;
	if (!CadWorkspaceUtils::TryValidateForApply(WorkspaceFolder, NormalizedWorkspace, WorkspaceValidationError))
	{
		SetStatus(FString::Printf(TEXT("Workspace apply failed:\n%s"), *WorkspaceValidationError));
		return FReply::Handled();
	}

	WorkspaceFolder = MoveTemp(NormalizedWorkspace);
	if (WorkspaceTextBox.IsValid())
	{
		WorkspaceTextBox->SetText(FText::FromString(WorkspaceFolder));
	}

	SetStep(1);
	SetStatus(FString::Printf(TEXT("Workspace applied: %s"), *WorkspaceFolder));
	return FReply::Handled();
}

FReply SCadWorkflowWizard::HandleBack()
{
	if (StepIndex == 3)
	{
		StopJointTestAnimation(true);
		ClearJointPreviewLines();
	}

	const int32 NextStep = FMath::Max(0, StepIndex - 1);
	if (SavedVisibility.Num() > 0 && NextStep < 2)
	{
		RestoreChildVisibilityState();
	}

	SetStep(NextStep);
	return FReply::Handled();
}

FReply SCadWorkflowWizard::ConfirmMaster()
{
	if (SavedVisibility.Num() > 0)
	{
		RestoreChildVisibilityState();
	}

	FString NormalizedWorkspace;
	FString WorkspaceValidationError;
	if (!CadWorkspaceUtils::TryValidateForGeneration(WorkspaceFolder, NormalizedWorkspace, WorkspaceValidationError))
	{
		SetStatus(FString::Printf(TEXT("Master actor confirmation failed:\n%s"), *WorkspaceValidationError));
		return FReply::Handled();
	}

	WorkspaceFolder = MoveTemp(NormalizedWorkspace);
	if (WorkspaceTextBox.IsValid())
	{
		WorkspaceTextBox->SetText(FText::FromString(WorkspaceFolder));
	}

	FString Error;
	FCadMasterSelection SelectionResult;
	if (!CadMasterSelectionCollector::TryCollectFromSelection(SelectionResult, Error))
	{
		SetStatus(FString::Printf(TEXT("Master actor confirmation failed:\n%s"), *Error));
		return FReply::Handled();
	}

	ConfirmedSelection = SelectionResult;
	ChildEntries = SelectionResult.Children;
	SavedVisibility.Reset();
	IsolatedIndex = INDEX_NONE;
	MasterJsonResult = FCadMasterJsonGenerationResult();
	ChildJsonResult = FCadChildJsonResult();
	BuildInput = FCadWorkflowBuildInput();
	LastBuildReplaceResult = FCadLevelReplaceResult();
	bCanRevertLastBuild = false;
	ResetJointEditorState();
	CleanupDryRunPreviewFolder();
	RebuildChildRows();

	const AActor* ConfirmedMasterActor = ConfirmedSelection.MasterActor.Get();
	SetStep(2);
	SetStatus(FString::Printf(
		TEXT("Master actor confirmed.\nmaster=%s\nchildren=%d\n다음 단계에서 child actor_type을 선택 후 joint setup 단계로 이동하세요. 'none'은 JSON 생성에서 제외합니다."),
		ConfirmedMasterActor ? *ConfirmedMasterActor->GetActorNameOrLabel() : TEXT("(none)"),
		ChildEntries.Num()));
	return FReply::Handled();
}

FReply SCadWorkflowWizard::ProceedToJointSetup()
{
	FCadMasterSelection SelectionForGeneration;
	FString Error;
	if (!TryBuildSelectionForGeneration(SelectionForGeneration, Error))
	{
		SetStatus(FString::Printf(TEXT("Joint setup step failed:\n%s"), *Error));
		return FReply::Handled();
	}

	if (SavedVisibility.Num() > 0)
	{
		RestoreChildVisibilityState();
	}

	TArray<FCadChildDoc> PreviewChildDocuments;
	if (!TryBuildDryRunChildDocuments(SelectionForGeneration, PreviewChildDocuments, Error))
	{
		SetStatus(FString::Printf(TEXT("Joint setup step failed during dry-run JSON validation:\n%s"), *Error));
		return FReply::Handled();
	}

	LoadEditableJointDocuments(SelectionForGeneration, PreviewChildDocuments);
	SetStep(3);
	SetStatus(FString::Printf(
		TEXT("Joint setup editor is ready.\ndry_run_folder=%s\nDry-run JSON files were generated and read back successfully; review and edit the parsed joint data, then continue."),
		DryRunPreviewFolderPath.IsEmpty() ? TEXT("(not available)") : *DryRunPreviewFolderPath));
	return FReply::Handled();
}

FReply SCadWorkflowWizard::GenerateWorkflowJson()
{
	FString NormalizedWorkspace;
	FString WorkspaceValidationError;
	if (!CadWorkspaceUtils::TryValidateForGeneration(WorkspaceFolder, NormalizedWorkspace, WorkspaceValidationError))
	{
		SetStatus(FString::Printf(TEXT("Generate JSON failed:\n%s"), *WorkspaceValidationError));
		return FReply::Handled();
	}

	WorkspaceFolder = MoveTemp(NormalizedWorkspace);
	if (WorkspaceTextBox.IsValid())
	{
		WorkspaceTextBox->SetText(FText::FromString(WorkspaceFolder));
	}

	StopJointTestAnimation(true);

	FCadMasterSelection SelectionForGeneration;
	FString Error;
	if (!TryBuildSelectionForGeneration(SelectionForGeneration, Error))
	{
		SetStatus(FString::Printf(TEXT("Generate JSON failed:\n%s"), *Error));
		return FReply::Handled();
	}

	if (!ValidateEditedJointDefinitions(Error))
	{
		SetStatus(FString::Printf(TEXT("Generate JSON failed during joint validation:\n%s"), *Error));
		return FReply::Handled();
	}

	if (!CadMasterDocExporter::TryGenerateAndWriteFromSelectionResult(SelectionForGeneration, WorkspaceFolder, MasterJsonResult, Error))
	{
		SetStatus(FString::Printf(TEXT("Generate JSON failed:\n%s"), *Error));
		return FReply::Handled();
	}

	const FString MasterJsonPath = MasterJsonResult.WorkspacePaths.MasterJsonPath;
	if (MasterJsonPath.IsEmpty())
	{
		SetStatus(TEXT("Generate JSON failed: master json path is empty."));
		return FReply::Handled();
	}

	if (!CadChildDocExporter::TryExtractChildJsonFilesFromDocument(
		MasterJsonPath,
		MasterJsonResult.Document,
		ChildJsonResult,
		Error))
	{
		SetStatus(FString::Printf(TEXT("Generate JSON failed while creating child json files:\n%s"), *Error));
		return FReply::Handled();
	}

	if (!TryApplyEditedChildJsonOverrides(Error))
	{
		SetStatus(FString::Printf(TEXT("Generate JSON failed while applying edited joint overrides:\n%s"), *Error));
		return FReply::Handled();
	}

	BuildInput = ChildJsonResult.BuildInput;
	if (BuildInput.ContentRootPath.TrimStartAndEnd().IsEmpty())
	{
		BuildInput.ContentRootPath = MasterJsonResult.WorkspacePaths.ContentRootPath;
	}

	if (SavedVisibility.Num() > 0)
	{
		RestoreChildVisibilityState();
	}

	ClearJointPreviewLines();
	CleanupDryRunPreviewFolder();
	SetStep(4);
	SetStatus(FString::Printf(
		TEXT("Master/Child JSON generated.\nmaster=%s\nchild_count=%d\nchild_folder=%s\nnext_step=build"),
		*MasterJsonResult.WorkspacePaths.MasterJsonPath,
		ChildJsonResult.GeneratedChildJsonPaths.Num(),
		*ChildJsonResult.ChildJsonFolderPath));
	return FReply::Handled();
}

FReply SCadWorkflowWizard::ContinueFromJointSetupPreview()
{
	return GenerateWorkflowJson();
}

FReply SCadWorkflowWizard::BuildAssembly()
{
	if (!Runner.IsValid())
	{
		SetStatus(TEXT("Build failed: importer runner is unavailable."));
		return FReply::Handled();
	}

	if (BuildInput.MasterJsonPath.TrimStartAndEnd().IsEmpty() ||
		BuildInput.ChildJsonFolderPath.TrimStartAndEnd().IsEmpty())
	{
		SetStatus(TEXT("Build failed: workflow input is incomplete. Run previous steps first."));
		return FReply::Handled();
	}

	FCadLevelReplaceResult ReplaceResult;
	if (!Runner->BuildFromWorkflow(BuildInput, ImportOptions, &ReplaceResult))
	{
		LastBuildReplaceResult = FCadLevelReplaceResult();
		bCanRevertLastBuild = false;
		SetStatus(TEXT("Build failed. Check message log for parser/import/replacement errors."));
		return FReply::Handled();
	}

	LastBuildReplaceResult = ReplaceResult;
	bCanRevertLastBuild = ReplaceResult.bUsedTransaction;
	SetStep(5);
	SetStatus(FString::Printf(
		TEXT("Build completed and level replacement executed.\nspawned_master=%s\nspawned_children=%d\ndeleted=%d\nrevert_available=%s"),
		*LastBuildReplaceResult.SpawnedActorPath,
		LastBuildReplaceResult.SpawnedChildActorCount,
		LastBuildReplaceResult.DeletedActorCount,
		bCanRevertLastBuild ? TEXT("true") : TEXT("false")));
	return FReply::Handled();
}

FReply SCadWorkflowWizard::RevertLastBuild()
{
	if (!bCanRevertLastBuild)
	{
		SetStatus(TEXT("Revert failed: no undoable build replacement is available."));
		return FReply::Handled();
	}

	if (!GEditor || !GEditor->UndoTransaction())
	{
		SetStatus(TEXT("Revert failed: editor undo transaction could not be executed."));
		return FReply::Handled();
	}

	bCanRevertLastBuild = false;
	LastBuildReplaceResult = FCadLevelReplaceResult();
	SetStep(4);
	SetStatus(TEXT("Last build replacement was reverted via editor undo."));
	return FReply::Handled();
}

FReply SCadWorkflowWizard::RestartWorkflow()
{
	if (SavedVisibility.Num() > 0)
	{
		RestoreChildVisibilityState();
	}

	ConfirmedSelection = FCadMasterSelection();
	ChildEntries.Reset();
	SavedVisibility.Reset();
	IsolatedIndex = INDEX_NONE;
	MasterJsonResult = FCadMasterJsonGenerationResult();
	ChildJsonResult = FCadChildJsonResult();
	BuildInput = FCadWorkflowBuildInput();
	LastBuildReplaceResult = FCadLevelReplaceResult();
	bCanRevertLastBuild = false;
	ResetJointEditorState();
	CleanupDryRunPreviewFolder();
	RebuildChildRows();
	SetStep(0);
	SetStatus(TEXT("Workflow restarted. Step 1: Set workspace folder."));
	return FReply::Handled();
}

void SCadWorkflowWizard::SetStatus(const FString& InMessage)
{
	StatusMessage = InMessage;
	UE_LOG(LogCadImporter, Display, TEXT("%s"), *InMessage);
}

void SCadWorkflowWizard::SetStep(const int32 InStepIndex)
{
	StepIndex = FMath::Clamp(InStepIndex, 0, 5);
}
