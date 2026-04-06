#include "CadImportStringUtils.h"

namespace CadImportStringUtils
{
	FString ToMasterChildActorTypeString(const ECadMasterChildActorType ActorType)
	{
		switch (ActorType)
		{
		case ECadMasterChildActorType::None:
			return TEXT("none");
		case ECadMasterChildActorType::Background:
			return TEXT("background");
		case ECadMasterChildActorType::Movable:
			return TEXT("movable");
		case ECadMasterChildActorType::Static:
		default:
			return TEXT("static");
		}
	}

	bool TryParseMasterChildActorTypeString(
		const FString& RawType,
		ECadMasterChildActorType& OutType,
		const bool bAllowNone)
	{
		if (bAllowNone && RawType.Equals(TEXT("none"), ESearchCase::IgnoreCase))
		{
			OutType = ECadMasterChildActorType::None;
			return true;
		}

		if (RawType.Equals(TEXT("background"), ESearchCase::IgnoreCase))
		{
			OutType = ECadMasterChildActorType::Background;
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

	FString ToJointTypeString(const ECadImportJointType JointType)
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

	bool TryParseJointTypeString(const FString& RawType, ECadImportJointType& OutType)
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

	FString ToImportModelProfileString(const ECadImportModelProfile Profile)
	{
		switch (Profile)
		{
		case ECadImportModelProfile::FixedAssembly:
			return TEXT("fixed_assembly");
		case ECadImportModelProfile::DynamicRobot:
		default:
			return TEXT("dynamic_robot");
		}
	}

	FString ToJointDriveModeString(const ECadImportJointDriveMode Mode)
	{
		switch (Mode)
		{
		case ECadImportJointDriveMode::None:
			return TEXT("none");
		case ECadImportJointDriveMode::Velocity:
			return TEXT("velocity");
		case ECadImportJointDriveMode::Position:
		default:
			return TEXT("position");
		}
	}
}
