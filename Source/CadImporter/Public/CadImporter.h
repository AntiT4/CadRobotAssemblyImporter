#pragma once

#include "Modules/ModuleManager.h"

class FCadImporterModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
