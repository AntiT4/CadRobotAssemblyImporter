// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Hyoseung

#include "CadImporterEditor.h"
#include "ImportRunner.h"
#include "UI/MasterWorkflowWizard.h"
#include "Framework/Application/SlateApplication.h"
#include "ToolMenus.h"
#include "Widgets/SWindow.h"

DEFINE_LOG_CATEGORY(LogCadImporter);

#define LOCTEXT_NAMESPACE "FCadImporterEditorModule"

void FCadImporterEditorModule::StartupModule()
{
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FCadImporterEditorModule::RegisterMenus));
}

void FCadImporterEditorModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	ImportService.Reset();
}

void FCadImporterEditorModule::RegisterMenus()
{
	// Owner will be used for cleanup in call to UToolMenus::UnregisterOwner
	FToolMenuOwnerScoped OwnerScoped(this);
	const FUIAction Action = FUIAction(FExecuteAction::CreateRaw(this, &FCadImporterEditorModule::OpenWorkflowWindow));

	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
		{
			FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
			Section.AddMenuEntry(
				"CadImporterAction",
				LOCTEXT("CadImporterMenuLabel", "CAD Master Workflow"),
				LOCTEXT("CadImporterMenuTooltip", "Open popup wizard for workspace, master json, child json, and build."),
				FSlateIcon(),
				Action);
		}
	}

	{
		UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
		{
			FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("PluginTools");
			{
				FToolMenuEntry Entry = FToolMenuEntry::InitToolBarButton(
					"CadImporterAction",
					Action,
					LOCTEXT("CadImporterToolbarLabel", "CAD Master Workflow"),
					LOCTEXT("CadImporterToolbarTooltip", "Open popup wizard for the master JSON workflow."),
					FSlateIcon());
				Section.AddEntry(Entry);
			}
		}
	}
}

void FCadImporterEditorModule::OpenWorkflowWindow()
{
	if (!FSlateApplication::IsInitialized())
	{
		UE_LOG(LogCadImporter, Warning, TEXT("Slate application is not initialized; cannot open workflow popup."));
		return;
	}

	if (!ImportService.IsValid())
	{
		ImportService = MakeShared<FCadImportService>();
	}

	TSharedRef<SWindow> WizardWindow = SNew(SWindow)
		.Title(LOCTEXT("MasterWorkflowWindowTitle", "CAD Master Workflow Wizard"))
		.ClientSize(FVector2D(860.0f, 680.0f))
		.SupportsMinimize(false)
		.SupportsMaximize(false)
		[
			SNew(SCadWorkflowWizard)
			.Runner(ImportService)
		];

	FSlateApplication::Get().AddWindow(WizardWindow);
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FCadImporterEditorModule, CadImporterEditor)
