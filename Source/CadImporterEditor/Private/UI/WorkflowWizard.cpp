#include "UI/WorkflowWizard.h"
#include "UI/WorkflowWizardSharedUtils.h"

#include "CadImportStringUtils.h"
#include "CadImporterEditorUserSettings.h"
#include "ChildDocParser.h"
#include "ImportService.h"
#include "CadImporterEditor.h"
#include "DesktopPlatformModule.h"
#include "Editor/ActorHierarchyUtils.h"
#include "Editor.h"
#include "Components/SceneComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/Selection.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformFileManager.h"
#include "IDesktopPlatform.h"
#include "Misc/MessageDialog.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "DrawDebugHelpers.h"
#include "Workflow/WorkspaceUtils.h"
#include "Widgets/Images/SImage.h"
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
#include "Widgets/Views/SHeaderRow.h"

using namespace CadWorkflowWizardShared;

void SCadWorkflowWizard::Construct(const FArguments& InArgs)
{
	Runner = InArgs._Runner;
#if UE_BUILD_DEVELOPMENT
	WorkspaceFolder = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MasterWorkspace")));
#else
	WorkspaceFolder = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
#endif
	StatusMessage = TEXT("Step 1: Set workspace folder.");
	SelectionPreviewText = TEXT("선택된 액터 없음");
	FlattenPreviewText = TEXT("Confirm master actor first.");
	MovableJointPreviewText = TEXT("No movable child actors are ready for joint setup preview yet.");
	DryRunPreviewFolderPath.Reset();
	StepIndex = 0;
	ChildTypeItems.Reset();
	ChildTypeItems.Add(MakeShared<FString>(TEXT("static")));
	ChildTypeItems.Add(MakeShared<FString>(TEXT("background")));
	ChildTypeItems.Add(MakeShared<FString>(TEXT("movable")));
	ChildTypeItems.Add(MakeShared<FString>(TEXT("none")));
	JointTypeItems.Reset();
	JointTypeItems.Add(MakeShared<FString>(TEXT("fixed")));
	JointTypeItems.Add(MakeShared<FString>(TEXT("revolute")));
	JointTypeItems.Add(MakeShared<FString>(TEXT("prismatic")));
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
						TEXT("3/5 Configure Children"),
						TEXT("3/5 Configure Children"),
						TEXT("4/5 Joint Setup + Generate JSON"),
						TEXT("5/5 Build Actor"),
						TEXT("Completed")
					};
					const int32 SafeIndex = FMath::Clamp(StepIndex, 0, 6);
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
					.Padding(0.0f, 0.0f, 0.0f, 10.0f)
					[
						SNew(SBox)
						.HeightOverride(150.0f)
						[
							SNew(SImage)
							.Image(FCadImporterEditorModule::GetWorkflowStepBrush())
						]
					]

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
						.Text(FText::FromString(TEXT("선택 액터 / 직계 자식 브랜치 요약 (1초 간격 자동 갱신)")))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						SNew(STextBlock)
						.Text_Lambda([this]()
						{
							return FText::FromString(SelectionPreviewText);
						})
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 6.0f)
					[
						SNew(SHeaderRow)
						+ SHeaderRow::Column(TEXT("SelBranchName"))
						.FillWidth(0.55f)
						.DefaultLabel(FText::FromString(TEXT("Branch")))
						+ SHeaderRow::Column(TEXT("SelBranchDepth"))
						.FixedWidth(70.0f)
						.HAlignCell(HAlign_Center)
						.DefaultLabel(FText::FromString(TEXT("Depth")))
						+ SHeaderRow::Column(TEXT("SelBranchDirect"))
						.FixedWidth(70.0f)
						.HAlignCell(HAlign_Center)
						.DefaultLabel(FText::FromString(TEXT("Direct")))
						+ SHeaderRow::Column(TEXT("SelBranchDesc"))
						.FixedWidth(70.0f)
						.HAlignCell(HAlign_Center)
						.DefaultLabel(FText::FromString(TEXT("Desc")))
						+ SHeaderRow::Column(TEXT("SelBranchNested"))
						.FixedWidth(100.0f)
						.HAlignCell(HAlign_Center)
						.DefaultLabel(FText::FromString(TEXT("Nested Mesh")))
					]

					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					.Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()
						[
							SAssignNew(SelectionBranchRowsBox, SVerticalBox)
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
						.Text(FText::FromString(TEXT("Choose which master direct-child branches to unpack by one level. Unpack deletes the selected direct child and promotes its children to direct children of the master.")))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						SNew(SBox)
						.MinDesiredHeight(140.0f)
						[
							SNew(SMultiLineEditableTextBox)
							.IsReadOnly(true)
							.Font(FAppStyle::Get().GetFontStyle(TEXT("MonoFont")))
							.Text_Lambda([this]()
							{
								return FText::FromString(FlattenPreviewText);
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
							SAssignNew(FlattenRowsBox, SVerticalBox)
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
							.Text(FText::FromString(TEXT("Apply Unpack + Next")))
							.OnClicked(this, &SCadWorkflowWizard::ApplyFlattenAndContinue)
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
						.Text(FText::FromString(TEXT("Review direct children, use Show / Type, and preview one-level unpack with the button on the right. Unpack is applied only when you press Next.")))
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
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Unpack preview rows are UI-only until Next. Reset restores the original direct-child list.")))
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SButton)
							.Text(FText::FromString(TEXT("Reset")))
							.OnClicked(this, &SCadWorkflowWizard::ResetFlattenPreview)
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 6.0f)
					[
						SNew(SHeaderRow)
						+ SHeaderRow::Column(TEXT("ChildName"))
						.FillWidth(0.45f)
						.DefaultLabel(FText::FromString(TEXT("Actor")))
						+ SHeaderRow::Column(TEXT("ChildInfo"))
						.FillWidth(0.25f)
						.DefaultLabel(FText::FromString(TEXT("Info")))
						+ SHeaderRow::Column(TEXT("ChildType"))
						.FixedWidth(130.0f)
						.HAlignCell(HAlign_Center)
						.DefaultLabel(FText::FromString(TEXT("Type")))
						+ SHeaderRow::Column(TEXT("ChildShow"))
						.FixedWidth(90.0f)
						.HAlignCell(HAlign_Center)
						.DefaultLabel(FText::FromString(TEXT("Show")))
						+ SHeaderRow::Column(TEXT("ChildFlatten"))
						.FixedWidth(110.0f)
						.HAlignCell(HAlign_Center)
						.DefaultLabel(FText::FromString(TEXT("Unpack")))
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

	RebuildFlattenRows();
	RebuildSelectionBranchRows();
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
		SelectionBranchStats.Reset();
		RebuildSelectionBranchRows();
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
		SelectionBranchStats.Reset();
		RebuildSelectionBranchRows();
		return;
	}

	TArray<AActor*> DirectChildren;
	CadActorHierarchyUtils::GetSortedAttachedChildren(SingleSelectedActor, DirectChildren, false);
	SelectionBranchStats.Reset();
	CadActorHierarchyUtils::AnalyzeDirectChildBranches(SingleSelectedActor, SelectionBranchStats);

	FString Result = FString::Printf(
		TEXT("Selected: %s\nPath: %s\nDirect Children: %d"),
		*SingleSelectedActor->GetActorNameOrLabel(),
		*SingleSelectedActor->GetPathName(),
		DirectChildren.Num());

	SelectionPreviewText = MoveTemp(Result);
	RebuildSelectionBranchRows();
}

void SCadWorkflowWizard::RebuildSelectionBranchRows()
{
	if (!SelectionBranchRowsBox.IsValid())
	{
		return;
	}

	SelectionBranchRowsBox->ClearChildren();

	if (SelectionBranchStats.Num() == 0)
	{
		SelectionBranchRowsBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("No direct child branches.")))
		];
		return;
	}

	for (int32 BranchIndex = 0; BranchIndex < SelectionBranchStats.Num(); ++BranchIndex)
	{
		const FCadHierarchyBranchStats& BranchStat = SelectionBranchStats[BranchIndex];

		SelectionBranchRowsBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(0.55f)
				.VAlign(VAlign_Center)
				.Padding(4.0f, 2.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(BranchStat.BranchName))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4.0f, 2.0f)
				[
					SNew(SBox)
					.WidthOverride(70.0f)
					[
						SNew(STextBlock)
						.Justification(ETextJustify::Center)
						.Text(FText::AsNumber(BranchStat.MaxDepthFromRoot))
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4.0f, 2.0f)
				[
					SNew(SBox)
					.WidthOverride(70.0f)
					[
						SNew(STextBlock)
						.Justification(ETextJustify::Center)
						.Text(FText::AsNumber(BranchStat.DirectChildCount))
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4.0f, 2.0f)
				[
					SNew(SBox)
					.WidthOverride(70.0f)
					[
						SNew(STextBlock)
						.Justification(ETextJustify::Center)
						.Text(FText::AsNumber(BranchStat.DescendantCount))
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4.0f, 2.0f)
				[
					SNew(SBox)
					.WidthOverride(100.0f)
					[
						SNew(STextBlock)
						.Justification(ETextJustify::Center)
						.Text(FText::AsNumber(BranchStat.NestedStaticMeshActorCount))
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 6.0f, 0.0f, 0.0f)
			[
				SNew(SBorder)
				.Visibility(BranchIndex + 1 < SelectionBranchStats.Num() ? EVisibility::Visible : EVisibility::Collapsed)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor(0.25f, 0.25f, 0.25f, 0.7f))
				.Padding(FMargin(0.0f, 0.5f))
			]
		];
	}
}

void SCadWorkflowWizard::RefreshFlattenBranchCandidates()
{
	FlattenBranchStats.Reset();
	FlattenBranchSelections.Reset();
	FlattenableChildActorPaths.Reset();

	const AActor* MasterActor = ConfirmedSelection.MasterActor.Get();
	if (!MasterActor)
	{
		FlattenPreviewText = TEXT("Confirm master actor first.");
		RebuildFlattenRows();
		return;
	}

	CadActorHierarchyUtils::AnalyzeDirectChildBranches(const_cast<AActor*>(MasterActor), FlattenBranchStats);
	FlattenBranchSelections.Init(false, FlattenBranchStats.Num());
	for (const FCadHierarchyBranchStats& BranchStat : FlattenBranchStats)
	{
		if (BranchStat.bCanFlattenOneLevel)
		{
			FlattenableChildActorPaths.Add(BranchStat.BranchPath);
		}
	}
	const FString BranchTableText = BuildBranchDepthTableText(FlattenBranchStats);
	FlattenPreviewText = FString::Printf(
		TEXT("Master:\t%s\nPath:\t%s\n\n%s"),
		*MasterActor->GetActorNameOrLabel(),
		*MasterActor->GetPathName(),
		*BranchTableText);
	RebuildFlattenRows();
}

void SCadWorkflowWizard::RebuildFlattenRows()
{
	if (!FlattenRowsBox.IsValid())
	{
		return;
	}

	FlattenRowsBox->ClearChildren();

	if (FlattenBranchStats.Num() == 0)
	{
		FlattenRowsBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("No unpack candidates available yet.")))
		];
		return;
	}

	for (int32 BranchIndex = 0; BranchIndex < FlattenBranchStats.Num(); ++BranchIndex)
	{
		const FCadHierarchyBranchStats& BranchStat = FlattenBranchStats[BranchIndex];
		const FString RowText = FString::Printf(
			TEXT("%s\tdepth=%d\tdirect=%d\tdesc=%d\tnested_mesh=%d\tunpackable=%s"),
			*BranchStat.BranchName,
			BranchStat.MaxDepthFromRoot,
			BranchStat.DirectChildCount,
			BranchStat.DescendantCount,
			BranchStat.NestedStaticMeshActorCount,
			*BoolToYesNoString(BranchStat.bCanFlattenOneLevel));

		FlattenRowsBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			SNew(SCheckBox)
			.IsEnabled(BranchStat.bCanFlattenOneLevel)
			.IsChecked_Lambda([this, BranchIndex]()
			{
				return FlattenBranchSelections.IsValidIndex(BranchIndex) && FlattenBranchSelections[BranchIndex]
					? ECheckBoxState::Checked
					: ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([this, BranchIndex](const ECheckBoxState NewState)
			{
				SetFlattenBranchSelected(BranchIndex, NewState == ECheckBoxState::Checked);
			})
			[
				SNew(STextBlock)
				.Font(FAppStyle::Get().GetFontStyle(TEXT("MonoFont")))
				.Text(FText::FromString(RowText))
			]
		];
	}
}

void SCadWorkflowWizard::RebuildChildEntriesFromFlattenPreview()
{
	TMap<FString, ECadMasterChildActorType> ActorTypesByPath;
	for (const FCadChildEntry& ChildEntry : ChildEntries)
	{
		ActorTypesByPath.Add(ChildEntry.ActorPath, ChildEntry.ActorType);
	}

	ChildEntries.Reset();
	PreviewPromotedChildActorPaths.Reset();
	FlattenableChildActorPaths.Reset();

	AActor* MasterActor = ConfirmedSelection.MasterActor.Get();
	if (!MasterActor)
	{
		return;
	}

	for (const FCadChildEntry& BaseChildEntry : BaseChildEntries)
	{
		AppendFlattenPreviewEntriesRecursive(
			BaseChildEntry,
			MasterActor,
			PendingFlattenBranchPaths,
			ActorTypesByPath,
			ChildEntries,
			PreviewPromotedChildActorPaths);
	}

	for (const FCadChildEntry& ChildEntry : ChildEntries)
	{
		AActor* ChildActor = CadActorHierarchyUtils::FindByPath(ChildEntry.ActorPath);
		if (CadActorHierarchyUtils::CanActorFlattenOneLevel(ChildActor))
		{
			FlattenableChildActorPaths.Add(ChildEntry.ActorPath);
		}
	}
}

void SCadWorkflowWizard::PreviewFlattenChild(const int32 ChildIndex)
{
	if (!ChildEntries.IsValidIndex(ChildIndex))
	{
		return;
	}

	const FCadChildEntry SelectedEntry = ChildEntries[ChildIndex];
	if (!FlattenableChildActorPaths.Contains(SelectedEntry.ActorPath) || PendingFlattenBranchPaths.Contains(SelectedEntry.ActorPath))
	{
		return;
	}

	AActor* BranchActor = CadActorHierarchyUtils::FindByPath(SelectedEntry.ActorPath);
	int32 DirectChildCount = 0;
	if (!CadActorHierarchyUtils::CanActorFlattenOneLevel(BranchActor, &DirectChildCount))
	{
		return;
	}

	if (DirectChildCount > 20)
	{
		const EAppReturnType::Type Response = FMessageDialog::Open(
			EAppMsgType::OkCancel,
			FText::FromString(FString::Printf(
				TEXT("'%s' has %d direct children.\nUnpack preview will replace this row with all of them.\n\nContinue?"),
				*SelectedEntry.ActorName,
				DirectChildCount)));
		if (Response != EAppReturnType::Ok)
		{
			return;
		}
	}

	if (SavedVisibility.Num() > 0)
	{
		RestoreChildVisibilityState();
	}

	PendingFlattenBranchPaths.Add(SelectedEntry.ActorPath);
	SavedVisibility.Reset();
	IsolatedIndex = INDEX_NONE;
	RebuildChildEntriesFromFlattenPreview();
	RebuildChildRows();

	TArray<AActor*> PromotedActors;
	CadActorHierarchyUtils::GetSortedAttachedChildren(BranchActor, PromotedActors, false);
	TArray<FString> PromotedNames;
	for (AActor* PromotedActor : PromotedActors)
	{
		if (PromotedActor)
		{
			PromotedNames.Add(PromotedActor->GetActorNameOrLabel());
		}
	}

	SetStatus(FString::Printf(
		TEXT("Unpack preview updated.\nbranch=%s\npromoted_children=%s\nNext를 누르면 실제 unpack이 적용됩니다."),
		*SelectedEntry.ActorName,
		PromotedNames.Num() > 0 ? *FString::Join(PromotedNames, TEXT(", ")) : TEXT("(none)")));
}

bool SCadWorkflowWizard::ApplyPendingFlattenPreview(FString& OutError)
{
	OutError.Reset();

	if (!ConfirmedSelection.IsValid())
	{
		OutError = TEXT("Confirm master actor first.");
		return false;
	}

	AActor* MasterActor = ConfirmedSelection.MasterActor.Get();
	if (!MasterActor)
	{
		OutError = TEXT("Confirmed master actor is invalid.");
		return false;
	}

	if (PendingFlattenBranchPaths.Num() == 0)
	{
		return true;
	}

	TArray<FString> BranchPathsToFlatten = PendingFlattenBranchPaths.Array();
	FCadHierarchyFlattenResult FlattenResult;
	if (!CadActorHierarchyUtils::TryFlattenSelectedDirectChildBranchesOneLevel(MasterActor, BranchPathsToFlatten, FlattenResult, OutError))
	{
		return false;
	}

	TMap<FString, ECadMasterChildActorType> PreviewTypesByPath;
	for (const FCadChildEntry& PreviewEntry : ChildEntries)
	{
		PreviewTypesByPath.Add(PreviewEntry.ActorPath, PreviewEntry.ActorType);
	}

	FCadMasterSelection UpdatedSelection;
	if (!CadMasterSelectionCollector::TryCollectFromMasterActor(MasterActor, UpdatedSelection, OutError))
	{
		return false;
	}

	ConfirmedSelection = UpdatedSelection;
	BaseChildEntries = UpdatedSelection.Children;
	ChildEntries = BaseChildEntries;
	for (FCadChildEntry& ChildEntry : ChildEntries)
	{
		if (const ECadMasterChildActorType* PreviewType = PreviewTypesByPath.Find(ChildEntry.ActorPath))
		{
			ChildEntry.ActorType = *PreviewType;
		}
	}

	PendingFlattenBranchPaths.Reset();
	PreviewPromotedChildActorPaths.Reset();
	RefreshFlattenBranchCandidates();
	RebuildChildEntriesFromFlattenPreview();
	RebuildChildRows();
	return true;
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
		OutError = TEXT("All children are set to 'none'. Select at least one 'static', 'background', or 'movable' child.");
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
		const FCadChildEntry& ChildEntry = ChildEntries[ChildIndex];
		const bool bIsPreviewPromoted = PreviewPromotedChildActorPaths.Contains(ChildEntry.ActorPath);
		const bool bCanPreviewUnpack = FlattenableChildActorPaths.Contains(ChildEntry.ActorPath);
		const bool bUnpackPending = PendingFlattenBranchPaths.Contains(ChildEntry.ActorPath);
		const FString InfoText = BuildChildHierarchyInfoLabel(ChildEntry);

		ChildTypeRowsBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(0.45f)
				.VAlign(VAlign_Center)
				.Padding(4.0f, 2.0f)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromString(ChildEntry.ActorName))
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Center)
					.Padding(6.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(bIsPreviewPromoted ? TEXT("[unpack-preview]") : TEXT("")))
					]
				]

				+ SHorizontalBox::Slot()
				.FillWidth(0.25f)
				.VAlign(VAlign_Center)
				.Padding(4.0f, 2.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(InfoText))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4.0f, 2.0f)
				[
					SNew(SBox)
					.WidthOverride(130.0f)
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
								return FText::FromString(CadImportStringUtils::ToMasterChildActorTypeString(ChildEntries[ChildIndex].ActorType));
							})
						]
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4.0f, 2.0f)
				[
					SNew(SBox)
					.WidthOverride(90.0f)
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
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4.0f, 2.0f)
				[
					SNew(SBox)
					.WidthOverride(110.0f)
					[
						SNew(SButton)
						.IsEnabled(bCanPreviewUnpack && !bUnpackPending)
						.Text(FText::FromString(bUnpackPending ? TEXT("Unpacked") : TEXT("Unpack")))
						.OnClicked_Lambda([this, ChildIndex]()
						{
							PreviewFlattenChild(ChildIndex);
							return FReply::Handled();
						})
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 6.0f, 0.0f, 0.0f)
			[
				SNew(SBorder)
				.Visibility(ChildIndex + 1 < ChildEntries.Num() ? EVisibility::Visible : EVisibility::Collapsed)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor(0.25f, 0.25f, 0.25f, 0.7f))
				.Padding(FMargin(0.0f, 0.5f))
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
	if (!CadImportStringUtils::TryParseMasterChildActorTypeString(SelectedType, ParsedType, true))
	{
		return;
	}

	ChildEntries[ChildIndex].ActorType = ParsedType;
}

void SCadWorkflowWizard::SetFlattenBranchSelected(const int32 BranchIndex, const bool bSelected)
{
	if (!FlattenBranchSelections.IsValidIndex(BranchIndex))
	{
		return;
	}

	FlattenBranchSelections[BranchIndex] = bSelected;
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
	if (StepIndex == 4)
	{
		StopJointTestAnimation(true);
		ClearJointPreviewLines();
	}

	const int32 NextStep = (StepIndex == 3) ? 1 : FMath::Max(0, StepIndex - 1);
	if (SavedVisibility.Num() > 0 && NextStep < 3)
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
	BaseChildEntries = SelectionResult.Children;
	ChildEntries.Reset();
	SavedVisibility.Reset();
	IsolatedIndex = INDEX_NONE;
	PendingFlattenBranchPaths.Reset();
	PreviewPromotedChildActorPaths.Reset();
	MasterJsonResult = FCadMasterJsonGenerationResult();
	ChildJsonResult = FCadChildJsonResult();
	BuildInput = FCadWorkflowBuildInput();
	LastBuildReplaceResult = FCadLevelReplaceResult();
	bCanRevertLastBuild = false;
	ResetJointEditorState();
	CleanupDryRunPreviewFolder();
	RefreshFlattenBranchCandidates();
	RebuildChildEntriesFromFlattenPreview();
	RebuildChildRows();

	SetStep(3);
	const AActor* ConfirmedMasterActor = ConfirmedSelection.MasterActor.Get();
	SetStatus(FString::Printf(
		TEXT("Master actor confirmed.\nmaster=%s\nchildren=%d\n3단계에서 Show / Type / Unpack preview를 설정하세요."),
		ConfirmedMasterActor ? *ConfirmedMasterActor->GetActorNameOrLabel() : TEXT("(none)"),
		ChildEntries.Num()));
	return FReply::Handled();
}

FReply SCadWorkflowWizard::ApplyFlattenAndContinue()
{
	return ProceedToJointSetup();
}

FReply SCadWorkflowWizard::ResetFlattenPreview()
{
	if (SavedVisibility.Num() > 0)
	{
		RestoreChildVisibilityState();
	}

	PendingFlattenBranchPaths.Reset();
	PreviewPromotedChildActorPaths.Reset();
	SavedVisibility.Reset();
	IsolatedIndex = INDEX_NONE;
	ChildEntries.Reset();
	RebuildChildEntriesFromFlattenPreview();
	RebuildChildRows();
	SetStatus(TEXT("Unpack preview was reset to the original direct-child list."));
	return FReply::Handled();
}

FReply SCadWorkflowWizard::ProceedToJointSetup()
{
	FString Error;
	if (!ApplyPendingFlattenPreview(Error))
	{
		SetStatus(FString::Printf(TEXT("Joint setup step failed during unpack apply:\n%s"), *Error));
		return FReply::Handled();
	}

	FCadMasterSelection SelectionForGeneration;
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
	SetStep(4);
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
	SetStep(5);
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
	if (!Runner->BuildFromWorkflow(BuildInput, &ReplaceResult))
	{
		LastBuildReplaceResult = FCadLevelReplaceResult();
		bCanRevertLastBuild = false;
		SetStatus(TEXT("Build failed. Check message log for parser/import/replacement errors."));
		return FReply::Handled();
	}

	LastBuildReplaceResult = ReplaceResult;
	bCanRevertLastBuild = ReplaceResult.bUsedTransaction;
	SetStep(6);
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
	SetStep(5);
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
	BaseChildEntries.Reset();
	FlattenBranchStats.Reset();
	FlattenBranchSelections.Reset();
	FlattenPreviewText = TEXT("Confirm master actor first.");
	PendingFlattenBranchPaths.Reset();
	FlattenableChildActorPaths.Reset();
	PreviewPromotedChildActorPaths.Reset();
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
	RebuildFlattenRows();
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
	StepIndex = FMath::Clamp(InStepIndex, 0, 6);
}

