#pragma once

#include "CoreMinimal.h"
#include "WorkflowTypes.h"

namespace CadImportStringUtils
{
	FString ToMasterChildActorTypeString(ECadMasterChildActorType ActorType);
	bool TryParseMasterChildActorTypeString(
		const FString& RawType,
		ECadMasterChildActorType& OutType,
		bool bAllowNone = true);
	FString ToMasterNodeTypeString(ECadMasterNodeType NodeType);
	bool TryParseMasterNodeTypeString(const FString& RawType, ECadMasterNodeType& OutType);
	FString ToJointTypeString(ECadImportJointType JointType);
	bool TryParseJointTypeString(const FString& RawType, ECadImportJointType& OutType);
	FString ToImportModelProfileString(ECadImportModelProfile Profile);
	FString ToJointDriveModeString(ECadImportJointDriveMode Mode);
}
