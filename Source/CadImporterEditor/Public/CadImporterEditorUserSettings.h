#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "CadImporterEditorUserSettings.generated.h"

UCLASS(config=EditorPerProjectUserSettings)
class CADIMPORTEREDITOR_API UCadImporterEditorUserSettings : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, config, Category="Joint Preview")
	bool bDrawJointPreviewLines = true;

	UPROPERTY(EditAnywhere, config, Category="Joint Preview")
	bool bDrawJointPreviewLinesInForeground = true;

	UPROPERTY(EditAnywhere, config, Category="Joint Preview", meta=(ClampMin="0.1", UIMin="0.1"))
	float JointPreviewLineThickness = 4.0f;

	UPROPERTY(EditAnywhere, config, Category="Joint Preview")
	FLinearColor FixedJointPreviewColor = FLinearColor(0.63f, 0.63f, 0.63f, 1.0f);

	UPROPERTY(EditAnywhere, config, Category="Joint Preview")
	FLinearColor RevoluteJointPreviewColor = FLinearColor(1.0f, 0.67f, 0.0f, 1.0f);

	UPROPERTY(EditAnywhere, config, Category="Joint Preview")
	FLinearColor PrismaticJointPreviewColor = FLinearColor(0.0f, 0.78f, 1.0f, 1.0f);
};
