#include "UI/MasterWorkflowWizard.h"

#include "ImportRunner.h"
#include "CadImporterEditor.h"
#include "DesktopPlatformModule.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "IDesktopPlatform.h"
#include "Misc/Paths.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	FString MasterChildActorTypeToUiString(const ECadMasterChildActorType ActorType)
	{
		return ActorType == ECadMasterChildActorType::Movable ? TEXT("movable") : TEXT("static");
	}

	bool TryParseMasterChildActorTypeFromUiString(const FString& RawType, ECadMasterChildActorType& OutType)
	{
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

	void CollectActorHierarchyForVisibility(AActor* RootActor, TArray<AActor*>& OutActors)
	{
		OutActors.Reset();
		if (!RootActor)
		{
			return;
		}

		OutActors.Add(RootActor);

		TArray<AActor*> AttachedActors;
		RootActor->GetAttachedActors(AttachedActors, true, true);
		for (AActor* AttachedActor : AttachedActors)
		{
			if (AttachedActor)
			{
				OutActors.Add(AttachedActor);
			}
		}
	}

	struct FAnyDirectoryEntryVisitor final : IPlatformFile::FDirectoryVisitor
	{
		bool bFoundAnyEntry = false;

		virtual bool Visit(const TCHAR* FilenameOrDirectory, bool bIsDirectory) override
		{
			static_cast<void>(FilenameOrDirectory);
			static_cast<void>(bIsDirectory);
			bFoundAnyEntry = true;
			return false;
		}
	};

	bool TryNormalizeWorkspacePath(const FString& WorkspaceInput, FString& OutTrimmedWorkspace, FString& OutNormalizedWorkspace, FString& OutError)
	{
		OutTrimmedWorkspace.Reset();
		OutNormalizedWorkspace.Reset();
		OutError.Reset();

		OutTrimmedWorkspace = WorkspaceInput.TrimStartAndEnd();
		if (OutTrimmedWorkspace.IsEmpty())
		{
			OutError = TEXT("Workspace path is empty. Apply a valid workspace folder first.");
			return false;
		}

		FString NormalizedWorkspace = OutTrimmedWorkspace;
		if (FPaths::IsRelative(NormalizedWorkspace))
		{
			NormalizedWorkspace = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), NormalizedWorkspace));
		}
		else
		{
			NormalizedWorkspace = FPaths::ConvertRelativePathToFull(NormalizedWorkspace);
		}
		FPaths::NormalizeDirectoryName(NormalizedWorkspace);

		if (NormalizedWorkspace.IsEmpty())
		{
			OutError = TEXT("Workspace path normalization produced an empty value.");
			return false;
		}

		OutNormalizedWorkspace = MoveTemp(NormalizedWorkspace);
		return true;
	}

	bool TryValidateWorkspaceForApply(const FString& WorkspaceInput, FString& OutNormalizedWorkspace, FString& OutError)
	{
		FString TrimmedWorkspace;
		if (!TryNormalizeWorkspacePath(WorkspaceInput, TrimmedWorkspace, OutNormalizedWorkspace, OutError))
		{
			return false;
		}

		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		const bool bWorkspaceDirectoryExists = PlatformFile.DirectoryExists(*OutNormalizedWorkspace);

		bool bWorkspaceDirectoryEmpty = true;
		if (bWorkspaceDirectoryExists)
		{
			FAnyDirectoryEntryVisitor Visitor;
			PlatformFile.IterateDirectory(*OutNormalizedWorkspace, Visitor);
			bWorkspaceDirectoryEmpty = !Visitor.bFoundAnyEntry;
		}

		UE_LOG(
			LogCadImporter,
			Display,
			TEXT("Workspace validation before apply: raw='%s', trimmed='%s', normalized='%s', exists=%s, empty=%s"),
			*WorkspaceInput,
			*TrimmedWorkspace,
			*OutNormalizedWorkspace,
			bWorkspaceDirectoryExists ? TEXT("true") : TEXT("false"),
			bWorkspaceDirectoryEmpty ? TEXT("true") : TEXT("false"));

		if (bWorkspaceDirectoryExists && !bWorkspaceDirectoryEmpty)
		{
			OutError = FString::Printf(
				TEXT("Workspace folder is not empty. Choose an empty folder or create a new one.\npath=%s"),
				*OutNormalizedWorkspace);
			return false;
		}

		return true;
	}

	bool TryValidateWorkspaceForGeneration(const FString& WorkspaceInput, FString& OutNormalizedWorkspace, FString& OutError)
	{
		FString TrimmedWorkspace;
		if (!TryNormalizeWorkspacePath(WorkspaceInput, TrimmedWorkspace, OutNormalizedWorkspace, OutError))
		{
			return false;
		}

		const bool bWorkspaceDirectoryExists = IFileManager::Get().DirectoryExists(*OutNormalizedWorkspace);
		UE_LOG(
			LogCadImporter,
			Display,
			TEXT("Workspace validation before generation: raw='%s', trimmed='%s', normalized='%s', exists=%s"),
			*WorkspaceInput,
			*TrimmedWorkspace,
			*OutNormalizedWorkspace,
			bWorkspaceDirectoryExists ? TEXT("true") : TEXT("false"));

		return true;
	}
}

void SCadWorkflowWizard::Construct(const FArguments& InArgs)
{
	Runner = InArgs._Runner;
	WorkspaceFolder = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
	StatusMessage = TEXT("Step 1: Set workspace folder.");
	SelectionPreviewText = TEXT("선택된 액터 없음");
	StepIndex = 0;
	ChildTypeItems.Reset();
	ChildTypeItems.Add(MakeShared<FString>(TEXT("static")));
	ChildTypeItems.Add(MakeShared<FString>(TEXT("movable")));
	ImportOptions = FCadFbxImportOptions();
	ImportOptions.bShowDialog = false;
	SelectionKey.Reset();
	RegisterActiveTimer(1.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SCadWorkflowWizard::PollSelection));

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
						TEXT("1/4 Workspace"),
						TEXT("2/4 Confirm Master Actor"),
						TEXT("3/4 Child Type + Generate JSON"),
						TEXT("4/4 Build Actor"),
						TEXT("Completed")
					};
					const int32 SafeIndex = FMath::Clamp(StepIndex, 0, 4);
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
						.Text(FText::FromString(TEXT("Set child actor types in UI, then generate master/child json files.")))
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
							.Text(FText::FromString(TEXT("Generate JSON")))
							.OnClicked(this, &SCadWorkflowWizard::GenerateWorkflowJson)
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
						SNew(SButton)
						.Text(FText::FromString(TEXT("Restart Workflow")))
						.OnClicked(this, &SCadWorkflowWizard::RestartWorkflow)
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
	SingleSelectedActor->GetAttachedActors(DirectChildren, false, false);
	DirectChildren.Sort([](const AActor& Left, const AActor& Right)
	{
		return Left.GetActorNameOrLabel() < Right.GetActorNameOrLabel();
	});

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

AActor* SCadWorkflowWizard::FindChildActor(const FCadChildEntry& ChildEntry) const
{
	if (ChildEntry.ActorPath.TrimStartAndEnd().IsEmpty())
	{
		return nullptr;
	}

	return FindObject<AActor>(nullptr, *ChildEntry.ActorPath);
}

void SCadWorkflowWizard::SaveChildVisibility()
{
	SavedVisibility.Reset();
	TArray<AActor*> HierarchyActors;

	for (const FCadChildEntry& ChildEntry : ChildEntries)
	{
		AActor* ChildActor = FindChildActor(ChildEntry);
		if (!ChildActor)
		{
			continue;
		}

		CollectActorHierarchyForVisibility(ChildActor, HierarchyActors);
		for (AActor* HierarchyActor : HierarchyActors)
		{
			if (!HierarchyActor)
			{
				continue;
			}

			const FString ActorPath = HierarchyActor->GetPathName();
			if (SavedVisibility.Contains(ActorPath))
			{
				continue;
			}

			SavedVisibility.Add(ActorPath, HierarchyActor->IsTemporarilyHiddenInEditor());
		}
	}
}

void SCadWorkflowWizard::IsolateChildVisibility(const int32 ChildIndex)
{
	TArray<AActor*> HierarchyActors;

	for (int32 Index = 0; Index < ChildEntries.Num(); ++Index)
	{
		AActor* ChildActor = FindChildActor(ChildEntries[Index]);
		if (!ChildActor)
		{
			continue;
		}

		const bool bHideInEditor = (Index != ChildIndex);
		CollectActorHierarchyForVisibility(ChildActor, HierarchyActors);
		for (AActor* HierarchyActor : HierarchyActors)
		{
			if (HierarchyActor)
			{
				HierarchyActor->SetIsTemporarilyHiddenInEditor(bHideInEditor);
			}
		}
	}

	if (GEditor)
	{
		GEditor->RedrawAllViewports(false);
	}
}

void SCadWorkflowWizard::RestoreChildVisibilityState()
{
	for (const TPair<FString, bool>& SnapshotEntry : SavedVisibility)
	{
		AActor* Actor = FindObject<AActor>(nullptr, *SnapshotEntry.Key);
		if (!Actor)
		{
			continue;
		}

		Actor->SetIsTemporarilyHiddenInEditor(SnapshotEntry.Value);
	}

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
	if (!TryValidateWorkspaceForApply(WorkspaceFolder, NormalizedWorkspace, WorkspaceValidationError))
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
	if (!TryValidateWorkspaceForGeneration(WorkspaceFolder, NormalizedWorkspace, WorkspaceValidationError))
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
	if (!CadMasterSelection::TryCollectFromSelection(SelectionResult, Error))
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
	RebuildChildRows();

	const AActor* ConfirmedMasterActor = ConfirmedSelection.MasterActor.Get();
	SetStep(2);
	SetStatus(FString::Printf(
		TEXT("Master actor confirmed.\nmaster=%s\nchildren=%d\n다음 단계에서 child actor_type을 선택 후 Generate JSON을 실행하세요."),
		ConfirmedMasterActor ? *ConfirmedMasterActor->GetActorNameOrLabel() : TEXT("(none)"),
		ChildEntries.Num()));
	return FReply::Handled();
}

FReply SCadWorkflowWizard::GenerateWorkflowJson()
{
	if (!ConfirmedSelection.IsValid())
	{
		SetStatus(TEXT("Generate JSON failed: confirm master actor first."));
		return FReply::Handled();
	}

	if (ChildEntries.Num() == 0)
	{
		SetStatus(TEXT("Generate JSON failed: confirmed master has no direct children."));
		return FReply::Handled();
	}

	FString NormalizedWorkspace;
	FString WorkspaceValidationError;
	if (!TryValidateWorkspaceForGeneration(WorkspaceFolder, NormalizedWorkspace, WorkspaceValidationError))
	{
		SetStatus(FString::Printf(TEXT("Generate JSON failed:\n%s"), *WorkspaceValidationError));
		return FReply::Handled();
	}

	WorkspaceFolder = MoveTemp(NormalizedWorkspace);
	if (WorkspaceTextBox.IsValid())
	{
		WorkspaceTextBox->SetText(FText::FromString(WorkspaceFolder));
	}

	FCadMasterSelection SelectionForGeneration = ConfirmedSelection;
	SelectionForGeneration.Children = ChildEntries;

	FString Error;
	if (!CadMasterJsonGenerator::TryGenerateAndWriteFromSelectionResult(SelectionForGeneration, WorkspaceFolder, MasterJsonResult, Error))
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

	if (!CadChildJsonService::TryExtractChildJsonFilesFromDocument(
		MasterJsonPath,
		MasterJsonResult.Document,
		ChildJsonResult,
		Error))
	{
		SetStatus(FString::Printf(TEXT("Generate JSON failed while creating child json files:\n%s"), *Error));
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

	SetStep(3);
	SetStatus(FString::Printf(
		TEXT("Master/Child JSON generated.\nmaster=%s\nchild_count=%d\nchild_folder=%s"),
		*MasterJsonResult.WorkspacePaths.MasterJsonPath,
		ChildJsonResult.GeneratedChildJsonPaths.Num(),
		*ChildJsonResult.ChildJsonFolderPath));
	return FReply::Handled();
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

	if (!Runner->BuildFromWorkflow(BuildInput, ImportOptions))
	{
		SetStatus(TEXT("Build failed. Check message log for parser/import/replacement errors."));
		return FReply::Handled();
	}

	SetStep(4);
	SetStatus(TEXT("Build completed and level replacement executed."));
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
	StepIndex = FMath::Clamp(InStepIndex, 0, 4);
}
