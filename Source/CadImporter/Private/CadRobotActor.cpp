#include "CadRobotActor.h"

#include "CadMasterActor.h"
#include "Components/PrimitiveComponent.h"
#include "Containers/StringConv.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "JsonObjectConverter.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

DEFINE_LOG_CATEGORY_STATIC(LogCadRobotIO, Log, All);

namespace
{
	constexpr TCHAR CadJointPrefix[] = TEXT("Joint_");

	ECadRobotResolvedJointType ResolveJointType(
		const UPhysicsConstraintComponent& Constraint,
		ECadRobotResolvedAngularAxis& OutAngularAxis,
		ECadRobotResolvedLinearAxis& OutLinearAxis)
	{
		const FConstraintInstance& ConstraintInstance = Constraint.ConstraintInstance;

		if (ConstraintInstance.GetAngularTwistMotion() != EAngularConstraintMotion::ACM_Locked)
		{
			OutAngularAxis = ECadRobotResolvedAngularAxis::Twist;
			return ECadRobotResolvedJointType::Revolute;
		}

		if (ConstraintInstance.GetAngularSwing1Motion() != EAngularConstraintMotion::ACM_Locked)
		{
			OutAngularAxis = ECadRobotResolvedAngularAxis::Swing1;
			return ECadRobotResolvedJointType::Revolute;
		}

		if (ConstraintInstance.GetAngularSwing2Motion() != EAngularConstraintMotion::ACM_Locked)
		{
			OutAngularAxis = ECadRobotResolvedAngularAxis::Swing2;
			return ECadRobotResolvedJointType::Revolute;
		}

		if (ConstraintInstance.GetLinearXMotion() != ELinearConstraintMotion::LCM_Locked)
		{
			OutLinearAxis = ECadRobotResolvedLinearAxis::X;
			return ECadRobotResolvedJointType::Prismatic;
		}

		if (ConstraintInstance.GetLinearYMotion() != ELinearConstraintMotion::LCM_Locked)
		{
			OutLinearAxis = ECadRobotResolvedLinearAxis::Y;
			return ECadRobotResolvedJointType::Prismatic;
		}

		if (ConstraintInstance.GetLinearZMotion() != ELinearConstraintMotion::LCM_Locked)
		{
			OutLinearAxis = ECadRobotResolvedLinearAxis::Z;
			return ECadRobotResolvedJointType::Prismatic;
		}

		return ECadRobotResolvedJointType::Fixed;
	}

	int32 FindFrameDelimiter(const TArray<uint8>& Buffer)
	{
		for (int32 Index = 0; Index < Buffer.Num(); ++Index)
		{
			if (Buffer[Index] == static_cast<uint8>('\n'))
			{
				return Index;
			}
		}

		return INDEX_NONE;
	}

	void WakeConstraintBodies(UPhysicsConstraintComponent& ConstraintComponent)
	{
		UPrimitiveComponent* Component1 = nullptr;
		UPrimitiveComponent* Component2 = nullptr;
		FName BoneName1 = NAME_None;
		FName BoneName2 = NAME_None;
		ConstraintComponent.GetConstrainedComponents(Component1, BoneName1, Component2, BoneName2);

		if (Component1)
		{
			if (BoneName1 != NAME_None)
			{
				Component1->WakeRigidBody(BoneName1);
			}
			else
			{
				Component1->WakeAllRigidBodies();
			}
		}

		if (Component2)
		{
			if (BoneName2 != NAME_None)
			{
				Component2->WakeRigidBody(BoneName2);
			}
			else
			{
				Component2->WakeAllRigidBodies();
			}
		}
	}
}

ACadRobotActor::ACadRobotActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void ACadRobotActor::BeginPlay()
{
	Super::BeginPlay();

	DiscoverRobotJoints();

	if (bEnableSocketIO && bAutoConnect)
	{
		ConnectIO();
	}
}

void ACadRobotActor::Tick(float DeltaSeconds)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(CadRobotActor_Tick);
	Super::Tick(DeltaSeconds);

	if (ResolvedJoints.IsEmpty())
	{
		DiscoverRobotJoints();
	}

	TryReconnectIfNeeded();
	ApplyQueuedCommandIfAny();

	RunController(DeltaSeconds);
	UpdateStatus(DeltaSeconds);

	if (bEnableSocketIO && bPublishStatusEveryTick)
	{
		PublishStatus();
	}
}

void ACadRobotActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DisconnectIO();

	Super::EndPlay(EndPlayReason);
}

bool ACadRobotActor::ApplyCommand(const FCadRobotCommand& InCommand)
{
	Command = InCommand;

	bool bAppliedAnyJoint = InCommand.Joints.IsEmpty();
	for (const FCadRobotJointCommand& JointCommand : InCommand.Joints)
	{
		const int32 JointIndex = FindResolvedJointIndex(JointCommand.JointName);
		if (!ResolvedJoints.IsValidIndex(JointIndex))
		{
			UE_LOG(LogCadRobotIO, Verbose, TEXT("Ignoring command for unresolved joint '%s'."), *JointCommand.JointName.ToString());
			continue;
		}

		ApplyJointTarget(ResolvedJoints[JointIndex], JointCommand.TargetPosition);
		bAppliedAnyJoint = true;
	}

	return bAppliedAnyJoint;
}

bool ACadRobotActor::RunController(float DeltaSeconds)
{
	static_cast<void>(DeltaSeconds);
	return ApplyCommand(Command);
}

bool ACadRobotActor::UpdateStatus(float DeltaSeconds)
{
	if (ResolvedJoints.IsEmpty())
	{
		DiscoverRobotJoints();
	}

	const UWorld* World = GetWorld();
	Status.TimestampSec = World ? static_cast<double>(World->GetTimeSeconds()) : 0.0;
	Status.ConnectionId = ActiveConnectionId;

	for (FCadRobotResolvedJoint& Joint : ResolvedJoints)
	{
		if (!Status.Joints.IsValidIndex(Joint.StatusIndex))
		{
			continue;
		}

		const float Position = ReadJointPosition(Joint);
		float Velocity = 0.0f;
		if (Joint.bHasLastPublishedPosition && DeltaSeconds > KINDA_SMALL_NUMBER)
		{
			Velocity = (Position - Joint.LastPublishedPosition) / DeltaSeconds;
		}

		FCadRobotJointStatus& JointStatus = Status.Joints[Joint.StatusIndex];
		JointStatus.JointName = Joint.PublishedName;
		JointStatus.JointPosition = Position;
		JointStatus.JointVelocity = Velocity;

		Joint.LastPublishedPosition = Position;
		Joint.bHasLastPublishedPosition = true;
	}

	return true;
}

bool ACadRobotActor::ConnectIO()
{
	if (!bEnableSocketIO || bSocketConnected || bSocketConnectInFlight)
	{
		return bSocketConnected;
	}

	if (!EnsureMasterActor())
	{
		return false;
	}

	int32 ConnectionId = INDEX_NONE;
	if (!MasterActor->ConnectRobot(this, ServerAddress, ServerPort, ReconnectIntervalSec, bAutoConnect, ConnectionId))
	{
		return false;
	}

	ActiveConnectionId = ConnectionId;
	bSocketConnected = (ConnectionId != INDEX_NONE) && MasterActor->IsRobotConnected(ConnectionId);
	bSocketConnectInFlight = (ConnectionId != INDEX_NONE) && !bSocketConnected;

	const UWorld* World = GetWorld();
	LastConnectionAttemptTimeSec = World ? static_cast<double>(World->GetTimeSeconds()) : 0.0;

	if (bSocketConnected)
	{
		UE_LOG(LogCadRobotIO, Log, TEXT("Robot IO connected immediately (connection_id=%d)."), ActiveConnectionId);
	}
	else if (bSocketConnectInFlight)
	{
		UE_LOG(LogCadRobotIO, Log, TEXT("Connecting robot IO to %s:%d (connection_id=%d)."), *ServerAddress, ServerPort, ActiveConnectionId);
	}

	return bSocketConnected || bSocketConnectInFlight;
}

void ACadRobotActor::DisconnectIO()
{
	bSocketConnectInFlight = false;
	bSocketConnected = false;
	bHasQueuedCommand = false;
	PendingReceiveBytes.Reset();

	if (MasterActor && IsValid(MasterActor) && ActiveConnectionId != INDEX_NONE)
	{
		MasterActor->DisconnectRobot(ActiveConnectionId);
	}

	ActiveConnectionId = INDEX_NONE;
}

bool ACadRobotActor::IsIOConnected() const
{
	if (!bSocketConnected || !MasterActor || !IsValid(MasterActor) || ActiveConnectionId == INDEX_NONE)
	{
		return false;
	}

	return MasterActor->IsRobotConnected(ActiveConnectionId);
}

void ACadRobotActor::NotifySocketConnected(int32 ConnectionId)
{
	if (ActiveConnectionId == INDEX_NONE)
	{
		ActiveConnectionId = ConnectionId;
	}

	if (ConnectionId != ActiveConnectionId)
	{
		return;
	}

	bSocketConnectInFlight = false;
	bSocketConnected = true;
	UE_LOG(LogCadRobotIO, Log, TEXT("Robot IO connected (connection_id=%d)."), ConnectionId);
}

void ACadRobotActor::NotifySocketDisconnected(int32 ConnectionId)
{
	if (ActiveConnectionId == INDEX_NONE)
	{
		ActiveConnectionId = ConnectionId;
	}

	if (ActiveConnectionId != INDEX_NONE && ConnectionId != ActiveConnectionId)
	{
		return;
	}

	UE_LOG(LogCadRobotIO, Warning, TEXT("Robot IO disconnected (connection_id=%d)."), ConnectionId);

	const bool bConnectionStillManaged = MasterActor && IsValid(MasterActor) && MasterActor->HasRobotConnection(ConnectionId);
	bSocketConnectInFlight = bAutoConnect && bConnectionStillManaged;
	bSocketConnected = false;
	if (!bSocketConnectInFlight)
	{
		ActiveConnectionId = INDEX_NONE;
	}
	PendingReceiveBytes.Reset();
}

void ACadRobotActor::NotifySocketMessageReceived(int32 ConnectionId, TArray<uint8>& Message)
{
	if (ActiveConnectionId == INDEX_NONE)
	{
		ActiveConnectionId = ConnectionId;
	}

	if (ConnectionId != ActiveConnectionId || Message.IsEmpty())
	{
		return;
	}

	PendingReceiveBytes.Append(Message);
	ProcessPendingReceiveBytes();
}

void ACadRobotActor::TryReconnectIfNeeded()
{
	if (!bEnableSocketIO || !bAutoConnect || bSocketConnected || bSocketConnectInFlight)
	{
		return;
	}

	const UWorld* World = GetWorld();
	const double NowSec = World ? static_cast<double>(World->GetTimeSeconds()) : 0.0;
	if (LastConnectionAttemptTimeSec < 0.0 || (NowSec - LastConnectionAttemptTimeSec) >= ReconnectIntervalSec)
	{
		ConnectIO();
	}
}

void ACadRobotActor::ApplyQueuedCommandIfAny()
{
	if (!bHasQueuedCommand)
	{
		return;
	}

	Command = QueuedCommand;
	bHasQueuedCommand = false;
}

void ACadRobotActor::ProcessPendingReceiveBytes()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(CadRobotActor_ProcessPendingReceiveBytes);

	FString CommandJson;
	while (TryExtractNextCommandFrame(CommandJson))
	{
		if (CommandJson.IsEmpty())
		{
			continue;
		}
		TryConsumeCommandFrame(CommandJson);
	}
}

bool ACadRobotActor::TryExtractNextCommandFrame(FString& OutCommandJson)
{
	OutCommandJson.Reset();

	const int32 DelimiterIndex = FindFrameDelimiter(PendingReceiveBytes);
	if (DelimiterIndex == INDEX_NONE)
	{
		return false;
	}

	TArray<uint8> FrameBytes;
	if (DelimiterIndex > 0)
	{
		FrameBytes.Append(PendingReceiveBytes.GetData(), DelimiterIndex);
	}

	PendingReceiveBytes.RemoveAt(0, DelimiterIndex + 1, EAllowShrinking::No);
	if (!FrameBytes.IsEmpty() && FrameBytes.Last() == static_cast<uint8>('\r'))
	{
		FrameBytes.Pop(EAllowShrinking::No);
	}

	if (FrameBytes.IsEmpty())
	{
		return true;
	}

	const FUTF8ToTCHAR Utf8Text(reinterpret_cast<const ANSICHAR*>(FrameBytes.GetData()), FrameBytes.Num());
	OutCommandJson.AppendChars(Utf8Text.Get(), Utf8Text.Length());
	return true;
}

bool ACadRobotActor::EnsureMasterActor()
{
	if (MasterActor && IsValid(MasterActor))
	{
		return true;
	}

	for (AActor* ParentActor = GetAttachParentActor(); ParentActor; ParentActor = ParentActor->GetAttachParentActor())
	{
		if (ACadMasterActor* ParentMasterActor = Cast<ACadMasterActor>(ParentActor))
		{
			MasterActor = ParentMasterActor;
			return true;
		}
	}

	for (TActorIterator<ACadMasterActor> It(GetWorld()); It; ++It)
	{
		if (IsValid(*It))
		{
			MasterActor = *It;
			break;
		}
	}

	if (!MasterActor)
	{
		UE_LOG(LogCadRobotIO, Warning, TEXT("No CadMasterActor found in the level for robot '%s'."), *GetName());
		return false;
	}

	return true;
}

void ACadRobotActor::DiscoverRobotJoints()
{
	ResolvedJoints.Reset();
	ResolvedJointIndexByName.Reset();
	Status.Joints.Reset();

	TArray<UPhysicsConstraintComponent*> ConstraintComponents;
	GetComponents<UPhysicsConstraintComponent>(ConstraintComponents);
	ConstraintComponents.Sort([](const UPhysicsConstraintComponent& A, const UPhysicsConstraintComponent& B)
	{
		return A.GetName() < B.GetName();
	});

	for (UPhysicsConstraintComponent* ConstraintComponent : ConstraintComponents)
	{
		if (!ConstraintComponent)
		{
			continue;
		}

		FCadRobotResolvedJoint Joint;
		Joint.Constraint = ConstraintComponent;
		Joint.PublishedName = NormalizeJointAlias(ConstraintComponent->GetName());
		Joint.Type = ResolveJointType(*ConstraintComponent, Joint.AngularAxis, Joint.LinearAxis);
		Joint.StatusIndex = Status.Joints.AddDefaulted();

		FCadRobotJointStatus& JointStatus = Status.Joints[Joint.StatusIndex];
		JointStatus.JointName = Joint.PublishedName;

		const int32 JointIndex = ResolvedJoints.Add(Joint);
		RegisterResolvedJointAlias(Joint.PublishedName, JointIndex);
		RegisterResolvedJointAlias(FName(*ConstraintComponent->GetName()), JointIndex);
	}
}

int32 ACadRobotActor::FindResolvedJointIndex(FName JointName) const
{
	if (const int32* JointIndex = ResolvedJointIndexByName.Find(NormalizeJointAlias(JointName.ToString())))
	{
		return *JointIndex;
	}

	if (const int32* JointIndex = ResolvedJointIndexByName.Find(JointName))
	{
		return *JointIndex;
	}

	return INDEX_NONE;
}

void ACadRobotActor::RegisterResolvedJointAlias(FName JointName, int32 JointIndex)
{
	if (JointName.IsNone() || ResolvedJointIndexByName.Contains(JointName))
	{
		return;
	}

	ResolvedJointIndexByName.Add(JointName, JointIndex);
}

bool ACadRobotActor::PublishStatus()
{
	if (!IsIOConnected())
	{
		return false;
	}

	FString Payload;
	if (!FJsonObjectConverter::UStructToJsonObjectString(Status, Payload, 0, 0, 0, nullptr, false))
	{
		UE_LOG(LogCadRobotIO, Warning, TEXT("Failed to serialize robot status for TCP publish."));
		return false;
	}

	FTCHARToUTF8 Utf8Payload(*Payload);
	TArray<uint8> Bytes;
	Bytes.Append(reinterpret_cast<const uint8*>(Utf8Payload.Get()), Utf8Payload.Length());
	Bytes.Add(static_cast<uint8>('\n'));

	return MasterActor->SendRobotData(ActiveConnectionId, MoveTemp(Bytes));
}

bool ACadRobotActor::TryConsumeCommandFrame(const FString& CommandJson)
{
	FCadRobotCommand ParsedCommand;
	if (!FJsonObjectConverter::JsonObjectStringToUStruct<FCadRobotCommand>(CommandJson, &ParsedCommand, 0, 0))
	{
		UE_LOG(LogCadRobotIO, Warning, TEXT("Failed to parse robot command JSON: %s"), *CommandJson);
		return false;
	}

	QueuedCommand = MoveTemp(ParsedCommand);
	bHasQueuedCommand = true;
	return true;
}

float ACadRobotActor::ReadJointPosition(const FCadRobotResolvedJoint& Joint) const
{
	const UPhysicsConstraintComponent* ConstraintComponent = Joint.Constraint.Get();
	if (!ConstraintComponent)
	{
		return 0.0f;
	}

	switch (Joint.Type)
	{
	case ECadRobotResolvedJointType::Revolute:
		switch (Joint.AngularAxis)
		{
		case ECadRobotResolvedAngularAxis::Twist:
			return FMath::DegreesToRadians(ConstraintComponent->GetCurrentTwist());
		case ECadRobotResolvedAngularAxis::Swing1:
			return FMath::DegreesToRadians(ConstraintComponent->GetCurrentSwing1());
		case ECadRobotResolvedAngularAxis::Swing2:
			return FMath::DegreesToRadians(ConstraintComponent->GetCurrentSwing2());
		default:
			return 0.0f;
		}

	case ECadRobotResolvedJointType::Prismatic:
		{
			UPrimitiveComponent* Component1 = nullptr;
			UPrimitiveComponent* Component2 = nullptr;
			FName BoneName1 = NAME_None;
			FName BoneName2 = NAME_None;
			const_cast<UPhysicsConstraintComponent*>(ConstraintComponent)->GetConstrainedComponents(Component1, BoneName1, Component2, BoneName2);
			if (!Component1 || !Component2)
			{
				return 0.0f;
			}

			const FTransform Frame1World = ConstraintComponent->ConstraintInstance.GetRefFrame(EConstraintFrame::Frame1) * Component1->GetComponentTransform();
			const FTransform Frame2World = ConstraintComponent->ConstraintInstance.GetRefFrame(EConstraintFrame::Frame2) * Component2->GetComponentTransform();
			const FVector Delta = Frame2World.GetLocation() - Frame1World.GetLocation();

			EAxis::Type Axis = EAxis::X;
			switch (Joint.LinearAxis)
			{
			case ECadRobotResolvedLinearAxis::X:
				Axis = EAxis::X;
				break;
			case ECadRobotResolvedLinearAxis::Y:
				Axis = EAxis::Y;
				break;
			case ECadRobotResolvedLinearAxis::Z:
				Axis = EAxis::Z;
				break;
			default:
				break;
			}

			return FVector::DotProduct(Delta, Frame1World.GetUnitAxis(Axis));
		}

	case ECadRobotResolvedJointType::Fixed:
	case ECadRobotResolvedJointType::Unknown:
	default:
		return 0.0f;
	}
}

void ACadRobotActor::ApplyJointTarget(const FCadRobotResolvedJoint& Joint, float TargetPosition) const
{
	UPhysicsConstraintComponent* ConstraintComponent = Joint.Constraint.Get();
	if (!ConstraintComponent)
	{
		return;
	}

	WakeConstraintBodies(*ConstraintComponent);

	switch (Joint.Type)
	{
	case ECadRobotResolvedJointType::Revolute:
		{
			const float TargetDegrees = FMath::RadiansToDegrees(TargetPosition);
			FRotator OrientationTarget = FRotator::ZeroRotator;

			switch (Joint.AngularAxis)
			{
			case ECadRobotResolvedAngularAxis::Twist:
				OrientationTarget.Roll = TargetDegrees;
				break;
			case ECadRobotResolvedAngularAxis::Swing1:
				OrientationTarget.Pitch = TargetDegrees;
				break;
			case ECadRobotResolvedAngularAxis::Swing2:
				OrientationTarget.Yaw = TargetDegrees;
				break;
			default:
				break;
			}

			ConstraintComponent->SetAngularOrientationTarget(OrientationTarget);
			break;
		}

	case ECadRobotResolvedJointType::Prismatic:
		{
			FVector LinearTarget = FVector::ZeroVector;
			switch (Joint.LinearAxis)
			{
			case ECadRobotResolvedLinearAxis::X:
				LinearTarget.X = TargetPosition;
				break;
			case ECadRobotResolvedLinearAxis::Y:
				LinearTarget.Y = TargetPosition;
				break;
			case ECadRobotResolvedLinearAxis::Z:
				LinearTarget.Z = TargetPosition;
				break;
			default:
				break;
			}

			ConstraintComponent->SetLinearPositionTarget(LinearTarget);
			break;
		}

	case ECadRobotResolvedJointType::Fixed:
	case ECadRobotResolvedJointType::Unknown:
	default:
		break;
	}
}

FName ACadRobotActor::NormalizeJointAlias(const FString& JointName)
{
	FString Normalized = JointName.TrimStartAndEnd();
	Normalized.ReplaceInline(TEXT(" "), TEXT("_"));
	Normalized.ReplaceInline(TEXT("-"), TEXT("_"));
	Normalized.ReplaceInline(TEXT("."), TEXT("_"));

	if (Normalized.StartsWith(CadJointPrefix))
	{
		Normalized.RightChopInline(UE_ARRAY_COUNT(CadJointPrefix) - 1, EAllowShrinking::No);
	}

	return FName(*Normalized);
}
