// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.


#include "EnginePrivate.h"
#include "PhysicsPublic.h"

#if WITH_PHYSX
	#include "PhysXSupport.h"
	#include "../Vehicles/PhysXVehicleManager.h"
#endif

FPhysCommandHandler::~FPhysCommandHandler()
{
	if(PendingCommands.Num() != 0)
	{
		UE_LOG(LogPhysics, Warning, TEXT("~FPhysCommandHandler() - Pending command list is not empty. %d item remain."), PendingCommands.Num());
	}
}

void FPhysCommandHandler::Flush()
{
	check(IsInGameThread());
	ExecuteCommands();
	PendingCommands.Empty();
}

bool FPhysCommandHandler::HasPendingCommands()
{
	return PendingCommands.Num() > 0;
}

void FPhysCommandHandler::ExecuteCommands()
{
	for (int32 i = 0; i < PendingCommands.Num(); ++i)
	{
		const FPhysPendingCommand& Command = PendingCommands[i];
		switch (Command.CommandType)
		{
#if WITH_APEX
		case PhysCommand::Release:
		{
			physx::apex::NxApexInterface * ApexInterface = Command.Pointer.ApexInterface;
			ApexInterface->release();
			break;
		}
#endif
#if WITH_PHYSX
		case PhysCommand::ReleasePScene:
		{
			physx::PxScene * PScene = Command.Pointer.PScene;
			PScene->release();
			break;
		}
		case PhysCommand::DeleteSimEventCallback:
		{
			physx::PxSimulationEventCallback * SimEventCallback = Command.Pointer.SimEventCallback;
			if (FPhysScene::SimEventCallbackFactory.IsValid())
			{
				FPhysScene::SimEventCallbackFactory->Destroy(SimEventCallback);
			}
			else
			{
				delete SimEventCallback;
			}
			break;
		}
		case PhysCommand::DeleteCPUDispatcher:
		{
			physx::PxCpuDispatcher * CPUDispatcher = Command.Pointer.CPUDispatcher;
			delete CPUDispatcher;
			break;
		}
#endif
		case PhysCommand::Max:	//this is just here because right now all commands are APEX and having a switch with only default is bad
		default:
			check(0);	//Unsupported command
			break;
		}
	}
}

void FPhysCommandHandler::EnqueueCommand(const FPhysPendingCommand& Command)
{
	check(IsInGameThread());
	PendingCommands.Add(Command);
}


#if WITH_PHYSX
void FPhysCommandHandler::DeferredRelease(physx::PxScene* PScene)
{
	check(PScene);

	FPhysPendingCommand Command;
	Command.Pointer.PScene = PScene;
	Command.CommandType = PhysCommand::ReleasePScene;

	EnqueueCommand(Command);

}

void  FPhysCommandHandler::DeferredDeleteSimEventCallback(physx::PxSimulationEventCallback * SimEventCallback)
{
	check(SimEventCallback);

	FPhysPendingCommand Command;
	Command.Pointer.SimEventCallback = SimEventCallback;
	Command.CommandType = PhysCommand::DeleteSimEventCallback;

	EnqueueCommand(Command);
}
void  FPhysCommandHandler::DeferredDeleteCPUDispathcer(physx::PxCpuDispatcher * CPUDispatcher)
{
	check(CPUDispatcher);

	FPhysPendingCommand Command;
	Command.Pointer.CPUDispatcher = CPUDispatcher;
	Command.CommandType = PhysCommand::DeleteCPUDispatcher;

	EnqueueCommand(Command);
}
#endif

#if WITH_APEX
void FPhysCommandHandler::DeferredRelease(physx::apex::NxApexInterface* ApexInterface)
{
	check(ApexInterface);

	FPhysPendingCommand Command;
	Command.Pointer.ApexInterface = ApexInterface;
	Command.CommandType = PhysCommand::Release;

	EnqueueCommand(Command);

}
#endif