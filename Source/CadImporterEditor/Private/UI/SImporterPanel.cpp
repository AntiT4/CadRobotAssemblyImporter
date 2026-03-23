#include "UI/SImporterPanel.h"

#include "UI/ActorToolsTab.h"
#include "UI/DialogUtils.h"
#include "UI/ImportTab.h"
#include "UI/JsonPreview.h"
#include "UI/MasterWorkflowWizard.h"
#include "ImportRunner.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/MessageDialog.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"

void SCadImporterPanel::Construct(const FArguments& InArgs)
{
	Runner = MakeShared<FCadImporterRunner>();

	SelectedJsonPath = MakeShared<FString>();
	LinkPreviewText = MakeShared<FString>(TEXT("Preview will appear here after loading config."));
	InspectorText = MakeShared<FString>(TEXT("Select one actor in the level, switch to Actor Inspector, then click Refresh From Selection."));
	JsonPreviewText = MakeShared<FString>(TEXT("Select one actor in the level, switch to JSON Preview, then click Refresh Preview."));
	bHasValidPreview = MakeShared<bool>(false);
	ActiveTabIndex = MakeShared<int32>(0);
	SelectedImportOptions = MakeShared<FCadFbxImportOptions>();

	UniformScaleText = MakeShared<FString>();
	TranslationXText = MakeShared<FString>();
	TranslationYText = MakeShared<FString>();
	TranslationZText = MakeShared<FString>();
	RotationPitchText = MakeShared<FString>();
	RotationYawText = MakeShared<FString>();
	RotationRollText = MakeShared<FString>();

	CadImportDialogUtils::FillImportOptionTextFields(
		*SelectedImportOptions,
		*UniformScaleText,
		*TranslationXText,
		*TranslationYText,
		*TranslationZText,
		*RotationPitchText,
		*RotationYawText,
		*RotationRollText);

	FCadImportTabBuildArgs ImportTabArgs(
		[this](FString& PickedPath)
		{
			return Runner.IsValid() && Runner->SelectJsonFile(PickedPath);
		},
		[this]()
		{
			HandleRunImport();
		},
		[this]()
		{
			HandleClearImportState();
		},
		SelectedJsonPath.ToSharedRef(),
		LinkPreviewText.ToSharedRef(),
		bHasValidPreview.ToSharedRef(),
		SelectedImportOptions.ToSharedRef(),
		UniformScaleText.ToSharedRef(),
		TranslationXText.ToSharedRef(),
		TranslationYText.ToSharedRef(),
		TranslationZText.ToSharedRef(),
		RotationPitchText.ToSharedRef(),
		RotationYawText.ToSharedRef(),
		RotationRollText.ToSharedRef());

	const TSharedRef<SWidget> ImportTabContent = CadImportTabBuilder::BuildImportTabContent(ImportTabArgs);
	const TSharedRef<SWidget> ActorToolsTabContent = CadActorToolsTab::BuildActorToolsTabContent(
		FCadActorToolsTabArgs(
			InspectorText.ToSharedRef(),
			JsonPreviewText.ToSharedRef(),
			[this]()
			{
				HandleSaveSelectionJson();
			}));

	ChildSlot
	[
		SNew(SBorder)
		.Padding(12.0f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 12.0f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Import")))
					.OnClicked_Lambda([ActiveTabIndex = ActiveTabIndex]()
					{
						*ActiveTabIndex = 0;
						return FReply::Handled();
					})
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Actor Tools")))
					.OnClicked_Lambda([ActiveTabIndex = ActiveTabIndex]()
					{
						*ActiveTabIndex = 1;
						return FReply::Handled();
					})
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(8.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Master Workflow Wizard")))
					.OnClicked_Lambda([this]()
					{
						if (!Runner.IsValid())
						{
							Runner = MakeShared<FCadImporterRunner>();
						}

						TSharedRef<SWindow> WizardWindow = SNew(SWindow)
							.Title(FText::FromString(TEXT("CAD Master Workflow Wizard")))
							.ClientSize(FVector2D(860.0f, 680.0f))
							.SupportsMinimize(false)
							.SupportsMaximize(false)
							[
								SNew(SCadMasterWorkflowWizard)
								.Runner(Runner)
							];

						FSlateApplication::Get().AddWindow(WizardWindow);
						return FReply::Handled();
					})
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 12.0f)
			[
				SNew(SSeparator)
				.Orientation(Orient_Horizontal)
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SWidgetSwitcher)
				.WidgetIndex_Lambda([ActiveTabIndex = ActiveTabIndex]()
				{
					return *ActiveTabIndex;
				})
				+ SWidgetSwitcher::Slot()
				[
					ImportTabContent
				]
				+ SWidgetSwitcher::Slot()
				[
					ActorToolsTabContent
				]
			]
		]
	];
}

void SCadImporterPanel::HandleRunImport()
{
	if (Runner.IsValid() && SelectedJsonPath.IsValid() && !SelectedJsonPath->IsEmpty())
	{
		Runner->RunImport(*SelectedJsonPath, *SelectedImportOptions);
	}
}

void SCadImporterPanel::HandleClearImportState()
{
	if (SelectedJsonPath.IsValid())
	{
		SelectedJsonPath->Reset();
	}

	if (LinkPreviewText.IsValid())
	{
		*LinkPreviewText = TEXT("Preview will appear here after loading config.");
	}

	if (JsonPreviewText.IsValid())
	{
		*JsonPreviewText = TEXT("Select one actor in the level, switch to JSON Preview, then click Refresh Preview.");
	}

	if (bHasValidPreview.IsValid())
	{
		*bHasValidPreview = false;
	}

	if (SelectedImportOptions.IsValid())
	{
		*SelectedImportOptions = FCadFbxImportOptions();
	}

	CadImportDialogUtils::FillImportOptionTextFields(
		*SelectedImportOptions,
		*UniformScaleText,
		*TranslationXText,
		*TranslationYText,
		*TranslationZText,
		*RotationPitchText,
		*RotationYawText,
		*RotationRollText);
}

void SCadImporterPanel::HandleSaveSelectionJson()
{
	if (!Runner.IsValid())
	{
		return;
	}

	FString OutputPath;
	if (!Runner->SelectOutputJsonFile(OutputPath))
	{
		return;
	}

	FString Error;
	if (CadJsonPreview::TrySaveSelectionJson(OutputPath, Error))
	{
		if (JsonPreviewText.IsValid())
		{
			FString PreviewText;
			FString PreviewError;
			if (CadJsonPreview::TryBuildSelectionJsonPreview(PreviewText, PreviewError))
			{
				*JsonPreviewText = MoveTemp(PreviewText);
			}
		}

		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString::Printf(TEXT("Saved JSON:\n%s"), *OutputPath)));
		return;
	}

	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString::Printf(TEXT("Failed to save JSON:\n%s"), *Error)));
}
