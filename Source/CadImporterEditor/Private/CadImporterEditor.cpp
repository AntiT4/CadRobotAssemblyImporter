// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Hyoseung

#include "CadImporterEditor.h"
#include "ImportService.h"
#include "UI/WorkflowWizard.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Application/SlateApplication.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

DEFINE_LOG_CATEGORY(LogCadImporter);

#define LOCTEXT_NAMESPACE "FCadImporterEditorModule"

namespace
{
const FName WorkflowTabName(TEXT("CadImporterEditor.Workflow"));
}

void FCadImporterEditorModule::StartupModule()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		WorkflowTabName,
		FOnSpawnTab::CreateRaw(this, &FCadImporterEditorModule::SpawnWorkflowTab))
		.SetDisplayName(LOCTEXT("CadImporterTabLabel", "CAD Master Workflow"))
		.SetTooltipText(LOCTEXT("CadImporterTabTooltip", "Open the dockable CAD master workflow tab."))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FCadImporterEditorModule::RegisterMenus));
}

void FCadImporterEditorModule::ShutdownModule()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(WorkflowTabName);
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	ImportService.Reset();
}

void FCadImporterEditorModule::RegisterMenus()
{
	// Owner will be used for cleanup in call to UToolMenus::UnregisterOwner
	FToolMenuOwnerScoped OwnerScoped(this);
	const FUIAction Action = FUIAction(FExecuteAction::CreateRaw(this, &FCadImporterEditorModule::OpenWorkflowTab));

	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
		{
			FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
			Section.AddMenuEntry(
				"CadImporterAction",
				LOCTEXT("CadImporterMenuLabel", "CAD Master Workflow"),
				LOCTEXT("CadImporterMenuTooltip", "Open the dockable workflow tab for workspace, master JSON, child JSON, and build."),
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
					LOCTEXT("CadImporterToolbarTooltip", "Open the dockable tab for the master JSON workflow."),
					FSlateIcon());
				Section.AddEntry(Entry);
			}
		}
	}
}

void FCadImporterEditorModule::OpenWorkflowTab()
{
	if (!FSlateApplication::IsInitialized())
	{
		UE_LOG(LogCadImporter, Warning, TEXT("Slate application is not initialized; cannot open workflow tab."));
		return;
	}

	FGlobalTabmanager::Get()->TryInvokeTab(WorkflowTabName);
}

TSharedRef<SDockTab> FCadImporterEditorModule::SpawnWorkflowTab(const FSpawnTabArgs& /*Args*/)
{
	if (!ImportService.IsValid())
	{
		ImportService = MakeShared<FCadImportService>();
	}

	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.Label(LOCTEXT("MasterWorkflowWindowTitle", "CAD Master Workflow"))
		[
			SNew(SCadWorkflowWizard)
			.Runner(ImportService)
		];
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FCadImporterEditorModule, CadImporterEditor)
