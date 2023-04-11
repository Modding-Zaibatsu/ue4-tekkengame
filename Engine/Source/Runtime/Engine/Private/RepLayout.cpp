// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	RepLayout.cpp: Unreal replication layout implementation.
=============================================================================*/

#include "EnginePrivate.h"
#include "Net/RepLayout.h"
#include "Net/DataReplication.h"
#include "Net/NetworkProfiler.h"
#include "Engine/ActorChannel.h"
#include "Engine/PackageMapClient.h"

static TAutoConsoleVariable<int32> CVarAllowPropertySkipping( TEXT( "net.AllowPropertySkipping" ), 1, TEXT( "Allow skipping of properties that haven't changed for other clients" ) );

static TAutoConsoleVariable<int32> CVarDoPropertyChecksum( TEXT( "net.DoPropertyChecksum" ), 0, TEXT( "" ) );

FAutoConsoleVariable CVarDoReplicationContextString( TEXT( "net.ContextDebug" ), 0, TEXT( "" ) );

int32 LogSkippedRepNotifies = 0;
static FAutoConsoleVariableRef CVarLogSkippedRepNotifies(TEXT("Net.LogSkippedRepNotifies"), LogSkippedRepNotifies, TEXT("Log when the networking code skips calling a repnotify clientside due to the property value not changing."), ECVF_Default );

#define ENABLE_PROPERTY_CHECKSUMS

//#define SANITY_CHECK_MERGES

#define USE_CUSTOM_COMPARE

//#define ENABLE_SUPER_CHECKSUMS

#ifdef USE_CUSTOM_COMPARE
static FORCEINLINE bool CompareBool( const FRepLayoutCmd& Cmd, const void* A, const void* B )
{
	return Cmd.Property->Identical( A, B );
}

static FORCEINLINE bool CompareObject( const FRepLayoutCmd& Cmd, const void* A, const void* B )
{
#if 1
	// Until UObjectPropertyBase::Identical is made safe for GC'd objects, we need to do it manually
	// This saves us from having to add referenced objects during GC
	UObjectPropertyBase * ObjProperty = CastChecked< UObjectPropertyBase>( Cmd.Property );

	UObject* ObjectA = ObjProperty->GetObjectPropertyValue( A );
	UObject* ObjectB = ObjProperty->GetObjectPropertyValue( B );

	return ObjectA == ObjectB;
#else
	return Cmd.Property->Identical( A, B );
#endif
}

template< typename T >
bool CompareValue( const T * A, const T * B )
{
	return *A == *B;
}

template< typename T >
bool CompareValue( const void* A, const void* B )
{
	return CompareValue( (T*)A, (T*)B);
}

static FORCEINLINE bool PropertiesAreIdenticalNative( const FRepLayoutCmd& Cmd, const void* A, const void* B )
{
	switch ( Cmd.Type )
	{
		case REPCMD_PropertyBool:			return CompareBool( Cmd, A, B );
		case REPCMD_PropertyByte:			return CompareValue<uint8>( A, B );
		case REPCMD_PropertyFloat:			return CompareValue<float>( A, B );
		case REPCMD_PropertyInt:			return CompareValue<int32>( A, B );
		case REPCMD_PropertyName:			return CompareValue<FName>( A, B );
		case REPCMD_PropertyObject:			return CompareObject( Cmd, A, B );
		case REPCMD_PropertyUInt32:			return CompareValue<uint32>( A, B );
		case REPCMD_PropertyUInt64:			return CompareValue<uint64>( A, B );
		case REPCMD_PropertyVector:			return CompareValue<FVector>( A, B );
		case REPCMD_PropertyVector100:		return CompareValue<FVector_NetQuantize100>( A, B );
		case REPCMD_PropertyVectorQ:		return CompareValue<FVector_NetQuantize>( A, B );
		case REPCMD_PropertyVectorNormal:	return CompareValue<FVector_NetQuantizeNormal>( A, B );
		case REPCMD_PropertyVector10:		return CompareValue<FVector_NetQuantize10>( A, B );
		case REPCMD_PropertyPlane:			return CompareValue<FPlane>( A, B );
		case REPCMD_PropertyRotator:		return CompareValue<FRotator>( A, B );
		case REPCMD_PropertyNetId:			return CompareValue<FUniqueNetIdRepl>( A, B );
		case REPCMD_RepMovement:			return CompareValue<FRepMovement>( A, B );
		case REPCMD_PropertyString:			return CompareValue<FString>( A, B );
		case REPCMD_Property:				return Cmd.Property->Identical( A, B );
		default: 
			UE_LOG( LogRep, Fatal, TEXT( "PropertiesAreIdentical: Unsupported type! %i (%s)" ), Cmd.Type, *Cmd.Property->GetName() );
	}

	return false;
}

static FORCEINLINE bool PropertiesAreIdentical( const FRepLayoutCmd& Cmd, const void* A, const void* B )
{
	const bool bIsIdentical = PropertiesAreIdenticalNative( Cmd, A, B );
#if 0
	// Sanity check result
	if ( bIsIdentical != Cmd.Property->Identical( A, B ) )
	{
		UE_LOG( LogRep, Fatal, TEXT( "PropertiesAreIdentical: Result mismatch! (%s)" ), *Cmd.Property->GetFullName() );
	}
#endif
	return bIsIdentical;
}
#else
static FORCEINLINE bool PropertiesAreIdentical( const FRepLayoutCmd& Cmd, const void* A, const void* B )
{
	return Cmd.Property->Identical( A, B );
}
#endif

static FORCEINLINE void StoreProperty( const FRepLayoutCmd& Cmd, void* A, const void* B )
{
	Cmd.Property->CopySingleValue( A, B );
}

static FORCEINLINE void SerializeGenericChecksum( FArchive & Ar )
{
	uint32 Checksum = 0xABADF00D;
	Ar << Checksum;
	check( Checksum == 0xABADF00D );
}

static void SerializeReadWritePropertyChecksum( const FRepLayoutCmd& Cmd, const int32 CurCmdIndex, const uint8* Data, FArchive & Ar )
{
	// Serialize various attributes that will mostly ensure we are working on the same property
	const uint32 NameHash = GetTypeHash( Cmd.Property->GetName() );

	uint32 MarkerChecksum = 0;

	// Evolve the checksum over several values that will uniquely identity where we are and should be
	MarkerChecksum = FCrc::MemCrc_DEPRECATED( &NameHash,		sizeof( NameHash ),		MarkerChecksum );
	MarkerChecksum = FCrc::MemCrc_DEPRECATED( &Cmd.Offset,		sizeof( Cmd.Offset ),	MarkerChecksum );
	MarkerChecksum = FCrc::MemCrc_DEPRECATED( &CurCmdIndex,	sizeof( CurCmdIndex ),	MarkerChecksum );

	const uint32 OriginalMarkerChecksum = MarkerChecksum;

	Ar << MarkerChecksum;

	if ( MarkerChecksum != OriginalMarkerChecksum )
	{
		// This is fatal, as it means we are out of sync to the point we can't recover
		UE_LOG( LogRep, Fatal, TEXT( "SerializeReadWritePropertyChecksum: Property checksum marker failed! [%s]" ), *Cmd.Property->GetFullName() );
	}

	if ( Cmd.Property->IsA( UObjectPropertyBase::StaticClass() ) )
	{
		// Can't handle checksums for objects right now
		// Need to resolve how to handle unmapped objects
		return;
	}

	// Now generate a checksum that guarantee that this property is in the exact state as the server
	// This will require NetSerializeItem to be deterministic, in and out
	// i.e, not only does NetSerializeItem need to write the same blob on the same input data, but
	//	it also needs to write the same blob it just read as well.
	FBitWriter Writer( 0, true );

	Cmd.Property->NetSerializeItem( Writer, NULL, const_cast< uint8* >( Data ) );

	if ( Ar.IsSaving() )
	{
		// If this is the server, do a read, and then another write so that we do exactly what the client will do, which will better ensure determinism 

		// We do this to force InitializeValue, DestroyValue etc to work on a single item
		const int32 OriginalDim = Cmd.Property->ArrayDim;
		Cmd.Property->ArrayDim = 1;

		TArray< uint8 > TempPropMemory;
		TempPropMemory.AddZeroed( Cmd.Property->ElementSize + 4 );
		uint32* Guard = (uint32*)&TempPropMemory[TempPropMemory.Num() - 4];
		const uint32 TAG_VALUE = 0xABADF00D;
		*Guard = TAG_VALUE;
		Cmd.Property->InitializeValue( TempPropMemory.GetData() );
		check( *Guard == TAG_VALUE );

		// Read it back in and then write it out to produce what the client will produce
		FBitReader Reader( Writer.GetData(), Writer.GetNumBits() );
		Cmd.Property->NetSerializeItem( Reader, NULL, TempPropMemory.GetData() );
		check( Reader.AtEnd() && !Reader.IsError() );
		check( *Guard == TAG_VALUE );

		// Write it back out for a final time
		Writer.Reset();

		Cmd.Property->NetSerializeItem( Writer, NULL, TempPropMemory.GetData() );
		check( *Guard == TAG_VALUE );

		// Destroy temp memory
		Cmd.Property->DestroyValue( TempPropMemory.GetData() );

		// Restore the static array size
		Cmd.Property->ArrayDim = OriginalDim;

		check( *Guard == TAG_VALUE );
	}

	uint32 PropertyChecksum = FCrc::MemCrc_DEPRECATED( Writer.GetData(), Writer.GetNumBytes() );

	const uint32 OriginalPropertyChecksum = PropertyChecksum;

	Ar << PropertyChecksum;

	if ( PropertyChecksum != OriginalPropertyChecksum )
	{
		// This is a warning, because for some reason, float rounding issues in the quantization functions cause this to return false positives
		UE_LOG( LogRep, Warning, TEXT( "Property checksum failed! [%s]" ), *Cmd.Property->GetFullName() );
	}
}

#define INIT_STACK( TStack )							\
	void InitStack( TStack& StackState )				\

#define SHOULD_PROCESS_NEXT_CMD()						\
	bool ShouldProcessNextCmd()							\

#define PROCESS_ARRAY_CMD( TStack )						\
	void ProcessArrayCmd_r(								\
	TStack&							PrevStackState,		\
	TStack&							StackState,			\
	const FRepLayoutCmd&			Cmd,				\
	const int32						CmdIndex,			\
	uint8* RESTRICT					ShadowData,			\
	uint8* RESTRICT					Data )				\


#define PROCESS_CMD( TStack )							\
	void ProcessCmd(									\
	TStack&							StackState,			\
	const FRepLayoutCmd&			Cmd,				\
	const int32						CmdIndex,			\
	uint8* RESTRICT					ShadowData,			\
	uint8* RESTRICT					Data )				\

class FCmdIteratorBaseStackState
{
public:
	FCmdIteratorBaseStackState( const int32 InCmdStart, const int32 InCmdEnd, FScriptArray*	InShadowArray, FScriptArray* InDataArray, uint8* RESTRICT InShadowBaseData, uint8* RESTRICT	InBaseData ) : 
		CmdStart( InCmdStart ),
		CmdEnd( InCmdEnd ),
		ShadowArray( InShadowArray ),
		DataArray( InDataArray ),
		ShadowBaseData( InShadowBaseData ),
		BaseData( InBaseData )
	{
	}

	const int32			CmdStart; 
	const int32			CmdEnd;

	FScriptArray*		ShadowArray;
	FScriptArray*		DataArray;

	uint8* RESTRICT		ShadowBaseData;
	uint8* RESTRICT		BaseData;
};

// This uses the "Curiously recurring template pattern" (CRTP) ideas
template< typename TImpl, typename TStackState >
class FRepLayoutCmdIterator
{
public:
	FRepLayoutCmdIterator( const TArray< FRepParentCmd >& InParents, const TArray< FRepLayoutCmd >& InCmds ) : Parents( InParents ), Cmds( InCmds ) {}

	void ProcessDataArrayElements_r( TStackState& StackState, const FRepLayoutCmd& Cmd )
	{
		const int32 NumDataArrayElements	= StackState.DataArray		? StackState.DataArray->Num()	: 0;
		const int32 NumShadowArrayElements	= StackState.ShadowArray	? StackState.ShadowArray->Num() : 0;

		// Loop using the number of elements in data array
		for ( int32 i = 0; i < NumDataArrayElements; i++ )
		{
			const int32 ElementOffset = i * Cmd.ElementSize;

			uint8* Data			= StackState.BaseData + ElementOffset;
			uint8* ShadowData	= i < NumShadowArrayElements ? ( StackState.ShadowBaseData + ElementOffset ) : NULL;	// ShadowArray might be smaller than DataArray

			ProcessCmds_r( StackState, ShadowData, Data );
		}
	}

	void ProcessShadowArrayElements_r( TStackState& StackState, const FRepLayoutCmd& Cmd )
	{
		const int32 NumDataArrayElements	= StackState.DataArray		? StackState.DataArray->Num()	: 0;
		const int32 NumShadowArrayElements	= StackState.ShadowArray	? StackState.ShadowArray->Num() : 0;

		// Loop using the number of elements in shadow array
		for ( int32 i = 0; i < NumShadowArrayElements; i++ )
		{
			const int32 ElementOffset = i * Cmd.ElementSize;

			uint8* Data			= i < NumDataArrayElements ? ( StackState.BaseData + ElementOffset ) : NULL;	// DataArray might be smaller than ShadowArray
			uint8* ShadowData	= StackState.ShadowBaseData + ElementOffset;

			ProcessCmds_r( StackState, ShadowData, Data );
		}
	}

	void ProcessArrayCmd_r( TStackState & PrevStackState, const FRepLayoutCmd& Cmd, const int32 CmdIndex, uint8* RESTRICT ShadowData, uint8* RESTRICT Data )
	{
		check( ShadowData != NULL || Data != NULL );

		FScriptArray* ShadowArray	= (FScriptArray*)ShadowData;
		FScriptArray* DataArray		= (FScriptArray*)Data;

		TStackState StackState( CmdIndex + 1, Cmd.EndCmd - 1, ShadowArray, DataArray, ShadowArray ? (uint8*)ShadowArray->GetData() : NULL, DataArray ? (uint8*)DataArray->GetData() : NULL );

		static_cast< TImpl* >( this )->ProcessArrayCmd_r( PrevStackState, StackState, Cmd, CmdIndex, ShadowData, Data );
	}

	void ProcessCmds_r( TStackState& StackState, uint8* RESTRICT ShadowData, uint8* RESTRICT Data )
	{
		check( ShadowData != NULL || Data != NULL );

		for ( int32 CmdIndex = StackState.CmdStart; CmdIndex < StackState.CmdEnd; CmdIndex++ )
		{
			const FRepLayoutCmd& Cmd = Cmds[ CmdIndex ];

			check( Cmd.Type != REPCMD_Return );

			if ( Cmd.Type == REPCMD_DynamicArray )
			{
				if ( static_cast< TImpl* >( this )->ShouldProcessNextCmd() )
				{
					ProcessArrayCmd_r( StackState, Cmd, CmdIndex, ShadowData ? ( ShadowData + Cmd.Offset ) : NULL, Data ? ( Data + Cmd.Offset ) : NULL );
				}
				CmdIndex = Cmd.EndCmd - 1;	// Jump past children of this array (-1 for ++ in for loop)
			}
			else
			{
				if ( static_cast< TImpl* >( this )->ShouldProcessNextCmd() )
				{
					static_cast< TImpl* >( this )->ProcessCmd( StackState, Cmd, CmdIndex, ShadowData, Data );
				}
			}
		}
	}

	void ProcessCmds( uint8* RESTRICT Data, uint8* RESTRICT ShadowData )
	{
		TStackState StackState( 0, Cmds.Num() - 1, NULL, NULL, ShadowData, Data );

		static_cast< TImpl* >( this )->InitStack( StackState );

		ProcessCmds_r( StackState, ShadowData, Data );
	}

	const TArray< FRepParentCmd >&	Parents;
	const TArray< FRepLayoutCmd >&	Cmds;
};

uint16 FRepLayout::CompareProperties_r(
	const int32				CmdStart,
	const int32				CmdEnd,
	const uint8* RESTRICT	CompareData, 
	const uint8* RESTRICT	Data,
	TArray< uint16 > &		Changed,
	uint16					Handle
) const
{
	for ( int32 CmdIndex = CmdStart; CmdIndex < CmdEnd; CmdIndex++ )
	{
		const FRepLayoutCmd& Cmd = Cmds[CmdIndex];

		check( Cmd.Type != REPCMD_Return );

		Handle++;

		if ( Cmd.Type == REPCMD_DynamicArray )
		{
			// Once we hit an array, start using a stack based approach
			CompareProperties_Array_r( CompareData ? CompareData + Cmd.Offset : NULL, (const uint8*)Data + Cmd.Offset, Changed, CmdIndex, Handle );
			CmdIndex = Cmd.EndCmd - 1;		// The -1 to handle the ++ in the for loop
			continue;
		}

		if ( CompareData == NULL || !PropertiesAreIdentical( Cmd, (const void*)( CompareData + Cmd.Offset ), (const void*)( Data + Cmd.Offset ) ) )
		{
			Changed.Add( Handle );
		}
	}

	return Handle;
}

void FRepLayout::CompareProperties_Array_r( 
	const uint8* RESTRICT	CompareData, 
	const uint8* RESTRICT	Data,
	TArray< uint16 > &		Changed,
	const uint16			CmdIndex,
	const uint16			Handle
) const
{
	const FRepLayoutCmd& Cmd = Cmds[CmdIndex];

	FScriptArray * CompareArray = (FScriptArray *)CompareData;
	FScriptArray * Array = (FScriptArray *)Data;

	const uint16 ArrayNum			= Array->Num();
	const uint16 CompareArrayNum	= CompareArray ? CompareArray->Num() : 0;

	TArray< uint16 > ChangedLocal;

	uint16 LocalHandle = 0;

	Data = (uint8*)Array->GetData();
	CompareData = CompareData ? (uint8*)CompareArray->GetData() : (uint8*)NULL;

	for ( int32 i = 0; i < ArrayNum; i++ )
	{
		const int32 ElementOffset = i * Cmd.ElementSize;
		const uint8* LocalCompareData = ( i < CompareArrayNum ) ? ( CompareData + ElementOffset ) : NULL;

		LocalHandle = CompareProperties_r( CmdIndex + 1, Cmd.EndCmd - 1, LocalCompareData, Data + ElementOffset, ChangedLocal, LocalHandle );
	}

	if ( ChangedLocal.Num() )
	{
		Changed.Add( Handle );
		Changed.Add( ChangedLocal.Num() );		// This is so we can jump over the array if we need to
		Changed.Append( ChangedLocal );
		Changed.Add( 0 );
	}
	else if ( ArrayNum != CompareArrayNum )
	{
		// If nothing below us changed, we either shrunk, or we grew and our inner was an array that didn't have any elements
		check( ArrayNum < CompareArrayNum || Cmds[CmdIndex + 1].Type == REPCMD_DynamicArray );

		// Array got smaller, send the array handle to force array size change
		Changed.Add( Handle );
		Changed.Add( 0 );
		Changed.Add( 0 );
	}
}

bool FRepLayout::CompareProperties( 
	FRepState * RESTRICT				RepState, 
	const uint8* RESTRICT				CompareData,
	const uint8* RESTRICT				Data, 
	TArray< FRepChangedParent > &		OutChangedParents,
	const TArray< uint16 > &			PropertyList ) const
{
	bool PropertyChanged = false;

	const uint16* RESTRICT FirstProp	= PropertyList.GetData();
	const uint16* RESTRICT LastProp	= FirstProp + PropertyList.Num();

	for ( const uint16* RESTRICT pLifeProp = FirstProp; pLifeProp < LastProp; ++pLifeProp )
	{
		const uint16 LifeProp = *pLifeProp;
		const FRepParentCmd& ParentCmd = Parents[LifeProp];

		// We store changed properties on each parent, so we can build a final sorted change list later
		TArray< uint16 > & Changed = OutChangedParents[LifeProp].Changed;

		// some games ship checks() in Shipping so we cannot rely on DO_CHECK here, and this check is in an extremely hot path
		check( UE_BUILD_SHIPPING || UE_BUILD_TEST || Changed.Num() == 0 );

		// Loop over the block of child properties that are children of this parent
		for ( int32 i = ParentCmd.CmdStart, ParentCmd_CmdEnd = ParentCmd.CmdEnd; i < ParentCmd_CmdEnd; ++i )
		{
			const FRepLayoutCmd& Cmd = Cmds[i];
			const int32 Cmd_Offset = Cmd.Offset;

			// some games ship checks() in Shipping so we cannot rely on DO_CHECK here, and this check is in an extremely hot path
			check( UE_BUILD_SHIPPING || UE_BUILD_TEST || Cmd.Type != REPCMD_Return );		// REPCMD_Return's are markers we shouldn't hit

			if ( UNLIKELY( Cmd.Type == REPCMD_DynamicArray ) )
			{
				// Once we hit an array, start using a recursive based approach
				CompareProperties_Array_r( CompareData + Cmd_Offset, Data + Cmd_Offset, Changed, i, Cmd.RelativeHandle );
				i = Cmd.EndCmd - 1;		// Jump past properties under array, we've already checked them (the -1 because of the ++ in the for loop)
				continue;
			}

			if ( !PropertiesAreIdentical( Cmd, (void*)( CompareData + Cmd_Offset ), (const void*)( Data + Cmd_Offset ) ) )
			{
				// Add this properties handle to the change list
				Changed.Add( Cmd.RelativeHandle );
			}
		}

		if ( Changed.Num() > 0 )
		{
			// Something changed on this parent property
			PropertyChanged = true;
		}
	}

	return PropertyChanged;
}

void FRepLayout::LogChangeListMismatches( 
	const uint8* 						Data, 
	UActorChannel *						OwningChannel, 
	const TArray< uint16 > &			PropertyList,
	FRepState *							RepState1, 
	FRepState * 						RepState2, 
	const TArray< FRepChangedParent > & ChangedParents1,
	const TArray< FRepChangedParent > & ChangedParents2 ) const
{
	FRepChangedPropertyTracker *	ChangeTracker	= RepState1->RepChangedPropertyTracker.Get();
	UObject *						Object			= (UObject*)Data;
	const UNetDriver *				NetDriver		= OwningChannel->Connection->Driver;

	UE_LOG( LogRep, Warning, TEXT( "LogChangeListMismatches: %s" ), *Object->GetName() );

	UE_LOG( LogRep, Warning, TEXT( "  %i, %i, %i, %i" ), NetDriver->ReplicationFrame, ChangeTracker->LastReplicationFrame, ChangeTracker->LastReplicationGroupFrame, RepState1->LastReplicationFrame );

	for ( int32 i = 0; i < PropertyList.Num(); i++ )
	{
		const int32 Index		= PropertyList[i];
		const int32 Changed1	= ChangedParents1[Index].Changed.Num();
		const int32 Changed2	= ChangedParents2[Index].Changed.Num();

		if ( Changed1 || Changed2 )
		{
			FString Changed1Str = Changed1 ? TEXT( "TRUE" ) : TEXT( "FALSE" );
			FString Changed2Str = Changed2 ? TEXT( "TRUE" ) : TEXT( "FALSE" );
			FString ExtraStr	= Changed1 != Changed2 ? TEXT( "<--- MISMATCH!" ) : TEXT( "<--- SAME" );

			UE_LOG( LogRep, Warning, TEXT( "    Property changed: %s (%s / %s) %s" ), *Parents[Index].Property->GetName(), *Changed1Str, *Changed2Str, *ExtraStr );

			FString StringValue1;
			FString StringValue2;
			FString StringValue3;

			Parents[Index].Property->ExportText_InContainer( Parents[Index].ArrayIndex, StringValue1, Data, NULL, NULL, PPF_DebugDump );
			Parents[Index].Property->ExportText_InContainer( Parents[Index].ArrayIndex, StringValue2, RepState1->StaticBuffer.GetData(), NULL, NULL, PPF_DebugDump );
			Parents[Index].Property->ExportText_InContainer( Parents[Index].ArrayIndex, StringValue3, RepState2->StaticBuffer.GetData(), NULL, NULL, PPF_DebugDump );

			UE_LOG( LogRep, Warning, TEXT( "      Values: %s, %s, %s" ), *StringValue1, *StringValue2, *StringValue3 );
		}
	}
}

static bool ChangedParentsHasChanged( const TArray< uint16 > & PropertyList, const TArray< FRepChangedParent > & ChangedParents )
{
	for ( int32 i = 0; i < PropertyList.Num(); i++ )
	{
		if ( ChangedParents[PropertyList[i]].Changed.Num() )
		{
			return true;
		}
	}

	return false;
}

void FRepLayout::SanityCheckShadowStateAgainstChangeList( 
	FRepState *							RepState, 
	const uint8 *						Data, 
	UActorChannel *						OwningChannel, 
	const TArray< uint16 > &			PropertyList,
	FRepState *							OtherRepState,
	const TArray< FRepChangedParent > & OtherChangedParents ) const
{
	const uint8 *	CompareData	= RepState->StaticBuffer.GetData();
	UObject *		Object		= (UObject*)Data;

	TArray< FRepChangedParent > LocalChangedParents;
	LocalChangedParents.SetNum( OtherChangedParents.Num() );

	// Do an actual check to see which properties are different from our internal shadow state, and make sure the passed in change list matches
	const bool Result = CompareProperties( RepState, CompareData, Data, LocalChangedParents, PropertyList );

	if ( Result != ChangedParentsHasChanged( PropertyList, OtherChangedParents ) )
	{
		LogChangeListMismatches( Data, OwningChannel, PropertyList, RepState, OtherRepState, LocalChangedParents, OtherChangedParents );
		UE_LOG( LogRep, Fatal, TEXT( "ReplicateProperties: Compare result mismatch: %s" ), *Object->GetName() );
	}

	for ( int32 i = 0; i < PropertyList.Num(); i++ )
	{
		const int32 Index = PropertyList[i];

		if ( OtherChangedParents[Index].Changed.Num() != LocalChangedParents[Index].Changed.Num() )
		{
			LogChangeListMismatches( Data, OwningChannel, PropertyList, RepState, OtherRepState, LocalChangedParents, OtherChangedParents );
			UE_LOG( LogRep, Fatal, TEXT( "ReplicateProperties: Compare count mismatch: %s" ), *Object->GetName() );
		}

		for ( int32 j = 0; j < OtherChangedParents[Index].Changed.Num(); j++ )
		{
			if ( OtherChangedParents[Index].Changed[j] != LocalChangedParents[Index].Changed[j] )
			{
				LogChangeListMismatches( Data, OwningChannel, PropertyList, RepState, OtherRepState, LocalChangedParents, OtherChangedParents );
				UE_LOG( LogRep, Fatal, TEXT( "ReplicateProperties: Compare changelist value mismatch: %s" ), *Object->GetName() );
			}
		}
	}
}

bool FRepLayout::ReplicateProperties( 
	FRepState * RESTRICT		RepState, 
	const uint8* RESTRICT		Data, 
	UClass *					ObjectClass,
	UActorChannel *				OwningChannel,
	FNetBitWriter&				Writer,
	const FReplicationFlags &	RepFlags ) const
{
	SCOPE_CYCLE_COUNTER( STAT_NetReplicateDynamicPropTime );

	check( ObjectClass == Owner );

	if ( OwningChannel->Connection->bResendAllDataSinceOpen )
	{
		check( OwningChannel->Connection->InternalAck );
		// If we are resending data since open, we don't want to affect the current state of channel/replication, so just do the minimum and send the data, and return
		if ( RepState->LifetimeChangelist.Num() > 0 )
		{
			// Use a pruned version of the list, in case arrays changed size since the last time we replicated
			TArray< uint16 > Pruned;
			PruneChangeList( RepState, Data, RepState->LifetimeChangelist, Pruned );
			RepState->LifetimeChangelist = MoveTemp( Pruned );
			SendProperties_BackwardsCompatible( nullptr, Data, OwningChannel->Connection, Writer, RepState->LifetimeChangelist );
			return true;
		}

		return false;
	}

	UObject *						Object			= (UObject*)Data;
	const UNetDriver *				NetDriver		= OwningChannel->Connection->Driver;
	FRepChangedPropertyTracker *	ChangeTracker	= RepState->RepChangedPropertyTracker.Get();
	const uint8 *					CompareData		= RepState->StaticBuffer.GetData();

	// Rebuild conditional properties if needed
	if ( RepState->RepFlags.Value != RepFlags.Value || RepState->ActiveStatusChanged != ChangeTracker->ActiveStatusChanged )
	{
		RebuildConditionalProperties( RepState, *ChangeTracker, RepFlags );

		RepState->RepFlags.Value		= RepFlags.Value;
		RepState->ActiveStatusChanged	= ChangeTracker->ActiveStatusChanged;
	}

	bool PropertyChanged = false;

#ifdef ENABLE_SUPER_CHECKSUMS
	const bool bIsAllAcked = AllAcked( RepState );

	if ( bIsAllAcked || !RepState->OpenAckedCalled )
#endif
	{
		const int32	AllowSkipping = CVarAllowPropertySkipping.GetValueOnGameThread();
		
		const bool bCanSkip =	AllowSkipping > 0 && 
								RepState->LastReplicationFrame != 0 &&
								ChangeTracker->LastReplicationFrame == NetDriver->ReplicationFrame &&
								ChangeTracker->LastReplicationGroupFrame == RepState->LastReplicationFrame;

		if ( bCanSkip )
		{
			INC_DWORD_STAT_BY( STAT_NetSkippedDynamicProps, UnconditionalLifetime.Num() );

			if ( AllowSkipping == 2 )
			{
				// Sanity check results
				check( ChangeTracker->UnconditionalPropChanged == ChangedParentsHasChanged( UnconditionalLifetime, ChangeTracker->Parents ) );

				SanityCheckShadowStateAgainstChangeList( RepState, Data, OwningChannel, UnconditionalLifetime, ChangeTracker->LastRepState, ChangeTracker->Parents );
			}
		}
		else
		{
			// FRepState group changed, force this group to compare again this frame
			// This happens either once a frame, which is normal, or multiple times a frame 
			// when multiple connections of the same actor aren't updated at the same time
			ChangeTracker->LastReplicationFrame			= NetDriver->ReplicationFrame;
			ChangeTracker->LastReplicationGroupFrame	= RepState->LastReplicationFrame;
			ChangeTracker->LastRepState					= RepState;

			// Reset changed list if anything changed last time
			if ( ChangeTracker->UnconditionalPropChanged )
			{
				for ( int32 i = UnconditionalLifetime.Num() - 1; i >= 0; i-- )
				{
					ChangeTracker->Parents[UnconditionalLifetime[i]].Changed.Empty();
				}
			}

			// Loop over all unconditional lifetime properties
			ChangeTracker->UnconditionalPropChanged = CompareProperties( RepState, CompareData, Data, ChangeTracker->Parents, UnconditionalLifetime );
		}

		// Remember the last frame this FRepState was replicated, so we can note above when the FRepState replication group changes
		RepState->LastReplicationFrame = NetDriver->ReplicationFrame;

		if ( ChangeTracker->UnconditionalPropChanged )
		{
			PropertyChanged	= true;
		}

		// Loop over all the conditional properties
		if ( CompareProperties( RepState, CompareData, Data, ChangeTracker->Parents, RepState->ConditionalLifetime ) )
		{
			PropertyChanged = true;
		}
	}
#ifdef ENABLE_SUPER_CHECKSUMS
	else
	{
		// If we didn't compare this frame, make sure to reset out replication frame
		// This is to force a compare next time it comes up
		RepState->LastReplicationFrame = 0;		
	}
#endif

	// PreOpenAckHistory are all the properties sent before we got our first open ack
	const bool bFlushPreOpenAckHistory = RepState->OpenAckedCalled && RepState->PreOpenAckHistory.Num() > 0;

	if ( PropertyChanged || RepState->NumNaks > 0 || bFlushPreOpenAckHistory )
	{
		// Use the first inactive history item to build this change list on
		check( RepState->HistoryEnd - RepState->HistoryStart < FRepState::MAX_CHANGE_HISTORY );
		const int32 HistoryIndex = RepState->HistoryEnd % FRepState::MAX_CHANGE_HISTORY;

		FRepChangedHistory & NewHistoryItem = RepState->ChangeHistory[ HistoryIndex ];

		RepState->HistoryEnd++;

		TArray<uint16> & Changed = NewHistoryItem.Changed;

		check( Changed.Num() == 0 );		// Make sure this history item is actually inactive

		if ( PropertyChanged )
		{
			// Initialize the history item change list with the parent change lists
			// We do it in the order of the parents so that the final change list will be fully sorted
			for ( int32 i = 0, Parents_Num = Parents.Num(); i < Parents_Num; ++i )
			{
				TArray<uint16>& Parents_Changed = ChangeTracker->Parents[i].Changed;

				if ( Parents_Changed.Num() > 0 )
				{
					Changed.Append( Parents_Changed );

					if ( Parents[i].Flags & PARENT_IsConditional )
					{
						// Reset properties that don't share information across connections
						Parents_Changed.Empty();
					}
				}
			}

			Changed.Add( 0 );

#ifdef SANITY_CHECK_MERGES
			SanityCheckChangeList( Data, Changed );
#endif
		}

		// Update the history, and merge in any nak'd change lists
		UpdateChangelistHistory( RepState, ObjectClass, Data, OwningChannel->Connection->OutAckPacketId, &Changed );

		// Merge in the PreOpenAckHistory (unreliable properties sent before the bunch was initially acked)
		if ( bFlushPreOpenAckHistory )
		{
			for ( int32 i = 0; i < RepState->PreOpenAckHistory.Num(); i++ )
			{
				TArray< uint16 > Temp = Changed;
				Changed.Empty();
				MergeDirtyList( RepState, (void*)Data, Temp, RepState->PreOpenAckHistory[i].Changed, Changed );
			}
			RepState->PreOpenAckHistory.Empty();
		}

		// At this point we should have a non empty change list
		check( Changed.Num() > 0 );

#ifdef SANITY_CHECK_MERGES
		SanityCheckChangeList( Data, Changed );
#endif

		// Send the final merged change list
		if ( OwningChannel->Connection->InternalAck )
		{
			// Remember all properties that have changed since this channel was first opened in case we need it (for bResendAllDataSinceOpen)
			TArray< uint16 > Temp = RepState->LifetimeChangelist;
			MergeDirtyList( RepState, ( void* )Data, Temp, Changed, RepState->LifetimeChangelist );

			SendProperties_BackwardsCompatible( RepState, Data, OwningChannel->Connection, Writer, Changed );
		}
		else
		{
			SendProperties( RepState, RepFlags, Data, ObjectClass, OwningChannel, Writer, Changed );
		}

#ifdef ENABLE_SUPER_CHECKSUMS
		Writer.WriteBit( bIsAllAcked ? 1 : 0 );

		if ( bIsAllAcked )
		{
			ValidateWithChecksum( RepState->StaticBuffer.GetData(), Writer, false );
		}
#endif
		
		return true;
	}

	// Nothing changed and there are no nak's, so just do normal housekeeping and remove acked history items
	UpdateChangelistHistory( RepState, ObjectClass, Data, OwningChannel->Connection->OutAckPacketId, NULL );

	return false;
}

void FRepLayout::UpdateChangelistHistory( FRepState * RepState, UClass * ObjectClass, const uint8* RESTRICT Data, const int32 AckPacketId, TArray< uint16 > * OutMerged ) const
{
	check( RepState->HistoryEnd >= RepState->HistoryStart );

	const int32 HistoryCount	= RepState->HistoryEnd - RepState->HistoryStart;
	const bool DumpHistory		= HistoryCount == FRepState::MAX_CHANGE_HISTORY;

	// If our buffer is currently full, forcibly send the entire history
	if ( DumpHistory )
	{
		UE_LOG( LogRep, Log, TEXT( "FRepLayout::UpdateChangelistHistory: History overflow, forcing history dump %s" ), *ObjectClass->GetName() );
	}

	for ( int32 i = RepState->HistoryStart; i < RepState->HistoryEnd; i++ )
	{
		const int32 HistoryIndex = i % FRepState::MAX_CHANGE_HISTORY;

		FRepChangedHistory & HistoryItem = RepState->ChangeHistory[ HistoryIndex ];

		if ( HistoryItem.OutPacketIdRange.First == INDEX_NONE )
		{
			continue;		//  Hasn't been initialized in PostReplicate yet
		}

		check( HistoryItem.Changed.Num() > 0 );		// All active history items should contain a change list

		if ( AckPacketId >= HistoryItem.OutPacketIdRange.Last || HistoryItem.Resend || DumpHistory )
		{
			if ( HistoryItem.Resend || DumpHistory )
			{
				// Merge in nak'd change lists
				check( OutMerged != NULL );
				TArray< uint16 > Temp = *OutMerged;
				OutMerged->Empty();
				MergeDirtyList( RepState, (void*)Data, Temp, HistoryItem.Changed, *OutMerged );
				HistoryItem.Changed.Empty();

#ifdef SANITY_CHECK_MERGES
				SanityCheckChangeList( Data, *OutMerged );
#endif

				if ( HistoryItem.Resend )
				{
					HistoryItem.Resend = false;
					RepState->NumNaks--;
				}
			}

			HistoryItem.Changed.Empty();
			HistoryItem.OutPacketIdRange = FPacketIdRange();
			RepState->HistoryStart++;
		}
	}

	// Remove any tiling in the history markers to keep them from wrapping over time
	const int32 NewHistoryCount	= RepState->HistoryEnd - RepState->HistoryStart;

	check( NewHistoryCount <= FRepState::MAX_CHANGE_HISTORY );

	RepState->HistoryStart	= RepState->HistoryStart % FRepState::MAX_CHANGE_HISTORY;
	RepState->HistoryEnd	= RepState->HistoryStart + NewHistoryCount;

	check( RepState->NumNaks == 0 );	// Make sure we processed all the naks properly
}

void FRepLayout::OpenAcked( FRepState * RepState ) const
{
	check( RepState != NULL );
	RepState->OpenAckedCalled = true;
}

void FRepLayout::PostReplicate( FRepState * RepState, FPacketIdRange & PacketRange, bool bReliable ) const
{
	for ( int32 i = RepState->HistoryStart; i < RepState->HistoryEnd; i++ )
	{
		const int32 HistoryIndex = i % FRepState::MAX_CHANGE_HISTORY;

		FRepChangedHistory & HistoryItem = RepState->ChangeHistory[ HistoryIndex ];

		if ( HistoryItem.OutPacketIdRange.First == INDEX_NONE )
		{
			check( HistoryItem.Changed.Num() > 0 );
			check( !HistoryItem.Resend );

			HistoryItem.OutPacketIdRange = PacketRange;

			if ( !bReliable && !RepState->OpenAckedCalled )
			{
				RepState->PreOpenAckHistory.Add( HistoryItem );
			}
		}
	}
}

void FRepLayout::ReceivedNak( FRepState * RepState, int32 NakPacketId ) const
{
	if ( RepState == NULL )
	{
		return;		// I'm not 100% certain why this happens, the only think I can think of is this is a bNetTemporary?
	}

	for ( int32 i = RepState->HistoryStart; i < RepState->HistoryEnd; i++ )
	{
		const int32 HistoryIndex = i % FRepState::MAX_CHANGE_HISTORY;

		FRepChangedHistory & HistoryItem = RepState->ChangeHistory[ HistoryIndex ];

		if ( !HistoryItem.Resend && HistoryItem.OutPacketIdRange.InRange( NakPacketId ) )
		{
			check( HistoryItem.Changed.Num() > 0 );
			HistoryItem.Resend = true;
			RepState->NumNaks++;
		}
	}
}

bool FRepLayout::AllAcked( FRepState * RepState ) const
{	
	if ( RepState->HistoryStart != RepState->HistoryEnd )
	{
		// We have change lists that haven't been acked
		return false;
	}

	if ( RepState->NumNaks > 0 )
	{
		return false;
	}

	if ( !RepState->OpenAckedCalled )
	{
		return false;
	}

	if ( RepState->PreOpenAckHistory.Num() > 0 )
	{
		return false;
	}

	return true;
}

bool FRepLayout::ReadyForDormancy( FRepState * RepState ) const
{
	if ( RepState == NULL )
	{
		return false;
	}

	return AllAcked( RepState );
}

static FORCEINLINE void WritePropertyHandle( FNetBitWriter & Writer, uint16 Handle, bool bDoChecksum )
{
	const int NumStartingBits = Writer.GetNumBits();

	uint32 LocalHandle = Handle;
	Writer.SerializeIntPacked( LocalHandle );

#ifdef ENABLE_PROPERTY_CHECKSUMS
	if ( bDoChecksum )
	{
		SerializeGenericChecksum( Writer );
	}
#endif

	NETWORK_PROFILER(GNetworkProfiler.TrackWritePropertyHandle( Writer.GetNumBits() - NumStartingBits, nullptr ));
}

static bool ShouldSendProperty( FRepWriterState & WriterState, const uint16 Handle )
{
	if ( Handle == WriterState.Changed[WriterState.CurrentChanged] )
	{
		// Write out the handle
		WritePropertyHandle( WriterState.Writer, Handle, WriterState.bDoChecksum );

		// Advance to the next expected handle
		WriterState.CurrentChanged++;

		return true;
	}

	return false;
}

void FRepLayout::SendProperties_DynamicArray_r( 
	FRepState *	RESTRICT		RepState, 
	const FReplicationFlags &	RepFlags,
	FRepWriterState &			WriterState,
	const int32					CmdIndex, 
	const uint8* RESTRICT		StoredData, 
	const uint8* RESTRICT		Data, 
	uint16						Handle ) const
{
	const FRepLayoutCmd& Cmd = Cmds[ CmdIndex ];

	FScriptArray * Array = (FScriptArray *)Data;
	FScriptArray * StoredArray = (FScriptArray *)StoredData;

	// Write array num
	uint16 ArrayNum = Array->Num();
	WriterState.Writer << ArrayNum;

	// Make the shadow state match the actual state at the time of send
	FScriptArrayHelper StoredArrayHelper( (UArrayProperty *)Cmd.Property, StoredData );
	StoredArrayHelper.Resize( ArrayNum );

	// Read the jump offset
	// We won't need to actually jump over anything because we expect the change list to be pruned once we get here
	// But we can use it to verify we read the correct amount.
	const int32 ArrayChangedCount = WriterState.Changed[WriterState.CurrentChanged++];

	const int32 OldChangedIndex = WriterState.CurrentChanged;

	Data = (uint8*)Array->GetData();
	StoredData = (uint8*)StoredArray->GetData();

	uint16 LocalHandle = 0;

	for ( int32 i = 0; i < Array->Num(); i++ )
	{
		const int32 ElementOffset = i * Cmd.ElementSize;
		LocalHandle = SendProperties_r( RepState, RepFlags, WriterState, CmdIndex + 1, Cmd.EndCmd - 1, StoredData + ElementOffset, Data + ElementOffset, LocalHandle );
	}

	check( WriterState.CurrentChanged - OldChangedIndex == ArrayChangedCount );	// Make sure we read correct amount
	check( WriterState.Changed[WriterState.CurrentChanged] == 0 );				// Make sure we are at the end

	WriterState.CurrentChanged++;

	WritePropertyHandle( WriterState.Writer, 0, WriterState.bDoChecksum );		// Signify end of dynamic array
}

void FRepLayout::SerializeObjectReplicatedProperties(UObject* Object, FArchive & Ar) const
{
	for (int32 i = 0; i < Parents.Num(); i++)
	{
        UStructProperty* StructProperty = Cast<UStructProperty>(Parents[i].Property);
        UObjectProperty* ObjectProperty = Cast<UObjectProperty>(Parents[i].Property);

		// We're only able to easily serialize non-object/struct properties, so just do those.
		if (ObjectProperty == nullptr && StructProperty == nullptr)
		{
			bool bHasUnmapped = false;
			SerializeProperties_r(Ar, NULL, Parents[i].CmdStart, Parents[i].CmdEnd, (uint8*)Object, bHasUnmapped);
		}
	}
}

uint16 FRepLayout::SendProperties_r( 
	FRepState *	RESTRICT		RepState, 
	const FReplicationFlags &	RepFlags,
	FRepWriterState &			WriterState,
	const int32					CmdStart, 
	const int32					CmdEnd, 
	const uint8* RESTRICT		StoredData, 
	const uint8* RESTRICT		Data, 
	uint16						Handle ) const
{
	for ( int32 CmdIndex = CmdStart; CmdIndex < CmdEnd; CmdIndex++ )
	{
		const FRepLayoutCmd& Cmd = Cmds[ CmdIndex ];

		check( Cmd.Type != REPCMD_Return );

		Handle++;

		if ( Cmd.Type == REPCMD_DynamicArray )
		{
			if ( ShouldSendProperty( WriterState, Handle ) )
			{
				SendProperties_DynamicArray_r( RepState, RepFlags, WriterState, CmdIndex, StoredData + Cmd.Offset, Data + Cmd.Offset, Handle );
			}
			CmdIndex = Cmd.EndCmd - 1;	// Jump past children of this array (-1 for the ++ in the for loop)
			continue;
		}

		if ( ShouldSendProperty( WriterState, Handle ) )
		{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			if (CVarDoReplicationContextString->GetInt() > 0)
			{
				WriterState.Writer.PackageMap->SetDebugContextString( FString::Printf(TEXT("%s - %s"), *Owner->GetPathName(), *Cmd.Property->GetPathName() ) );
			}
#endif

			const int32 NumStartBits = WriterState.Writer.GetNumBits();
			
			// This property changed, so send it
			Cmd.Property->NetSerializeItem( WriterState.Writer, WriterState.Writer.PackageMap, (void*)( Data + Cmd.Offset ) );

			const int32 NumEndBits = WriterState.Writer.GetNumBits();

			const FRepParentCmd& ParentCmd = Parents[Cmd.ParentIndex];

			NETWORK_PROFILER( GNetworkProfiler.TrackReplicateProperty( ParentCmd.Property, NumEndBits - NumStartBits, nullptr ) );

			// Make the shadow state match the actual state at the time of send
			StoreProperty( Cmd, (void*)( StoredData + Cmd.Offset ), (const void*)( Data + Cmd.Offset ) );

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			if (CVarDoReplicationContextString->GetInt() > 0)
			{
				WriterState.Writer.PackageMap->ClearDebugContextString();
			}
#endif

#ifdef ENABLE_PROPERTY_CHECKSUMS
			if ( WriterState.bDoChecksum )
			{
				SerializeReadWritePropertyChecksum( Cmd, CmdIndex, Data + Cmd.Offset, WriterState.Writer );
			}
#endif
		}
	}

	return Handle;
}

void FRepLayout::SendProperties( 
	FRepState *	RESTRICT		RepState, 
	const FReplicationFlags &	RepFlags,
	const uint8* RESTRICT		Data, 
	UClass *					ObjectClass,
	UActorChannel	*			OwningChannel,
	FNetBitWriter&				Writer, 
	TArray< uint16 > &			Changed ) const
{
#ifdef ENABLE_PROPERTY_CHECKSUMS
	const bool bDoChecksum = CVarDoPropertyChecksum.GetValueOnGameThread() == 1;
#else
	const bool bDoChecksum = false;
#endif

	FRepWriterState WriterState( Writer, Changed, bDoChecksum );

#ifdef ENABLE_PROPERTY_CHECKSUMS
	Writer.WriteBit( bDoChecksum ? 1 : 0 );
#endif

	SendProperties_r( RepState, RepFlags, WriterState, 0, Cmds.Num() - 1, RepState->StaticBuffer.GetData(), Data, 0 );

	WritePropertyHandle( Writer, 0, bDoChecksum );
}

static FORCEINLINE void WritePropertyHandle_BackwardsCompatible( FNetBitWriter & Writer, uint32 NetFieldExportHandle, bool bDoChecksum )
{
	const int NumStartingBits = Writer.GetNumBits();

	Writer.SerializeIntPacked( NetFieldExportHandle );

#ifdef ENABLE_PROPERTY_CHECKSUMS
	if ( bDoChecksum )
	{
		SerializeGenericChecksum( Writer );
	}
#endif

	NETWORK_PROFILER( GNetworkProfiler.TrackWritePropertyHandle( Writer.GetNumBits() - NumStartingBits, nullptr ) );
}

static bool ShouldSendProperty_BackwardsCompatible( FRepWriterState & WriterState, FNetBitWriter & Writer, const uint16 Handle, const uint32 NetFieldExportHandle )
{
	if ( WriterState.Changed.Num() == 0 || Handle == WriterState.Changed[WriterState.CurrentChanged] )
	{
		// Write out the handle
		WritePropertyHandle_BackwardsCompatible( Writer, NetFieldExportHandle, WriterState.bDoChecksum );

		// Advance to the next expected handle
		WriterState.CurrentChanged++;

		return true;
	}

	return false;
}

void FRepLayout::SendProperties_BackwardsCompatible_DynamicArray_r(
	FRepState *	RESTRICT		RepState,
	FRepWriterState &			WriterState,
	FNetBitWriter &				Writer,
	UPackageMapClient*			PackageMapClient,
	FNetFieldExportGroup*		NetFieldExportGroup,
	const int32					CmdIndex,
	const uint8* RESTRICT		StoredData,
	const uint8* RESTRICT		Data ) const
{
	const FRepLayoutCmd& Cmd = Cmds[CmdIndex];

	FScriptArray * Array = ( FScriptArray * )Data;
	FScriptArray * StoredArray = ( FScriptArray * )StoredData;

	// Write array num
	uint32 ArrayNum = Array->Num();
	Writer.SerializeIntPacked( ArrayNum );

	// Make the shadow state match the actual state at the time of send
	if ( StoredData != nullptr )
	{
		FScriptArrayHelper StoredArrayHelper( ( UArrayProperty * )Cmd.Property, StoredData );
		StoredArrayHelper.Resize( ArrayNum );
	}

	// Read the jump offset
	// We won't need to actually jump over anything because we expect the change list to be pruned once we get here
	// But we can use it to verify we read the correct amount.
	const int32 ArrayChangedCount = WriterState.Changed.Num() > 0 ? WriterState.Changed[WriterState.CurrentChanged++] : -1;

	const int32 OldChangedIndex = WriterState.CurrentChanged;

	Data = ( uint8* )Array->GetData();
	StoredData = StoredArray != nullptr ? ( uint8* )StoredArray->GetData() : nullptr;

	uint16 LocalHandle = 0;

	for ( int32 i = 0; i < Array->Num(); i++ )
	{
		uint32 Index = i + 1;
		Writer.SerializeIntPacked( Index );
		const int32 ElementOffset = i * Cmd.ElementSize;
		LocalHandle = SendProperties_BackwardsCompatible_r( RepState, WriterState, Writer, PackageMapClient, NetFieldExportGroup, CmdIndex + 1, Cmd.EndCmd - 1, StoredData ? StoredData + ElementOffset : nullptr, Data + ElementOffset, LocalHandle );
	}

	uint32 Index = 0;
	Writer.SerializeIntPacked( Index );

	check( WriterState.Changed.Num() == 0 || WriterState.CurrentChanged - OldChangedIndex == ArrayChangedCount );							// Make sure we read correct amount
	check( WriterState.Changed.Num() == 0 || WriterState.Changed[WriterState.CurrentChanged] == 0 );	// Make sure we are at the end

	WriterState.CurrentChanged++;
}

uint16 FRepLayout::SendProperties_BackwardsCompatible_r(
	FRepState *	RESTRICT		RepState,
	FRepWriterState &			WriterState,
	FNetBitWriter &				Writer,
	UPackageMapClient*			PackageMapClient,
	FNetFieldExportGroup*		NetFieldExportGroup,
	const int32					CmdStart,
	const int32					CmdEnd,
	const uint8* RESTRICT		StoredData,
	const uint8* RESTRICT		Data,
	uint16						Handle ) const
{
	for ( int32 CmdIndex = CmdStart; CmdIndex < CmdEnd; CmdIndex++ )
	{
		const FRepLayoutCmd& Cmd = Cmds[CmdIndex];

		check( Cmd.Type != REPCMD_Return );

		Handle++;

		if ( Cmd.Type == REPCMD_DynamicArray )
		{
			if ( ShouldSendProperty_BackwardsCompatible( WriterState, Writer, Handle, CmdIndex + 1 ) )
			{
				PackageMapClient->TrackNetFieldExport( NetFieldExportGroup, CmdIndex );

				FNetBitWriter TempWriter( Writer.PackageMap, 0 );
				SendProperties_BackwardsCompatible_DynamicArray_r( RepState, WriterState, TempWriter, PackageMapClient, NetFieldExportGroup, CmdIndex, StoredData ? StoredData + Cmd.Offset : nullptr, Data + Cmd.Offset );

				uint32 NumBits = TempWriter.GetNumBits();
				Writer.SerializeIntPacked( NumBits );
				Writer.SerializeBits( TempWriter.GetData(), NumBits );
			}
			CmdIndex = Cmd.EndCmd - 1;	// Jump past children of this array (-1 for the ++ in the for loop)
			continue;
		}

		if ( ShouldSendProperty_BackwardsCompatible( WriterState, Writer, Handle, CmdIndex + 1 ) )
		{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			if ( CVarDoReplicationContextString->GetInt() > 0 )
			{
				Writer.PackageMap->SetDebugContextString( FString::Printf( TEXT( "%s - %s" ), *Owner->GetPathName(), *Cmd.Property->GetPathName() ) );
			}
#endif
			PackageMapClient->TrackNetFieldExport( NetFieldExportGroup, CmdIndex );

			const int32 NumStartBits = Writer.GetNumBits();

			// This property changed, so send it
			FNetBitWriter TempWriter( Writer.PackageMap, 0 );

			Cmd.Property->NetSerializeItem( TempWriter, TempWriter.PackageMap, ( void* )( Data + Cmd.Offset ) );

			uint32 NumBits = TempWriter.GetNumBits();
			Writer.SerializeIntPacked( NumBits );
			Writer.SerializeBits( TempWriter.GetData(), NumBits );

			const int32 NumEndBits = Writer.GetNumBits();

			const FRepParentCmd& ParentCmd = Parents[Cmd.ParentIndex];

			NETWORK_PROFILER( GNetworkProfiler.TrackReplicateProperty( ParentCmd.Property, NumEndBits - NumStartBits, nullptr ) );

			// Make the shadow state match the actual state at the time of send
			if ( StoredData != nullptr )
			{
				StoreProperty( Cmd, ( void* )( StoredData + Cmd.Offset ), ( const void* )( Data + Cmd.Offset ) );
			}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			if ( CVarDoReplicationContextString->GetInt() > 0 )
			{
				Writer.PackageMap->ClearDebugContextString();
			}
#endif

#ifdef ENABLE_PROPERTY_CHECKSUMS
			if ( WriterState.bDoChecksum )
			{
				SerializeReadWritePropertyChecksum( Cmd, CmdIndex, Data + Cmd.Offset, Writer );
			}
#endif
		}
	}

	WritePropertyHandle_BackwardsCompatible( Writer, 0, WriterState.bDoChecksum );

	return Handle;
}

TSharedPtr< FNetFieldExportGroup > FRepLayout::CreateNetfieldExportGroup() const
{
	TSharedPtr< FNetFieldExportGroup > NetFieldExportGroup = TSharedPtr< FNetFieldExportGroup >( new FNetFieldExportGroup() );

	NetFieldExportGroup->PathName = Owner->GetPathName();
	NetFieldExportGroup->NetFieldExports.SetNum( Cmds.Num() );

	for ( int32 i = 0; i < Cmds.Num(); i++ )
	{
		FNetFieldExport NetFieldExport(
			i,
			Cmds[i].CompatibleChecksum,
			Cmds[i].Property ? Cmds[i].Property->GetName() : TEXT( "" ),
			Cmds[i].Property ? Cmds[i].Property->GetCPPType( nullptr, 0 ) : TEXT( "" ) );

		NetFieldExportGroup->NetFieldExports[i] = NetFieldExport;
	}

	return NetFieldExportGroup;
}

void FRepLayout::SendProperties_BackwardsCompatible(
	FRepState* RESTRICT			RepState,
	const uint8* RESTRICT		Data,
	UNetConnection*				Connection,
	FNetBitWriter&				Writer,
	TArray< uint16 >&			Changed ) const
{
#ifdef ENABLE_PROPERTY_CHECKSUMS
	const bool bDoChecksum = CVarDoPropertyChecksum.GetValueOnGameThread() == 1;
	Writer.WriteBit( bDoChecksum ? 1 : 0 );
#else
	const bool bDoChecksum = false;
#endif

	UPackageMapClient* PackageMapClient = ( UPackageMapClient* )Connection->PackageMap;

	TSharedPtr< FNetFieldExportGroup > NetFieldExportGroup = PackageMapClient->GetNetFieldExportGroup( Owner->GetPathName() );

	if ( !NetFieldExportGroup.IsValid() )
	{
		NetFieldExportGroup = CreateNetfieldExportGroup();

		PackageMapClient->AddNetFieldExportGroup( Owner->GetPathName(), NetFieldExportGroup );
	}

	FRepWriterState WriterState( Writer, Changed, bDoChecksum );

	SendProperties_BackwardsCompatible_r( RepState, WriterState, Writer, PackageMapClient, NetFieldExportGroup.Get(), 0, Cmds.Num() - 1, RepState ? RepState->StaticBuffer.GetData() : nullptr, Data, 0 );
}

class FReceivedPropertiesStackState : public FCmdIteratorBaseStackState
{
public:
	FReceivedPropertiesStackState( const int32 InCmdStart, const int32 InCmdEnd, FScriptArray*	InShadowArray, FScriptArray* InDataArray, uint8* RESTRICT InShadowBaseData, uint8* RESTRICT	InBaseData ) : 
		FCmdIteratorBaseStackState( InCmdStart, InCmdEnd, InShadowArray, InDataArray, InShadowBaseData, InBaseData ),
        UnmappedGuids( NULL )
	{}

	FUnmappedGuidMgr* UnmappedGuids;
};

static bool ReceivePropertyHelper( 
	FNetBitReader&					Bunch, 
	FUnmappedGuidMgr*				UnmappedGuids,
	const int32						ElementOffset, 
	uint8* RESTRICT					ShadowData,
	uint8* RESTRICT					Data,
	TArray< UProperty * >*			RepNotifies,
	const TArray< FRepParentCmd >&	Parents,
	const TArray< FRepLayoutCmd >&	Cmds,
	const int32						CmdIndex,
	const bool						bDoChecksum )
{
	const FRepLayoutCmd& Cmd	= Cmds[CmdIndex];
	const FRepParentCmd& Parent = Parents[Cmd.ParentIndex];

	// This swaps Role/RemoteRole as we write it
	const FRepLayoutCmd& SwappedCmd = Parent.RoleSwapIndex != -1 ? Cmds[Parents[Parent.RoleSwapIndex].CmdStart] : Cmd;

	if ( UnmappedGuids )		// Don't reset unmapped guids here if we are told not to (assuming calling code is handling this)
	{
		// Let package map know we want to track and know about any guids that are unmapped during the serialize call
		Bunch.PackageMap->ResetTrackedUnmappedGuids( true );
	}

	// Remember where we started reading from, so that if we have unmapped properties, we can re-deserialize from this data later
	FBitReaderMark Mark( Bunch );

	if ( RepNotifies != nullptr && Parent.Property->HasAnyPropertyFlags( CPF_RepNotify ) )
	{
		// Copy current value over so we can check to see if it changed
		StoreProperty( Cmd, ShadowData + Cmd.Offset, Data + SwappedCmd.Offset );

		// Read the property
		Cmd.Property->NetSerializeItem( Bunch, Bunch.PackageMap, Data + SwappedCmd.Offset );

		// Check to see if this property changed
		if ( Parent.RepNotifyCondition == REPNOTIFY_Always || !PropertiesAreIdentical( Cmd, ShadowData + Cmd.Offset, Data + SwappedCmd.Offset ) )
		{
			(*RepNotifies).AddUnique( Parent.Property );
		}
		else
		{
			UE_CLOG( LogSkippedRepNotifies > 0, LogRep, Display, TEXT( "2 FReceivedPropertiesStackState Skipping RepNotify for propery %s because local value has not changed." ), *Cmd.Property->GetName() );
		}
	}
	else
	{
		Cmd.Property->NetSerializeItem( Bunch, Bunch.PackageMap, Data + SwappedCmd.Offset );
	}

#ifdef ENABLE_PROPERTY_CHECKSUMS
	if ( bDoChecksum )
	{
		SerializeReadWritePropertyChecksum( Cmd, CmdIndex, Data + SwappedCmd.Offset, Bunch );
	}
#endif

	if ( UnmappedGuids )
	{
		const int32 AbsOffset = ElementOffset + SwappedCmd.Offset;

		const TArray< FNetworkGUID > & TrackedUnmappedGuids = Bunch.PackageMap->GetTrackedUnmappedGuids();

		bool bHasUnmapped = false;

		if ( TrackedUnmappedGuids.Num() > 0 )
		{
			// If we have an unmapped guid, we need to remember it, so we can fix up this object pointer when it finally arrives at a other time
			// Note - If we already have an existing entry for this offset, we will replace it with this most recent data
			UnmappedGuids->Map.Add( AbsOffset, FUnmappedGuidMgrElement( Bunch, Mark, TrackedUnmappedGuids, Cmd.ParentIndex, CmdIndex ) );

			UE_LOG( LogRep, Verbose, TEXT( "ADDED unmapped property: Offset: %i, Name: %s" ), AbsOffset, *Cmd.Property->GetName() );

			// List all the guids that were unmapped
			for ( int32 i = 0; i < TrackedUnmappedGuids.Num(); i++ )
			{
				UE_LOG( LogRep, Verbose, TEXT( "  Guid: %s" ), *TrackedUnmappedGuids[i].ToString() );
			}

			bHasUnmapped = true;
		}
		else
		{
			// If we don't have any unmapped guids, then make sure to remove the entry so we don't serialize old data when we update unmapped objects
			UnmappedGuids->Map.Remove( AbsOffset );
		}

		// Stop tracking unmapped objects
		Bunch.PackageMap->ResetTrackedUnmappedGuids( false );

		return bHasUnmapped;
	}

	return false;
}

static FUnmappedGuidMgr* PrepReceivedArray(
	const int32				ArrayNum,
	FScriptArray*			ShadowArray,
	FScriptArray*			DataArray,
	FUnmappedGuidMgr*		ParentUnmappedGuids,
	const int32				AbsOffset,
	const FRepParentCmd&	Parent, 
	const FRepLayoutCmd&	Cmd, 
	const int32				CmdIndex,
	uint8* RESTRICT*		OutShadowBaseData,
	uint8* RESTRICT*		OutBaseData,
	TArray< UProperty * >*	RepNotifies )
{
	FUnmappedGuidMgrElement * NewArrayElement = nullptr;

	if ( ParentUnmappedGuids != nullptr )
	{
		// Since we don't know yet if something under us could be unmapped, go ahead and allocate an array container now
		NewArrayElement = ParentUnmappedGuids->Map.Find( AbsOffset );

		if ( NewArrayElement == NULL )
		{
			NewArrayElement = &ParentUnmappedGuids->Map.FindOrAdd( AbsOffset );

			NewArrayElement->Array			= new FUnmappedGuidMgr;
			NewArrayElement->ParentIndex	= Cmd.ParentIndex;
			NewArrayElement->CmdIndex		= CmdIndex;
		}

		check( NewArrayElement != NULL );
		check( NewArrayElement->ParentIndex == Cmd.ParentIndex );
		check( NewArrayElement->CmdIndex == CmdIndex );
	}

	if ( RepNotifies != nullptr )
	{
		if ( ( DataArray->Num() != ArrayNum || Parent.RepNotifyCondition == REPNOTIFY_Always ) && Parent.Property->HasAnyPropertyFlags( CPF_RepNotify ) )
		{
			( *RepNotifies ).AddUnique( Parent.Property );
		}
		else
		{
			UE_CLOG( LogSkippedRepNotifies > 0, LogRep, Display, TEXT( "1 FReceivedPropertiesStackState Skipping RepNotify for propery %s because local value has not changed." ), *Cmd.Property->GetName() );
		}
	}

	check( CastChecked< UArrayProperty >( Cmd.Property ) != NULL );

	// Resize arrays if needed
	FScriptArrayHelper ArrayHelper( ( UArrayProperty * )Cmd.Property, DataArray );
	ArrayHelper.Resize( ArrayNum );

	// Re-compute the base data values since they could have changed after the resize above
	*OutBaseData		= ( uint8* )DataArray->GetData();
	*OutShadowBaseData	= nullptr;

	// Only resize the shadow data array if we're actually tracking RepNotifies
	if ( RepNotifies != nullptr )
	{
		check( ShadowArray != nullptr );

		FScriptArrayHelper ShadowArrayHelper( ( UArrayProperty* )Cmd.Property, ShadowArray );
		ShadowArrayHelper.Resize( ArrayNum );

		*OutShadowBaseData = ( uint8* )ShadowArray->GetData();
	}

	return NewArrayElement ? NewArrayElement->Array : nullptr;
}

class FReceivePropertiesImpl : public FRepLayoutCmdIterator< FReceivePropertiesImpl, FReceivedPropertiesStackState >
{
public:
	FReceivePropertiesImpl( FNetBitReader & InBunch, FRepState * InRepState, bool bInDoChecksum, const TArray< FRepParentCmd >& InParents, const TArray< FRepLayoutCmd >& InCmds, const bool bInDoRepNotify ) :
        FRepLayoutCmdIterator( InParents, InCmds ),
		WaitingHandle( 0 ),
		CurrentHandle( 0 ), 
		Bunch( InBunch ),
		RepState( InRepState ),
		bDoChecksum( bInDoChecksum ),
		bHasUnmapped( false ),
		bDoRepNotify( bInDoRepNotify )
	{}

	void ReadNextHandle()
	{
		Bunch.SerializeIntPacked( WaitingHandle );

#ifdef ENABLE_PROPERTY_CHECKSUMS
		if ( bDoChecksum )
		{
			SerializeGenericChecksum( Bunch );
		}
#endif
	}

	INIT_STACK( FReceivedPropertiesStackState )
	{
		StackState.UnmappedGuids = &RepState->UnmappedGuids;
	}

	SHOULD_PROCESS_NEXT_CMD()
	{
		CurrentHandle++;

		if ( CurrentHandle == WaitingHandle )
		{
			check( WaitingHandle != 0 );
			return true;
		}

		return false;
	}

	PROCESS_ARRAY_CMD( FReceivedPropertiesStackState )
	{
		// Read array size
		uint16 ArrayNum = 0;
		Bunch << ArrayNum;

		// Read the next property handle
		ReadNextHandle();

		const int32 AbsOffset = Data - PrevStackState.BaseData;

		const FRepParentCmd& Parent = Parents[Cmd.ParentIndex];

		StackState.UnmappedGuids = PrepReceivedArray( 
			ArrayNum,
			StackState.ShadowArray,
			StackState.DataArray,
			PrevStackState.UnmappedGuids,
			AbsOffset,
			Parent,
			Cmd,
			CmdIndex,
			&StackState.ShadowBaseData,
			&StackState.BaseData,
			bDoRepNotify ? &RepState->RepNotifies : nullptr );

		// Save the old handle so we can restore it when we pop out of the array
		const uint16 OldHandle = CurrentHandle;

		// Array children handles are always relative to their immediate parent
		CurrentHandle = 0;

		// Loop over array
		ProcessDataArrayElements_r( StackState, Cmd );

		// Restore the current handle to what it was before we processed this array
		CurrentHandle = OldHandle;

		// We should be waiting on the NULL terminator handle at this point
		check( WaitingHandle == 0 );
		ReadNextHandle();
	}

	PROCESS_CMD( FReceivedPropertiesStackState )
	{
		check( StackState.UnmappedGuids != NULL );

		const int32 ElementOffset = ( Data - StackState.BaseData );

		if ( ReceivePropertyHelper( Bunch, StackState.UnmappedGuids, ElementOffset, ShadowData, Data, bDoRepNotify ? &RepState->RepNotifies : nullptr, Parents, Cmds, CmdIndex, bDoChecksum ) )
		{
			bHasUnmapped = true;
		}

		// Read the next property handle
		ReadNextHandle();
	}

	uint32					WaitingHandle;
	uint32					CurrentHandle;
	FNetBitReader &			Bunch;
	FRepState *				RepState;
	bool					bDoChecksum;
	bool					bHasUnmapped;
	bool					bDoRepNotify;
};

bool FRepLayout::ReceiveProperties( UActorChannel* OwningChannel, UClass * InObjectClass, FRepState * RESTRICT RepState, void* RESTRICT Data, FNetBitReader & InBunch, bool & bOutHasUnmapped, const bool bEnableRepNotifies ) const
{
	check( InObjectClass == Owner );

#ifdef ENABLE_PROPERTY_CHECKSUMS
	const bool bDoChecksum = InBunch.ReadBit() ? true : false;
#else
	const bool bDoChecksum = false;
#endif

	bOutHasUnmapped = false;

	if ( OwningChannel->Connection->InternalAck )
	{
		TSharedPtr< FNetFieldExportGroup > NetFieldExportGroup = ( ( UPackageMapClient* )OwningChannel->Connection->PackageMap )->GetNetFieldExportGroupChecked( Owner->GetPathName() );

		return ReceiveProperties_BackwardsCompatible_r( RepState, NetFieldExportGroup.Get(), InBunch, 0, Cmds.Num() - 1, bEnableRepNotifies ? RepState->StaticBuffer.GetData() : nullptr, ( uint8* )Data, ( uint8* )Data, &RepState->UnmappedGuids, bOutHasUnmapped );
	}

	FReceivePropertiesImpl ReceivePropertiesImpl( InBunch, RepState, bDoChecksum, Parents, Cmds, bEnableRepNotifies );

	// Read first handle
	ReceivePropertiesImpl.ReadNextHandle();

	// Read all properties
	ReceivePropertiesImpl.ProcessCmds( (uint8*)Data, RepState->StaticBuffer.GetData() );

	// Make sure we're waiting on the last NULL terminator
	if ( ReceivePropertiesImpl.WaitingHandle != 0 )
	{
		UE_LOG( LogRep, Warning, TEXT( "Read out of sync." ) );
		return false;
	}

#ifdef ENABLE_SUPER_CHECKSUMS
	if ( InBunch.ReadBit() == 1 )
	{
		ValidateWithChecksum( RepState->StaticBuffer.GetData(), InBunch );
	}
#endif

	bOutHasUnmapped = ReceivePropertiesImpl.bHasUnmapped;

	return true;
}

bool FRepLayout::ReceiveProperties_BackwardsCompatible( UNetConnection* Connection, FRepState * RESTRICT RepState, void* RESTRICT Data, FNetBitReader & InBunch, bool & bOutHasUnmapped, const bool bEnableRepNotifies ) const
{
#ifdef ENABLE_PROPERTY_CHECKSUMS
	const bool bDoChecksum = InBunch.ReadBit() ? true : false;
#else
	const bool bDoChecksum = false;
#endif

	bOutHasUnmapped = false;

	TSharedPtr< FNetFieldExportGroup > NetFieldExportGroup = ( ( UPackageMapClient* )Connection->PackageMap )->GetNetFieldExportGroup( Owner->GetPathName() );

	return ReceiveProperties_BackwardsCompatible_r( RepState, NetFieldExportGroup.Get(), InBunch, 0, Cmds.Num() - 1, (bEnableRepNotifies && RepState) ? RepState->StaticBuffer.GetData() : nullptr, ( uint8* )Data, ( uint8* )Data, RepState ? &RepState->UnmappedGuids : nullptr, bOutHasUnmapped );
}

int32 FRepLayout::FindCompatibleProperty( const int32 CmdStart, const int32 CmdEnd, const uint32 Checksum ) const
{
	for ( int32 CmdIndex = CmdStart; CmdIndex < CmdEnd; CmdIndex++ )
	{
		const FRepLayoutCmd& Cmd = Cmds[CmdIndex];

		check( Cmd.Type != REPCMD_Return );

		if ( Cmd.CompatibleChecksum == Checksum )
		{
			return CmdIndex;
		}

		// Jump over entire array and inner properties if checksum didn't match
		if ( Cmd.Type == REPCMD_DynamicArray )
		{
			CmdIndex = Cmd.EndCmd - 1;
		}
	}

	return -1;
}

bool FRepLayout::ReceiveProperties_BackwardsCompatible_r( 
	FRepState * RESTRICT	RepState,
	FNetFieldExportGroup*	NetFieldExportGroup,
	FNetBitReader &			Reader,
	const int32				CmdStart,
	const int32				CmdEnd,
	uint8* RESTRICT			ShadowData,
	uint8* RESTRICT			OldData,
	uint8* RESTRICT			Data,
	FUnmappedGuidMgr*		UnmappedGuids,
	bool &					bOutHasUnmapped ) const
{
	while ( true )
	{
		uint32 NetFieldExportHandle = 0;
		Reader.SerializeIntPacked( NetFieldExportHandle );

		if ( Reader.IsError() )
		{
			UE_LOG( LogRep, Warning, TEXT( "ReceiveProperties_BackwardsCompatible_r: Error reading handle. Owner: %s" ), *Owner->GetName() );
			return false;
		}

		if ( NetFieldExportHandle == 0 )
		{
			// We're done
			break;
		}

		check( NetFieldExportGroup != nullptr );

		// We purposely add 1 on save, so we can reserve 0 for "done"
		NetFieldExportHandle--;

		if ( !ensure( NetFieldExportHandle < ( uint32 )NetFieldExportGroup->NetFieldExports.Num() ) )
		{
			UE_LOG( LogRep, Warning, TEXT( "ReceiveProperties_BackwardsCompatible_r: NetFieldExportHandle > NetFieldExportGroup->NetFieldExports.Num(). Owner: %s, NetFieldExportHandle: %u" ), *Owner->GetName(), NetFieldExportHandle );
			return false;
		}

		const uint32 Checksum = NetFieldExportGroup->NetFieldExports[NetFieldExportHandle].CompatibleChecksum;

		if ( !ensure( Checksum != 0 ) )
		{
			UE_LOG( LogRep, Warning, TEXT( "ReceiveProperties_BackwardsCompatible_r: Checksum == 0. Owner: %s, Name: %s, Type: %s, NetFieldExportHandle: %i" ), *Owner->GetName(), *NetFieldExportGroup->NetFieldExports[NetFieldExportHandle].Name, *NetFieldExportGroup->NetFieldExports[NetFieldExportHandle].Type, NetFieldExportHandle );
			return false;
		}

		uint32 NumBits = 0;
		Reader.SerializeIntPacked( NumBits );

		if ( Reader.IsError() )
		{
			UE_LOG( LogRep, Warning, TEXT( "ReceiveProperties_BackwardsCompatible_r: Error reading num bits. Owner: %s, Name: %s, Type: %s, NetFieldExportHandle: %i, Checksum: %u" ), *Owner->GetName(), *NetFieldExportGroup->NetFieldExports[NetFieldExportHandle].Name, *NetFieldExportGroup->NetFieldExports[NetFieldExportHandle].Type, NetFieldExportHandle, Checksum );
			return false;
		}

		FNetBitReader TempReader;
		
		TempReader.PackageMap = Reader.PackageMap;
		TempReader.SetData( Reader, NumBits );

		if ( Reader.IsError() )
		{
			UE_LOG( LogRep, Warning, TEXT( "ReceiveProperties_BackwardsCompatible_r: Error reading payload. Owner: %s, Name: %s, Type: %s, NetFieldExportHandle: %i, Checksum: %u" ), *Owner->GetName(), *NetFieldExportGroup->NetFieldExports[NetFieldExportHandle].Name, *NetFieldExportGroup->NetFieldExports[NetFieldExportHandle].Type, NetFieldExportHandle, Checksum );
			return false;
		}

		if ( NetFieldExportGroup->NetFieldExports[NetFieldExportHandle].bIncompatible )
		{
			continue;		// We've already warned that this property doesn't load anymore
		}

		// Find this property
		const int32 CmdIndex = FindCompatibleProperty( CmdStart, CmdEnd, Checksum );

		if ( CmdIndex == -1 )
		{
			UE_LOG( LogRep, Warning, TEXT( "ReceiveProperties_BackwardsCompatible_r: Property not found. Owner: %s, Name: %s, Type: %s, NetFieldExportHandle: %i, Checksum: %u" ), *Owner->GetName(), *NetFieldExportGroup->NetFieldExports[NetFieldExportHandle].Name, *NetFieldExportGroup->NetFieldExports[NetFieldExportHandle].Type, NetFieldExportHandle, Checksum );

			// Mark this property as incompatible so we don't keep spamming this warning
			NetFieldExportGroup->NetFieldExports[NetFieldExportHandle].bIncompatible = true;
			continue;
		}

		const FRepLayoutCmd& Cmd = Cmds[CmdIndex];

		if ( Cmd.Type == REPCMD_DynamicArray )
		{
			uint32 ArrayNum = 0;
			TempReader.SerializeIntPacked( ArrayNum );

			if ( TempReader.IsError() )
			{
				return false;
			}

			const int32 AbsOffset = ( Data - OldData ) + Cmd.Offset;

			FScriptArray* DataArray		= ( FScriptArray* )( Data + Cmd.Offset );
			FScriptArray* ShadowArray	= ShadowData ? ( FScriptArray* )( ShadowData + Cmd.Offset ) : nullptr;

			uint8* LocalData			= Data;
			uint8* LocalShadowData		= ShadowData;

			FUnmappedGuidMgr* ArrayUnmappedGuids = PrepReceivedArray(
				ArrayNum,
				ShadowArray,
				DataArray,
				UnmappedGuids,
				AbsOffset,
				Parents[Cmd.ParentIndex],
				Cmd,
				CmdIndex,
				&LocalShadowData,
				&LocalData,
				ShadowData != nullptr ? &RepState->RepNotifies : nullptr );

			// Read until we read all array elements
			while ( true )
			{
				uint32 Index = 0;
				TempReader.SerializeIntPacked( Index );

				if ( TempReader.IsError() )
				{
					UE_LOG( LogRep, Warning, TEXT( "ReceiveProperties_BackwardsCompatible_r: Error reading array index. Index: %i, Owner: %s, Name: %s, Type: %s, NetFieldExportHandle: %i, Checksum: %u" ), Index, *Owner->GetName(), *NetFieldExportGroup->NetFieldExports[NetFieldExportHandle].Name, *NetFieldExportGroup->NetFieldExports[NetFieldExportHandle].Type, NetFieldExportHandle, Checksum );
					return false;
				}

				if ( Index == 0 )
				{
					// We're done
					break;
				}

				const int32 ElementOffset = ( Index - 1 ) * Cmd.ElementSize;

				uint8* ElementData			= LocalData + ElementOffset;
				uint8* ElementShadowData	= LocalShadowData ? LocalShadowData + ElementOffset : nullptr;

				if ( !ReceiveProperties_BackwardsCompatible_r( RepState, NetFieldExportGroup, TempReader, CmdIndex + 1, Cmd.EndCmd - 1, ElementShadowData, LocalData, ElementData, ArrayUnmappedGuids, bOutHasUnmapped ) )
				{
					return false;
				}

				if ( TempReader.IsError() )
				{
					UE_LOG( LogRep, Warning, TEXT( "ReceiveProperties_BackwardsCompatible_r: Error reading array index element payload. Index: %i, Owner: %s, Name: %s, Type: %s, NetFieldExportHandle: %i, Checksum: %u" ), Index, *Owner->GetName(), *NetFieldExportGroup->NetFieldExports[NetFieldExportHandle].Name, *NetFieldExportGroup->NetFieldExports[NetFieldExportHandle].Type, NetFieldExportHandle, Checksum );
					return false;
				}
			}

			if ( TempReader.GetBitsLeft() != 0 )
			{
				UE_LOG( LogRep, Warning, TEXT( "ReceiveProperties_BackwardsCompatible_r: Array didn't read propery number of bits. Owner: %s, Name: %s, Type: %s, NetFieldExportHandle: %i, Checksum: %u" ), *Owner->GetName(), *NetFieldExportGroup->NetFieldExports[NetFieldExportHandle].Name, *NetFieldExportGroup->NetFieldExports[NetFieldExportHandle].Type, NetFieldExportHandle, Checksum );
				return false;
			}
		}
		else
		{
			const int32 ElementOffset = ( Data - OldData );

			if ( ReceivePropertyHelper( TempReader, UnmappedGuids, ElementOffset, ShadowData, Data, ShadowData != nullptr ? &RepState->RepNotifies : nullptr, Parents, Cmds, CmdIndex, false ) )
			{
				bOutHasUnmapped = true;
			}

			if ( TempReader.GetBitsLeft() != 0 )
			{
				UE_LOG( LogRep, Warning, TEXT( "ReceiveProperties_BackwardsCompatible_r: Property didn't read propery number of bits. Owner: %s, Name: %s, Type: %s, NetFieldExportHandle: %i, Checksum: %u" ), *Owner->GetName(), *NetFieldExportGroup->NetFieldExports[NetFieldExportHandle].Name, *NetFieldExportGroup->NetFieldExports[NetFieldExportHandle].Type, NetFieldExportHandle, Checksum );
				return false;
			}
		}
	}

	return true;
}

FUnmappedGuidMgrElement::~FUnmappedGuidMgrElement()
{
	if ( Array != NULL )
	{
		delete Array;
		Array = NULL;
	}
}

void FRepLayout::UpdateUnmappedObjects_r( 
	FRepState *			RepState, 
	FUnmappedGuidMgr *	UnmappedGuids, 
	UObject *			OriginalObject,
	UPackageMap *		PackageMap, 
	uint8* RESTRICT		StoredData, 
	uint8* RESTRICT		Data, 
	const int32			MaxAbsOffset,
	bool &				bOutSomeObjectsWereMapped,
	bool &				bOutHasMoreUnmapped ) const
{
	for ( auto It = UnmappedGuids->Map.CreateIterator(); It; ++It )
	{
		const int32 AbsOffset = It.Key();

		if ( AbsOffset >= MaxAbsOffset )
		{
			// Array must have shrunk, we can remove this item
			UE_LOG( LogRep, VeryVerbose, TEXT( "UpdateUnmappedObjects_r: REMOVED unmapped property: AbsOffset >= MaxAbsOffset. Offset: %i" ), AbsOffset );
			It.RemoveCurrent();
			continue;
		}

		FUnmappedGuidMgrElement&		UnmappedProperty = It.Value();
		const FRepLayoutCmd&			Cmd = Cmds[UnmappedProperty.CmdIndex];
		const FRepParentCmd&			Parent = Parents[UnmappedProperty.ParentIndex];

		if ( UnmappedProperty.Array != NULL )
		{
			check( Cmd.Type == REPCMD_DynamicArray );
			
			FScriptArray* StoredArray = (FScriptArray *)( StoredData + AbsOffset );
			FScriptArray* Array = (FScriptArray *)( Data + AbsOffset );
			
			const int32 NewMaxOffset = FMath::Min( StoredArray->Num() * Cmd.ElementSize, Array->Num() * Cmd.ElementSize );

			UpdateUnmappedObjects_r( RepState, UnmappedProperty.Array, OriginalObject, PackageMap, (uint8*)StoredArray->GetData(), (uint8*)Array->GetData(), NewMaxOffset, bOutSomeObjectsWereMapped, bOutHasMoreUnmapped );
			continue;
		}

		bool bMappedSomeGUIDs = false;

		for ( int32 i = UnmappedProperty.UnmappedGUIDs.Num() - 1; i >= 0 ; i-- )
		{			
			const FNetworkGUID& GUID = UnmappedProperty.UnmappedGUIDs[i];

			if ( PackageMap->IsGUIDBroken( GUID, false ) )
			{
				UE_LOG( LogRep, Warning, TEXT( "UpdateUnmappedObjects_r: Broken GUID. NetGuid: %s" ), *GUID.ToString() );
				UnmappedProperty.UnmappedGUIDs.RemoveAt( i );
				continue;
			}

			UObject* Object = PackageMap->GetObjectFromNetGUID( GUID, false );

			if ( Object != NULL )
			{
				UE_LOG( LogRep, VeryVerbose, TEXT( "UpdateUnmappedObjects_r: REMOVED unmapped property: Offset: %i, Guid: %s, PropName: %s, ObjName: %s" ), AbsOffset, *GUID.ToString(), *Cmd.Property->GetName(), *Object->GetName() );
				UnmappedProperty.UnmappedGUIDs.RemoveAt( i );
				bMappedSomeGUIDs = true;
			}
		}

		// If we resolved some guids, re-deserialize the data which will hook up the object pointer with the property
		if ( bMappedSomeGUIDs )
		{
			if ( !bOutSomeObjectsWereMapped )
			{
				// Call PreNetReceive if we are going to change a value (some game code will need to think this is an actual replicated value)
				OriginalObject->PreNetReceive();
				bOutSomeObjectsWereMapped = true;
			}

			// Copy current value over so we can check to see if it changed
			if ( Parent.Property->HasAnyPropertyFlags( CPF_RepNotify ) )
			{
				StoreProperty( Cmd, StoredData + AbsOffset, Data + AbsOffset );
			}

			// Initialize the reader with the stored buffer that we need to read from
			FBitReader Reader( UnmappedProperty.Buffer.GetData(), UnmappedProperty.NumBufferBits );

			// Read the property
			Cmd.Property->NetSerializeItem( Reader, PackageMap, Data + AbsOffset );

			// Check to see if this property changed
			if ( Parent.Property->HasAnyPropertyFlags( CPF_RepNotify ) )
			{
				if ( Parent.RepNotifyCondition == REPNOTIFY_Always || !PropertiesAreIdentical( Cmd, StoredData + AbsOffset, Data + AbsOffset ) )
				{
					// If this properties needs an OnRep, queue that up to be handled later
					RepState->RepNotifies.AddUnique( Parent.Property );
				}
				else
				{
					UE_CLOG( LogSkippedRepNotifies, LogRep, Display, TEXT( "UpdateUnmappedObjects_r: Skipping RepNotify because Property did not change. %s" ), *Cmd.Property->GetName() );
				}
			}
		}

		// If we still have more unmapped guids, we need to keep processing this entry
		if ( UnmappedProperty.UnmappedGUIDs.Num() > 0 )
		{
			bOutHasMoreUnmapped = true;
		}
		else
		{
			It.RemoveCurrent();
		}
	}
}

void FRepLayout::UpdateUnmappedObjects( FRepState *	RepState, UPackageMap * PackageMap, UObject* OriginalObject, bool & bOutSomeObjectsWereMapped, bool & bOutHasMoreUnmapped ) const
{
	bOutSomeObjectsWereMapped	= false;
	bOutHasMoreUnmapped			= false;

	UpdateUnmappedObjects_r( RepState, &RepState->UnmappedGuids, OriginalObject, PackageMap, (uint8*)RepState->StaticBuffer.GetData(), (uint8*)OriginalObject, RepState->StaticBuffer.Num(), bOutSomeObjectsWereMapped, bOutHasMoreUnmapped );
}

void FRepLayout::CallRepNotifies( FRepState * RepState, UObject* Object ) const
{
	if ( RepState->RepNotifies.Num() == 0 )
	{
		return;
	}

	for ( int32 i = 0; i < RepState->RepNotifies.Num(); i++ )
	{
		UProperty * RepProperty = RepState->RepNotifies[i];

		UFunction * RepNotifyFunc = Object->FindFunction( RepProperty->RepNotifyFunc );

		if (RepNotifyFunc == nullptr)
		{
			UE_LOG(LogRep, Warning, TEXT("FRepLayout::CallRepNotifies: Can't find RepNotify function %s for property %s on object %s."),
				*RepProperty->RepNotifyFunc.ToString(), *RepProperty->GetName(), *Object->GetName());
			continue;
		}

		check( RepNotifyFunc->NumParms <= 1 );	// 2 parms not supported yet

		if ( RepNotifyFunc->NumParms == 0 )
		{
			Object->ProcessEvent( RepNotifyFunc, NULL );
		}
		else if (RepNotifyFunc->NumParms == 1 )
		{
			Object->ProcessEvent( RepNotifyFunc, RepProperty->ContainerPtrToValuePtr<uint8>( RepState->StaticBuffer.GetData() ) );
		}

		// Store the property we just received
		//StoreProperty( Cmd, StoredData + Cmd.Offset, Data + SwappedCmd.Offset );
	}

	RepState->RepNotifies.Empty();
}

void FRepLayout::ValidateWithChecksum_DynamicArray_r( const FRepLayoutCmd& Cmd, const int32 CmdIndex, const uint8* RESTRICT Data, FArchive & Ar ) const
{
	FScriptArray * Array = (FScriptArray *)Data;

	uint16 ArrayNum		= Array->Num();
	uint16 ElementSize	= Cmd.ElementSize;

	Ar << ArrayNum;
	Ar << ElementSize;

	if ( ArrayNum != Array->Num() )
	{
		UE_LOG( LogRep, Fatal, TEXT( "ValidateWithChecksum_AnyArray_r: Array sizes different! %s %i / %i" ), *Cmd.Property->GetFullName(), ArrayNum, Array->Num() );
	}

	if ( ElementSize != Cmd.ElementSize )
	{
		UE_LOG( LogRep, Fatal, TEXT( "ValidateWithChecksum_AnyArray_r: Array element sizes different! %s %i / %i" ), *Cmd.Property->GetFullName(), ElementSize, Cmd.ElementSize );
	}

	uint8* LocalData = (uint8*)Array->GetData();

	for ( int32 i = 0; i < ArrayNum; i++ )
	{
		ValidateWithChecksum_r( CmdIndex + 1, Cmd.EndCmd - 1, LocalData + i * ElementSize, Ar );
	}
}

void FRepLayout::ValidateWithChecksum_r( 
	const int32				CmdStart, 
	const int32				CmdEnd, 
	const uint8* RESTRICT	Data, 
	FArchive &				Ar ) const
{
	for ( int32 CmdIndex = CmdStart; CmdIndex < CmdEnd; CmdIndex++ )
	{
		const FRepLayoutCmd& Cmd = Cmds[ CmdIndex ];

		check( Cmd.Type != REPCMD_Return );

		if ( Cmd.Type == REPCMD_DynamicArray )
		{
			ValidateWithChecksum_DynamicArray_r( Cmd, CmdIndex, Data + Cmd.Offset, Ar );
			CmdIndex = Cmd.EndCmd - 1;	// Jump past children of this array (-1 for ++ in for loop)
			continue;
		}

		SerializeReadWritePropertyChecksum( Cmd, CmdIndex - 1, Data + Cmd.Offset, Ar );
	}
}

void FRepLayout::ValidateWithChecksum( const void* RESTRICT Data, FArchive & Ar ) const
{
	ValidateWithChecksum_r( 0, Cmds.Num() - 1, (const uint8*)Data, Ar );
}

uint32 FRepLayout::GenerateChecksum( const FRepState* RepState ) const
{
	FBitWriter Writer( 1024, true );
	ValidateWithChecksum_r( 0, Cmds.Num() - 1, (const uint8*)RepState->StaticBuffer.GetData(), Writer );

	return FCrc::MemCrc32( Writer.GetData(), Writer.GetNumBytes(), 0 );
}

class FPruneChangeListImpl : public FRepLayoutCmdIterator< FPruneChangeListImpl, FCmdIteratorBaseStackState >
{
public:
	FPruneChangeListImpl( const TArray< uint16 >& InChanged, TArray< uint16 >& OutChanged, const TArray< FRepParentCmd >& InParents, const TArray< FRepLayoutCmd >& InCmds ) :
        FRepLayoutCmdIterator( InParents, InCmds ),
		Changed( InChanged ),
		ChangeIndex( 0 ),
		CurrentHandle( 0 ),
		PrunedChanged( OutChanged ),
		bLastChangedMatches( false )
	{}

	INIT_STACK( FCmdIteratorBaseStackState ) { }

	SHOULD_PROCESS_NEXT_CMD() 
	{ 
		CurrentHandle++;

		check( CurrentHandle != 0 );

		bLastChangedMatches = Changed[ChangeIndex] == CurrentHandle;

		return bLastChangedMatches;
	}

	PROCESS_ARRAY_CMD( FCmdIteratorBaseStackState ) 
	{
		check( bLastChangedMatches )

		const bool bDirty1Matches = bLastChangedMatches;

		// This will be a new pruned entry (i.e. clamped to new array boundary)
		PrunedChanged.Add( CurrentHandle );

		const int32 OriginalChangeIndex = PrunedChanged.AddUninitialized();
		check( OriginalChangeIndex == PrunedChanged.Num() - 1 );

		// Advance the change list index since we matched
		if ( bLastChangedMatches )
		{
			ChangeIndex++;
		}

		const int32 JumpToIndex = bLastChangedMatches ? Changed[ChangeIndex++] : -1;

		const int32 OldChangeListIndex = ChangeIndex;

		const int32 OldHandle = CurrentHandle;
		CurrentHandle = 0;

		// Process the array elements
		ProcessDataArrayElements_r( StackState, Cmd );

		// Restore the handle
		CurrentHandle = OldHandle;

		if ( bDirty1Matches )
		{
			check( ChangeIndex - OldChangeListIndex <= JumpToIndex );
			ChangeIndex = OldChangeListIndex + JumpToIndex;
			check( Changed[ChangeIndex] == 0 );
			ChangeIndex++;
		}

		// Patch in the jump offset
		PrunedChanged[OriginalChangeIndex] = PrunedChanged.Num() - ( OriginalChangeIndex + 1 );

		// Add the array terminator
		PrunedChanged.Add( 0 );
	}

	PROCESS_CMD( FCmdIteratorBaseStackState ) 
	{
		check( bLastChangedMatches )

		// This will be a new merged dirty entry
		PrunedChanged.Add( CurrentHandle );

		// Advance matching dirty indices
		ChangeIndex++;
	}

	const TArray< uint16 >& Changed;
	int32					ChangeIndex;
	uint16					CurrentHandle;
	TArray< uint16 >& 		PrunedChanged;
	bool					bLastChangedMatches;
};

void FRepLayout::PruneChangeList( FRepState* RepState, const void* RESTRICT Data, const TArray< uint16 >& Changed, TArray< uint16 >& PrunedChanged ) const
{
	check( Changed.Num() > 0 );

	PrunedChanged.Empty();

	FPruneChangeListImpl PrunePropertiesImpl( Changed, PrunedChanged, Parents, Cmds );

	// Prune it
	PrunePropertiesImpl.ProcessCmds( ( uint8* )Data, ( uint8* )RepState->StaticBuffer.GetData() );

	PrunePropertiesImpl.PrunedChanged.Add( 0 );
}

class FMergeDirtyListImpl : public FRepLayoutCmdIterator< FMergeDirtyListImpl, FCmdIteratorBaseStackState >
{
public:
	FMergeDirtyListImpl( const TArray< uint16 > & InDirty1, const TArray< uint16 > & InDirty2, TArray< uint16 > & OutMergedDirty, const TArray< FRepParentCmd >& InParents, const TArray< FRepLayoutCmd >& InCmds ) : 
        FRepLayoutCmdIterator( InParents, InCmds ),
		DirtyList1( InDirty1 ),
		DirtyList2( InDirty2 ), 
		DirtyListIndex1( 0 ),
		DirtyListIndex2( 0 ),
		CurrentHandle( 0 ),
		bDirtyValid1( true ),
		bDirtyValid2( true ),
		MergedDirtyList( OutMergedDirty ),
		bLastDirty1Matches( false ), 
		bLastDirty2Matches( false )
	{}

	INIT_STACK( FCmdIteratorBaseStackState ) { }

	SHOULD_PROCESS_NEXT_CMD() 
	{ 
		CurrentHandle++;

		check( CurrentHandle != 0 );

		bLastDirty1Matches = bDirtyValid1 && DirtyList1[DirtyListIndex1] == CurrentHandle;
		bLastDirty2Matches = bDirtyValid2 && DirtyList2[DirtyListIndex2] == CurrentHandle;

		return bLastDirty1Matches || bLastDirty2Matches;
	}

	PROCESS_ARRAY_CMD( FCmdIteratorBaseStackState ) 
	{
		// At least one of the list should be valid to be here
		check( bDirtyValid1 || bDirtyValid2 );
		check( bLastDirty1Matches || bLastDirty2Matches )

		const bool bDirty1Matches = bLastDirty1Matches;
		const bool bDirty2Matches = bLastDirty2Matches;

		// This will be a new merged dirty entry
		MergedDirtyList.Add( CurrentHandle );

		const int32 OriginalMergedDirtyListIndex = MergedDirtyList.AddUninitialized();
		check( OriginalMergedDirtyListIndex == MergedDirtyList.Num() - 1 );

		// Advance the matching dirty lists
		if ( bLastDirty1Matches )
		{
			DirtyListIndex1++;
		}

		if ( bLastDirty2Matches )
		{
			DirtyListIndex2++;
		}

		// Remember valid dirty lists
		const bool bOldDirtyValid1 = bDirtyValid1;
		const bool bOldDirtyValid2 = bDirtyValid2;

		// Update which lists are still valid from this point
		bDirtyValid1 = bLastDirty1Matches;
		bDirtyValid2 = bLastDirty2Matches;

		const int32 JumpToIndex1 = bLastDirty1Matches ? DirtyList1[DirtyListIndex1++] : -1;
		const int32 JumpToIndex2 = bLastDirty2Matches ? DirtyList2[DirtyListIndex2++] : -1;

		const int32 OldDirtyListIndex1 = DirtyListIndex1;
		const int32 OldDirtyListIndex2 = DirtyListIndex2;

		const int32 OldHandle = CurrentHandle;
		CurrentHandle = 0;

		// Process the array elements
		ProcessDataArrayElements_r( StackState, Cmd );

		// Restore the handle
		CurrentHandle = OldHandle;

		if ( bDirty1Matches )
		{
			check( DirtyListIndex1 - OldDirtyListIndex1 <= JumpToIndex1 );
			DirtyListIndex1 = OldDirtyListIndex1 + JumpToIndex1;
			check( DirtyList1[DirtyListIndex1] == 0 );
			DirtyListIndex1++;
		}

		if ( bDirty2Matches )
		{
			check( DirtyListIndex2 - OldDirtyListIndex2 <= JumpToIndex2 );
			DirtyListIndex2 = OldDirtyListIndex2 + JumpToIndex2;
			check( DirtyList2[DirtyListIndex2] == 0 );
			DirtyListIndex2++;
		}

		bDirtyValid1 = bOldDirtyValid1;
		bDirtyValid2 = bOldDirtyValid2;

		// Patch in the jump offset
		MergedDirtyList[OriginalMergedDirtyListIndex] = MergedDirtyList.Num() - ( OriginalMergedDirtyListIndex + 1 );

		// Add the array terminator
		MergedDirtyList.Add( 0 );	
	}

	PROCESS_CMD( FCmdIteratorBaseStackState ) 
	{
		check( bLastDirty1Matches || bLastDirty2Matches )

		// This will be a new merged dirty entry
		MergedDirtyList.Add( CurrentHandle );

		// Advance matching dirty indices
		if ( bLastDirty1Matches )
		{
			DirtyListIndex1++;
		}

		if ( bLastDirty2Matches )
		{
			DirtyListIndex2++;
		}
	}

	const TArray< uint16 >& DirtyList1;
	const TArray< uint16 >& DirtyList2;

	int32					DirtyListIndex1;
	int32					DirtyListIndex2;
	uint16					CurrentHandle;

	bool					bDirtyValid1;
	bool					bDirtyValid2;

	TArray< uint16 >& 		MergedDirtyList;

	bool					bLastDirty1Matches;
	bool					bLastDirty2Matches;
};

void FRepLayout::MergeDirtyList( FRepState * RepState, const void* RESTRICT Data, const TArray< uint16 > & Dirty1, const TArray< uint16 > & Dirty2, TArray< uint16 > & MergedDirty ) const
{
	check( Dirty1.Num() > 0 || Dirty2.Num() > 0 );

	MergedDirty.Empty();

	FMergeDirtyListImpl MergePropertiesImpl( Dirty1, Dirty2, MergedDirty, Parents, Cmds );

	// Even though one of these can be empty, we need to send the single one through, so we can prune it to the current shape of the tree
	MergePropertiesImpl.bDirtyValid1 = Dirty1.Num() > 0;
	MergePropertiesImpl.bDirtyValid2 = Dirty2.Num() > 0;

	// Merge lists
	MergePropertiesImpl.ProcessCmds( (uint8*)Data, (uint8*)RepState->StaticBuffer.GetData() );

	MergePropertiesImpl.MergedDirtyList.Add( 0 );
}

void FRepLayout::SanityCheckChangeList_DynamicArray_r( 
	const int32				CmdIndex, 
	const uint8* RESTRICT	Data, 
	TArray< uint16 > &		Changed,
	int32 &					ChangedIndex ) const
{
	const FRepLayoutCmd& Cmd = Cmds[ CmdIndex ];

	FScriptArray * Array = (FScriptArray *)Data;

	// Read the jump offset
	// We won't need to actually jump over anything because we expect the change list to be pruned once we get here
	// But we can use it to verify we read the correct amount.
	const int32 ArrayChangedCount = Changed[ChangedIndex++];

	const int32 OldChangedIndex = ChangedIndex;

	Data = (uint8*)Array->GetData();

	uint16 LocalHandle = 0;

	for ( int32 i = 0; i < Array->Num(); i++ )
	{
		LocalHandle = SanityCheckChangeList_r( CmdIndex + 1, Cmd.EndCmd - 1, Data + i * Cmd.ElementSize, Changed, ChangedIndex, LocalHandle );
	}

	check( ChangedIndex - OldChangedIndex == ArrayChangedCount );	// Make sure we read correct amount
	check( Changed[ChangedIndex] == 0 );							// Make sure we are at the end

	ChangedIndex++;
}

uint16 FRepLayout::SanityCheckChangeList_r( 
	const int32				CmdStart, 
	const int32				CmdEnd, 
	const uint8* RESTRICT	Data, 
	TArray< uint16 > &		Changed,
	int32 &					ChangedIndex,
	uint16					Handle 
	) const
{
	for ( int32 CmdIndex = CmdStart; CmdIndex < CmdEnd; CmdIndex++ )
	{
		const FRepLayoutCmd& Cmd = Cmds[ CmdIndex ];

		check( Cmd.Type != REPCMD_Return );

		Handle++;

		if ( Cmd.Type == REPCMD_DynamicArray )
		{
			if ( Handle == Changed[ChangedIndex] )
			{
				const int32 LastChangedArrayHandle = Changed[ChangedIndex];
				ChangedIndex++;
				SanityCheckChangeList_DynamicArray_r( CmdIndex, Data + Cmd.Offset, Changed, ChangedIndex );
				check( Changed[ChangedIndex] == 0 || Changed[ChangedIndex] > LastChangedArrayHandle );
			}
			CmdIndex = Cmd.EndCmd - 1;	// Jump past children of this array (the -1 because of the ++ in the for loop)
			continue;
		}

		if ( Handle == Changed[ChangedIndex] )
		{
			const int32 LastChangedArrayHandle = Changed[ChangedIndex];
			ChangedIndex++;
			check( Changed[ChangedIndex] == 0 || Changed[ChangedIndex] > LastChangedArrayHandle );
		}
	}

	return Handle;
}

void FRepLayout::SanityCheckChangeList( const uint8* RESTRICT Data, TArray< uint16 > & Changed ) const
{
	int32 ChangedIndex = 0;
	SanityCheckChangeList_r( 0, Cmds.Num() - 1, Data, Changed, ChangedIndex, 0 );
	check( Changed[ChangedIndex] == 0 );
}

class FDiffPropertiesImpl : public FRepLayoutCmdIterator< FDiffPropertiesImpl, FCmdIteratorBaseStackState >
{
public:
	FDiffPropertiesImpl( const bool bInSync, TArray< UProperty * >*	InRepNotifies, const TArray< FRepParentCmd >& InParents, const TArray< FRepLayoutCmd >& InCmds ) : 
		FRepLayoutCmdIterator( InParents, InCmds ),
		bSync( bInSync ),
		RepNotifies( InRepNotifies ),
		bDifferent( false )
	{}

	INIT_STACK( FCmdIteratorBaseStackState ) { }

	SHOULD_PROCESS_NEXT_CMD() 
	{ 
		return true;
	}

	PROCESS_ARRAY_CMD( FCmdIteratorBaseStackState ) 
	{
		if ( StackState.DataArray->Num() != StackState.ShadowArray->Num() )
		{
			bDifferent = true;

			if ( !bSync )
			{			
				UE_LOG( LogRep, Warning, TEXT( "FDiffPropertiesImpl: Array sizes different: %s %i / %i" ), *Cmd.Property->GetFullName(), StackState.DataArray->Num(), StackState.ShadowArray->Num() );
				return;
			}

			if ( !( Parents[Cmd.ParentIndex].Flags & PARENT_IsLifetime ) )
			{
				// Currently, only lifetime properties init from their defaults
				return;
			}

			// Make the shadow state match the actual state
			FScriptArrayHelper ShadowArrayHelper( (UArrayProperty *)Cmd.Property, ShadowData );
			ShadowArrayHelper.Resize( StackState.DataArray->Num() );
		}

		StackState.BaseData			= (uint8*)StackState.DataArray->GetData();
		StackState.ShadowBaseData	= (uint8*)StackState.ShadowArray->GetData();

		// Loop over array
		ProcessDataArrayElements_r( StackState, Cmd );
	}

	PROCESS_CMD( FCmdIteratorBaseStackState ) 
	{
		const FRepParentCmd& Parent = Parents[Cmd.ParentIndex];

		// Make the shadow state match the actual state at the time of send
		if ( Parent.RepNotifyCondition == REPNOTIFY_Always || !PropertiesAreIdentical( Cmd, (const void*)( Data + Cmd.Offset ), (const void*)( ShadowData + Cmd.Offset ) ) )
		{
			bDifferent = true;

			if ( !bSync )
			{			
				UE_LOG( LogRep, Warning, TEXT( "FDiffPropertiesImpl: Property different: %s" ), *Cmd.Property->GetFullName() );
				return;
			}

			if ( !( Parent.Flags & PARENT_IsLifetime ) )
			{
				// Currently, only lifetime properties init from their defaults
				return;
			}

			StoreProperty( Cmd, (void*)( Data + Cmd.Offset ), (const void*)( ShadowData + Cmd.Offset ) );

			if ( RepNotifies && Parent.Property->HasAnyPropertyFlags( CPF_RepNotify ) )
			{
				RepNotifies->AddUnique( Parent.Property );
			}
		}
		else
		{
			UE_CLOG( LogSkippedRepNotifies > 0, LogRep, Display, TEXT( "FDiffPropertiesImpl: Skipping RepNotify because values are the same: %s" ), *Cmd.Property->GetFullName() );
		}
	}

	bool					bSync;
	TArray< UProperty * >*	RepNotifies;
	bool					bDifferent;
};

bool FRepLayout::DiffProperties( TArray<UProperty*>* RepNotifies, void* RESTRICT Destination, const void* RESTRICT Source, const bool bSync ) const
{	
	FDiffPropertiesImpl DiffPropertiesImpl( bSync, RepNotifies, Parents, Cmds );

	DiffPropertiesImpl.ProcessCmds( (uint8*)Destination, (uint8*)Source );

	return DiffPropertiesImpl.bDifferent;
}

uint32 FRepLayout::AddPropertyCmd( UProperty * Property, int32 Offset, int32 RelativeHandle, int32 ParentIndex, uint32 ParentChecksum, int32 StaticArrayIndex )
{
	const int32 Index = Cmds.AddZeroed();

	FRepLayoutCmd & Cmd = Cmds[Index];

	Cmd.Property			= Property;
	Cmd.Type				= REPCMD_Property;		// Initially set to generic type
	Cmd.Offset				= Offset;
	Cmd.ElementSize			= Property->ElementSize;
	Cmd.RelativeHandle		= RelativeHandle;
	Cmd.ParentIndex			= ParentIndex;

	Cmd.CompatibleChecksum	= FCrc::StrCrc32( *Property->GetName().ToLower(), ParentChecksum );								// Evolve checksum on name
	Cmd.CompatibleChecksum	= FCrc::StrCrc32( *Property->GetCPPType( nullptr, 0 ).ToLower(), Cmd.CompatibleChecksum );		// Evolve by property type
	Cmd.CompatibleChecksum	= FCrc::StrCrc32( *FString::Printf( TEXT( "%i" ), StaticArrayIndex ), Cmd.CompatibleChecksum );	// Evolve by StaticArrayIndex (to make all unrolled static array elements unique)

	// Try to special case to custom types we know about
	if ( Property->IsA( UStructProperty::StaticClass() ) )
	{
		UStructProperty * StructProp = Cast< UStructProperty >( Property );
		UScriptStruct * Struct = StructProp->Struct;
		if ( Struct->GetFName() == NAME_Vector )
		{
			Cmd.Type = REPCMD_PropertyVector;
		}
		else if ( Struct->GetFName() == NAME_Rotator )
		{
			Cmd.Type = REPCMD_PropertyRotator;
		}
		else if ( Struct->GetFName() == NAME_Plane )
		{
			Cmd.Type = REPCMD_PropertyPlane;
		}
		else if ( Struct->GetName() == TEXT( "Vector_NetQuantize100" ) )
		{
			Cmd.Type = REPCMD_PropertyVector100;
		}
		else if ( Struct->GetName() == TEXT( "Vector_NetQuantize10" ) )
		{
			Cmd.Type = REPCMD_PropertyVector10;
		}
		else if ( Struct->GetName() == TEXT( "Vector_NetQuantizeNormal" ) )
		{
			Cmd.Type = REPCMD_PropertyVectorNormal;
		}
		else if ( Struct->GetName() == TEXT( "Vector_NetQuantize" ) )
		{
			Cmd.Type = REPCMD_PropertyVectorQ;
		}
		else if ( Struct->GetName() == TEXT( "UniqueNetIdRepl" ) )
		{
			Cmd.Type = REPCMD_PropertyNetId;
		}
		else if ( Struct->GetName() == TEXT( "RepMovement" ) )
		{
			Cmd.Type = REPCMD_RepMovement;
		}
		else
		{
			UE_LOG( LogRep, VeryVerbose, TEXT( "AddPropertyCmd: Falling back to default type for property [%s]" ), *Cmd.Property->GetFullName() );
		}
	}
	else if ( Property->IsA( UBoolProperty::StaticClass() ) )
	{
		Cmd.Type = REPCMD_PropertyBool;
	}
	else if ( Property->IsA( UFloatProperty::StaticClass() ) )
	{
		Cmd.Type = REPCMD_PropertyFloat;
	}
	else if ( Property->IsA( UIntProperty::StaticClass() ) )
	{
		Cmd.Type = REPCMD_PropertyInt;
	}
	else if ( Property->IsA( UByteProperty::StaticClass() ) )
	{
		Cmd.Type = REPCMD_PropertyByte;
	}
	else if ( Property->IsA( UObjectPropertyBase::StaticClass() ) )
	{
		Cmd.Type = REPCMD_PropertyObject;
	}
	else if ( Property->IsA( UNameProperty::StaticClass() ) )
	{
		Cmd.Type = REPCMD_PropertyName;
	}
	else if ( Property->IsA( UUInt32Property::StaticClass() ) )
	{
		Cmd.Type = REPCMD_PropertyUInt32;
	}
	else if ( Property->IsA( UUInt64Property::StaticClass() ) )
	{
		Cmd.Type = REPCMD_PropertyUInt64;
	}
	else if ( Property->IsA( UStrProperty::StaticClass() ) )
	{
		Cmd.Type = REPCMD_PropertyString;
	}
	else
	{
		UE_LOG( LogRep, VeryVerbose, TEXT( "AddPropertyCmd: Falling back to default type for property [%s]" ), *Cmd.Property->GetFullName() );
	}

	return Cmd.CompatibleChecksum;
}

uint32 FRepLayout::AddArrayCmd( UArrayProperty * Property, int32 Offset, int32 RelativeHandle, int32 ParentIndex, uint32 ParentChecksum, int32 StaticArrayIndex )
{
	const int32 Index = Cmds.AddZeroed();

	FRepLayoutCmd & Cmd = Cmds[Index];

	Cmd.Type				= REPCMD_DynamicArray;
	Cmd.Property			= Property;
	Cmd.Offset				= Offset;
	Cmd.ElementSize			= Property->Inner->ElementSize;
	Cmd.RelativeHandle		= RelativeHandle;
	Cmd.ParentIndex			= ParentIndex;
	
	Cmd.CompatibleChecksum	= FCrc::StrCrc32( *Property->GetName().ToLower(), ParentChecksum );								// Evolve checksum on name
	Cmd.CompatibleChecksum	= FCrc::StrCrc32( *Property->GetCPPType( nullptr, 0 ).ToLower(), Cmd.CompatibleChecksum );		// Evolve by property type
	Cmd.CompatibleChecksum	= FCrc::StrCrc32( *FString::Printf( TEXT( "%i" ), StaticArrayIndex ), Cmd.CompatibleChecksum );	// Evolve by StaticArrayIndex (to make all unrolled static array elements unique)

	return Cmd.CompatibleChecksum;
}

void FRepLayout::AddReturnCmd()
{
	const int32 Index = Cmds.AddZeroed();
	
	FRepLayoutCmd & Cmd = Cmds[Index];

	Cmd.Type = REPCMD_Return;
}

int32 FRepLayout::InitFromProperty_r( UProperty * Property, int32 Offset, int32 RelativeHandle, int32 ParentIndex, uint32 ParentChecksum, int32 StaticArrayIndex )
{
	UArrayProperty * ArrayProp = Cast< UArrayProperty >( Property );

	if ( ArrayProp != NULL )
	{
		const int32 CmdStart = Cmds.Num();

		RelativeHandle++;

		const uint32 ArrayChecksum = AddArrayCmd( ArrayProp, Offset + ArrayProp->GetOffset_ForGC(), RelativeHandle, ParentIndex, ParentChecksum, StaticArrayIndex );

		InitFromProperty_r( ArrayProp->Inner, 0, 0, ParentIndex, ArrayChecksum, 0 );
		
		AddReturnCmd();

		Cmds[CmdStart].EndCmd = Cmds.Num();		// Patch in the offset to jump over our array inner elements

		return RelativeHandle;
	}

	UStructProperty * StructProp = Cast< UStructProperty >( Property );

	if ( StructProp != NULL )
	{
		UScriptStruct * Struct = StructProp->Struct;

		if ( Struct->StructFlags & STRUCT_NetDeltaSerializeNative )
		{
			// Custom delta serializers handles outside of FRepLayout
			return RelativeHandle;
		}

		if ( Struct->StructFlags & STRUCT_NetSerializeNative )
		{
			RelativeHandle++;
			AddPropertyCmd( Property, Offset + Property->GetOffset_ForGC(), RelativeHandle, ParentIndex, ParentChecksum, StaticArrayIndex );
			return RelativeHandle;
		}

		TArray< UProperty * > NetProperties;		// Track properties so me can ensure they are sorted by offsets at the end

		for ( TFieldIterator<UProperty> It( Struct ); It; ++It )
		{
			if ( ( It->PropertyFlags & CPF_RepSkip ) )
			{
				continue;
			}

			NetProperties.Add( *It );
		}

		// Sort NetProperties by memory offset
		struct FCompareUFieldOffsets
		{
			FORCEINLINE bool operator()( UProperty & A, UProperty & B ) const
			{
				// Ensure stable sort
				if ( A.GetOffset_ForGC() == B.GetOffset_ForGC() )
				{
					return A.GetName() < B.GetName();
				}

				return A.GetOffset_ForGC() < B.GetOffset_ForGC();
			}
		};

		Sort( NetProperties.GetData(), NetProperties.Num(), FCompareUFieldOffsets() );

		// Evolve checksum on struct name
		uint32 StructChecksum = FCrc::StrCrc32( *Property->GetName().ToLower(), ParentChecksum );

		// Evolve by property type
		StructChecksum = FCrc::StrCrc32( *Property->GetCPPType( nullptr, 0 ).ToLower(), StructChecksum );

		// Evolve by StaticArrayIndex (to make all unrolled static array elements unique)
		StructChecksum = FCrc::StrCrc32( *FString::Printf( TEXT( "%i" ), StaticArrayIndex ), StructChecksum );

		for ( int32 i = 0; i < NetProperties.Num(); i++ )
		{
			for ( int32 j = 0; j < NetProperties[i]->ArrayDim; j++ )
			{
				RelativeHandle = InitFromProperty_r( NetProperties[i], Offset + StructProp->GetOffset_ForGC() + j * NetProperties[i]->ElementSize, RelativeHandle, ParentIndex, StructChecksum, j );
			}
		}
		return RelativeHandle;
	}

	// Add actual property
	RelativeHandle++;

	AddPropertyCmd( Property, Offset + Property->GetOffset_ForGC(), RelativeHandle, ParentIndex, ParentChecksum, StaticArrayIndex );

	return RelativeHandle;
}

uint16 FRepLayout::AddParentProperty( UProperty * Property, int32 ArrayIndex )
{
	return Parents.Add( FRepParentCmd( Property, ArrayIndex ) );
}

void FRepLayout::InitFromObjectClass( UClass * InObjectClass )
{
	RoleIndex				= -1;
	RemoteRoleIndex			= -1;
	FirstNonCustomParent	= -1;

	int32 RelativeHandle	= 0;
	int32 LastOffset		= -1;

	Parents.Empty();

	for ( int32 i = 0; i < InObjectClass->ClassReps.Num(); i++ )
	{
		UProperty * Property	= InObjectClass->ClassReps[i].Property;
		const int32 ArrayIdx	= InObjectClass->ClassReps[i].Index;

		check( Property->PropertyFlags & CPF_Net );

		const int32 ParentHandle = AddParentProperty( Property, ArrayIdx );

		check( ParentHandle == i );
		check( Parents[i].Property->RepIndex + Parents[i].ArrayIndex == i );

		Parents[ParentHandle].CmdStart = Cmds.Num();
		RelativeHandle = InitFromProperty_r( Property, Property->ElementSize * ArrayIdx, RelativeHandle, ParentHandle, 0, ArrayIdx );
		Parents[ParentHandle].CmdEnd = Cmds.Num();
		Parents[ParentHandle].Flags |= PARENT_IsConditional;

		if ( Parents[i].CmdEnd > Parents[i].CmdStart )
		{
			check( Cmds[Parents[i].CmdStart].Offset >= LastOffset );		// >= since bool's can be combined
			LastOffset = Cmds[Parents[i].CmdStart].Offset;
		}

		// Setup flags
		if ( IsCustomDeltaProperty( Property ) )
		{
			Parents[ParentHandle].Flags |= PARENT_IsCustomDelta;
		}

		if ( Property->GetPropertyFlags() & CPF_Config )
		{
			Parents[ParentHandle].Flags |= PARENT_IsConfig;
		}

		// Hijack the first non custom property for identifying this as a rep layout block
		if ( FirstNonCustomParent == -1 && Property->ArrayDim == 1 && ( Parents[ParentHandle].Flags & PARENT_IsCustomDelta ) == 0 )
		{
			FirstNonCustomParent = ParentHandle;
		}

		// Find Role/RemoteRole property indexes so we can swap them on the client
		if ( Property->GetFName() == NAME_Role )
		{
			check( RoleIndex == -1 );
			check( Parents[ParentHandle].CmdEnd == Parents[ParentHandle].CmdStart + 1 );
			RoleIndex = ParentHandle;
		}

		if ( Property->GetFName() == NAME_RemoteRole )
		{
			check( RemoteRoleIndex == -1 );
			check( Parents[ParentHandle].CmdEnd == Parents[ParentHandle].CmdStart + 1 );
			RemoteRoleIndex = ParentHandle;
		}
	}

	// Make sure it either found both, or didn't find either
	check( ( RoleIndex == -1 ) == ( RemoteRoleIndex == -1 ) );

	// This is so the receiving side can swap these as it receives them
	if ( RoleIndex != -1 )
	{
		Parents[RoleIndex].RoleSwapIndex = RemoteRoleIndex;
		Parents[RemoteRoleIndex].RoleSwapIndex = RoleIndex;
	}
	
	AddReturnCmd();

	// Initialize lifetime props
	TArray< FLifetimeProperty >	LifetimeProps;			// Properties that replicate for the lifetime of the channel

	UObject* Object = InObjectClass->GetDefaultObject();

	Object->GetLifetimeReplicatedProps( LifetimeProps );

	if ( LifetimeProps.Num() > 0 )
	{
		UnconditionalLifetime.Empty();
		ConditionalLifetime.Empty();

		// Fix Lifetime props to have the proper index to the parent
		for ( int32 i = 0; i < LifetimeProps.Num(); i++ )
		{
			// Store the condition on the parent in case we need it
			Parents[LifetimeProps[i].RepIndex].Condition = LifetimeProps[i].Condition;
			Parents[LifetimeProps[i].RepIndex].RepNotifyCondition = LifetimeProps[i].RepNotifyCondition;

			if ( Parents[LifetimeProps[i].RepIndex].Flags & PARENT_IsCustomDelta )
			{
				continue;		// We don't handle custom properties in the FRepLayout class
			}

			Parents[LifetimeProps[i].RepIndex].Flags |= PARENT_IsLifetime;

			if ( LifetimeProps[i].RepIndex == RemoteRoleIndex )
			{
				// We handle remote role specially, since it can change between connections when downgraded
				// So we force it on the conditional list
				check( LifetimeProps[i].Condition == COND_None );
				LifetimeProps[i].Condition = COND_Custom;
				ConditionalLifetime.AddUnique( LifetimeProps[i] );
				continue;		
			}

			if ( LifetimeProps[i].Condition == COND_None )
			{
				// These properties are simple, and can benefit from property compare sharing
				UnconditionalLifetime.AddUnique( LifetimeProps[i].RepIndex );
				Parents[LifetimeProps[i].RepIndex].Flags &= ~PARENT_IsConditional;
				continue;
			}
			
			// The rest are conditional
			// These properties are eventually conditionally copied to the RepState
			ConditionalLifetime.AddUnique( LifetimeProps[i] );
		}
	}

	Owner = InObjectClass;
}

void FRepLayout::InitFromFunction( UFunction * InFunction )
{
	int32 RelativeHandle = 0;

	for ( TFieldIterator<UProperty> It( InFunction ); It && ( It->PropertyFlags & ( CPF_Parm | CPF_ReturnParm ) ) == CPF_Parm; ++It )
	{
		for ( int32 ArrayIdx = 0; ArrayIdx < It->ArrayDim; ++ArrayIdx )
		{
			const int32 ParentHandle = AddParentProperty( *It, ArrayIdx );
			Parents[ParentHandle].CmdStart = Cmds.Num();
			RelativeHandle = InitFromProperty_r( *It, It->ElementSize * ArrayIdx, RelativeHandle, ParentHandle, 0, ArrayIdx );
			Parents[ParentHandle].CmdEnd = Cmds.Num();
		}
	}

	AddReturnCmd();

	Owner = InFunction;
}

void FRepLayout::InitFromStruct( UStruct * InStruct )
{
	int32 RelativeHandle = 0;

	for ( TFieldIterator<UProperty> It( InStruct ); It; ++It )
	{
		if ( It->PropertyFlags & CPF_RepSkip )
		{
			continue;
		}
			
		for ( int32 ArrayIdx = 0; ArrayIdx < It->ArrayDim; ++ArrayIdx )
		{
			const int32 ParentHandle = AddParentProperty( *It, ArrayIdx );
			Parents[ParentHandle].CmdStart = Cmds.Num();
			RelativeHandle = InitFromProperty_r( *It, It->ElementSize * ArrayIdx, RelativeHandle, ParentHandle, 0, ArrayIdx );
			Parents[ParentHandle].CmdEnd = Cmds.Num();
		}
	}

	AddReturnCmd();

	Owner = InStruct;
}

void FRepLayout::SerializeProperties_DynamicArray_r( 
	FArchive &			Ar, 
	UPackageMap	*		Map,
	const int32			CmdIndex,
	uint8 *				Data,
	bool &				bHasUnmapped ) const
{
	const FRepLayoutCmd& Cmd = Cmds[ CmdIndex ];

	FScriptArray * Array = (FScriptArray *)Data;

	// Read array num
	uint16 ArrayNum = Array->Num();
	Ar << ArrayNum;

	const int MAX_ARRAY_SIZE = 2048;

	if ( ArrayNum > MAX_ARRAY_SIZE )
	{
		UE_LOG( LogRepTraffic, Error, TEXT( "SerializeProperties_DynamicArray_r: ArrayNum > MAX_ARRAY_SIZE (%s)" ), *Cmd.Property->GetName() );
		Ar.SetError();
		return;
	}

	const int MAX_ARRAY_MEMORY = 1024 * 64;

	if ( (int32)ArrayNum * Cmd.ElementSize > MAX_ARRAY_MEMORY )
	{
		UE_LOG( LogRepTraffic, Error, TEXT( "SerializeProperties_DynamicArray_r: ArrayNum * Cmd.ElementSize > MAX_ARRAY_MEMORY (%s)" ), *Cmd.Property->GetName() );
		Ar.SetError();
		return;
	}

	if ( Ar.IsLoading() && ArrayNum != Array->Num() )
	{
		// If we are reading, size the array to the incoming value
		FScriptArrayHelper ArrayHelper( (UArrayProperty *)Cmd.Property, Data );
		ArrayHelper.Resize( ArrayNum );
	}

	Data = (uint8*)Array->GetData();

	for ( int32 i = 0; i < Array->Num() && !Ar.IsError(); i++ )
	{
		SerializeProperties_r( Ar, Map, CmdIndex + 1, Cmd.EndCmd - 1, Data + i * Cmd.ElementSize, bHasUnmapped );
	}
}

void FRepLayout::SerializeProperties_r( 
	FArchive &			Ar, 
	UPackageMap	*		Map,
	const int32			CmdStart, 
	const int32			CmdEnd,
	void *				Data,
	bool &				bHasUnmapped ) const
{
	for ( int32 CmdIndex = CmdStart; CmdIndex < CmdEnd && !Ar.IsError(); CmdIndex++ )
	{
		const FRepLayoutCmd& Cmd = Cmds[CmdIndex];

		check( Cmd.Type != REPCMD_Return );

		if ( Cmd.Type == REPCMD_DynamicArray )
		{
			SerializeProperties_DynamicArray_r( Ar, Map, CmdIndex, (uint8*)Data + Cmd.Offset, bHasUnmapped );
			CmdIndex = Cmd.EndCmd - 1;		// The -1 to handle the ++ in the for loop
			continue;
		}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		if (CVarDoReplicationContextString->GetInt() > 0)
		{
			Map->SetDebugContextString( FString::Printf(TEXT("%s - %s"), *Owner->GetPathName(), *Cmd.Property->GetPathName() ) );
		}
#endif

		if ( !Cmd.Property->NetSerializeItem( Ar, Map, (void*)( (uint8*)Data + Cmd.Offset ) ) )
		{
			bHasUnmapped = true;
		}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		if (CVarDoReplicationContextString->GetInt() > 0)
		{
			Map->ClearDebugContextString();
		}
#endif
	}
}

void FRepLayout::BuildChangeList_r( const int32 CmdStart, const int32 CmdEnd, uint8* Data, const int32 HandleOffset, TArray< uint16 >& Changed ) const
{
	for ( int32 CmdIndex = CmdStart; CmdIndex < CmdEnd; CmdIndex++ )
	{
		const FRepLayoutCmd& Cmd = Cmds[CmdIndex];

		check( Cmd.Type != REPCMD_Return );

		if ( Cmd.Type == REPCMD_DynamicArray )
		{			
			FScriptArray* Array = ( FScriptArray * )( Data + Cmd.Offset );

			TArray< uint16 > ChangedLocal;

			const int32 ArrayCmdStart			= CmdIndex + 1;
			const int32 ArrayCmdEnd				= Cmd.EndCmd - 1;
			const int32 NumHandlesPerElement	= ArrayCmdEnd - ArrayCmdStart;

			for ( int32 i = 0; i < Array->Num(); i++ )
			{
				BuildChangeList_r( ArrayCmdStart, ArrayCmdEnd, ((uint8*)Array->GetData()) + Cmd.ElementSize * i, i * NumHandlesPerElement, Changed );
			}

			if ( ChangedLocal.Num() )
			{
				Changed.Add( Cmd.RelativeHandle + HandleOffset );	// Identify the array cmd handle
				Changed.Add( ChangedLocal.Num() );					// This is so we can jump over the array if we need to
				Changed.Append( ChangedLocal );						// Append the change list under the array
				Changed.Add( 0 );									// Null terminator
			}

			CmdIndex = Cmd.EndCmd - 1;		// The -1 to handle the ++ in the for loop
			continue;
		}

		Changed.Add( Cmd.RelativeHandle + HandleOffset );
	}
}

void FRepLayout::SendPropertiesForRPC( UObject* Object, UFunction * Function, UActorChannel * Channel, FNetBitWriter & Writer, void* Data ) const
{
	check( Function == Owner );

	if ( Channel->Connection->InternalAck )
	{
		TArray< uint16 > Changed;

		for ( int32 i = 0; i < Parents.Num(); i++ )
		{
			if ( !Parents[i].Property->Identical_InContainer( Data, NULL, Parents[i].ArrayIndex ) )
			{
				BuildChangeList_r( Parents[i].CmdStart, Parents[i].CmdEnd, ( uint8* )Data, 0, Changed );
			}
		}

		Changed.Add( 0 ); // Null terminator

		SendProperties_BackwardsCompatible( nullptr, ( uint8* )Data, Channel->Connection, Writer, Changed );

		return;
	}

	for ( int32 i = 0; i < Parents.Num(); i++ )
	{
		bool Send = true;

		if ( !Cast<UBoolProperty>( Parents[i].Property ) )
		{
			// check for a complete match, including arrays
			// (we're comparing against zero data here, since 
			// that's the default.)
			Send = !Parents[i].Property->Identical_InContainer( Data, NULL, Parents[i].ArrayIndex );

			Writer.WriteBit( Send ? 1 : 0 );
		}

		if ( Send )
		{
			bool bHasUnmapped = false;
			SerializeProperties_r( Writer, Writer.PackageMap, Parents[i].CmdStart, Parents[i].CmdEnd, Data, bHasUnmapped );
		}
	}
}

void FRepLayout::ReceivePropertiesForRPC( UObject* Object, UFunction * Function, UActorChannel * Channel, FNetBitReader & Reader, void* Data ) const
{
	check( Function == Owner );

	for ( int32 i = 0; i < Parents.Num(); i++ )
	{
		if ( Parents[i].ArrayIndex == 0 && ( Parents[i].Property->PropertyFlags & CPF_ZeroConstructor ) == 0 )
		{
			// If this property needs to be constructed, make sure we do that
			Parents[i].Property->InitializeValue((uint8*)Data + Parents[i].Property->GetOffset_ForUFunction());
		}
	}

	if ( Channel->Connection->InternalAck )
	{
		bool bHasUnmapped = false;

		// Let package map know we want to track and know about any guids that are unmapped during the serialize call
		// We have to do this manually since we aren't passing in any unmapped info
		Reader.PackageMap->ResetTrackedUnmappedGuids( true );

		ReceiveProperties_BackwardsCompatible( Channel->Connection, nullptr, Data, Reader, bHasUnmapped, false );

		if ( Reader.PackageMap->GetTrackedUnmappedGuids().Num() > 0 )
		{
			bHasUnmapped = true;
		}

		Reader.PackageMap->ResetTrackedUnmappedGuids( false );

		if ( bHasUnmapped )
		{
			UE_LOG( LogRepTraffic, Log, TEXT( "Unable to resolve RPC parameter to do being unmapped. Object[%d] %s. Function %s." ),
					Channel->ChIndex, *Object->GetName(), *Function->GetName() );
		}
	}
	else
	{
		for ( int32 i = 0; i < Parents.Num(); i++ )
		{
			if ( Cast<UBoolProperty>( Parents[i].Property ) || Reader.ReadBit() )
			{
				bool bHasUnmapped = false;

				SerializeProperties_r( Reader, Reader.PackageMap, Parents[i].CmdStart, Parents[i].CmdEnd, Data, bHasUnmapped );

				if ( Reader.IsError() )
				{
					return;
				}

				if ( bHasUnmapped )
				{
					UE_LOG( LogRepTraffic, Log, TEXT( "Unable to resolve RPC parameter. Object[%d] %s. Function %s. Parameter %s." ),
							Channel->ChIndex, *Object->GetName(), *Function->GetName(), *Parents[i].Property->GetName() );
				}
			}
		}
	}
}

void FRepLayout::SerializePropertiesForStruct( UStruct * Struct, FArchive & Ar, UPackageMap	* Map, void* Data, bool & bHasUnmapped ) const
{
	check( Struct == Owner );

	for ( int32 i = 0; i < Parents.Num(); i++ )
	{
		SerializeProperties_r( Ar, Map, Parents[i].CmdStart, Parents[i].CmdEnd, Data, bHasUnmapped );

		if ( Ar.IsError() )
		{
			return;
		}
	}
}

void FRepLayout::RebuildConditionalProperties( FRepState * RESTRICT	RepState, const FRepChangedPropertyTracker& ChangedTracker, const FReplicationFlags& RepFlags ) const
{
	SCOPE_CYCLE_COUNTER( STAT_NetRebuildConditionalTime );

	//UE_LOG( LogRep, Warning, TEXT( "Rebuilding custom properties [%s]" ), *Owner->GetName() );
	
	// Setup condition map
	bool ConditionMap[COND_Max];

	const bool bIsInitial	= RepFlags.bNetInitial		? true : false;
	const bool bIsOwner		= RepFlags.bNetOwner		? true : false;
	const bool bIsSimulated	= RepFlags.bNetSimulated	? true : false;
	const bool bIsPhysics	= RepFlags.bRepPhysics		? true : false;
	const bool bIsReplay	= RepFlags.bReplay			? true : false;

	ConditionMap[COND_None]					= true;
	ConditionMap[COND_InitialOnly]			= bIsInitial;

	ConditionMap[COND_OwnerOnly]			= bIsOwner;
	ConditionMap[COND_SkipOwner]			= !bIsOwner;

	ConditionMap[COND_SimulatedOnly]			= bIsSimulated;
	ConditionMap[COND_SimulatedOnlyNoReplay]	= bIsSimulated && !bIsReplay;
	ConditionMap[COND_AutonomousOnly]			= !bIsSimulated;

	ConditionMap[COND_SimulatedOrPhysics]			= bIsSimulated || bIsPhysics;
	ConditionMap[COND_SimulatedOrPhysicsNoReplay]	= ( bIsSimulated || bIsPhysics ) && !bIsReplay;

	ConditionMap[COND_InitialOrOwner]		= bIsInitial || bIsOwner;
	ConditionMap[COND_ReplayOrOwner]		= bIsReplay || bIsOwner;
	ConditionMap[COND_ReplayOnly]			= bIsReplay;

	ConditionMap[COND_Custom]				= true;

	RepState->ConditionalLifetime.Empty();

	for ( int32 i = 0; i < ConditionalLifetime.Num(); i++ )
	{
		if ( !ConditionMap[ConditionalLifetime[i].Condition] )
		{
			continue;
		}
		
		if ( !ChangedTracker.Parents[ConditionalLifetime[i].RepIndex].Active )
		{
			continue;
		}

		RepState->ConditionalLifetime.Add( ConditionalLifetime[i].RepIndex );
	}

	RepState->RepFlags = RepFlags;
}

void FRepLayout::InitChangedTracker( FRepChangedPropertyTracker * ChangedTracker ) const
{
	ChangedTracker->Parents.SetNum( Parents.Num() );

	for ( int32 i = 0; i < Parents.Num(); i++ )
	{
		ChangedTracker->Parents[i].IsConditional = ( Parents[i].Flags & PARENT_IsConditional ) ? 1 : 0;
	}
}

void FRepLayout::InitRepState( 
	FRepState *									RepState,
	UClass *									InObjectClass, 
	uint8 *										Src, 
	TSharedPtr< FRepChangedPropertyTracker > &	InRepChangedPropertyTracker ) const
{
	RepState->StaticBuffer.Empty();
	RepState->StaticBuffer.AddZeroed( InObjectClass->GetDefaultsCount() );

	// Construct the properties
	ConstructProperties( RepState );

	// Init the properties
	InitProperties( RepState, Src );
	
	RepState->RepChangedPropertyTracker = InRepChangedPropertyTracker;

	check( RepState->RepChangedPropertyTracker->Parents.Num() == Parents.Num() );

	// Start out the conditional props based on a default RepFlags struct
	// It will rebuild if it ever changes
	RebuildConditionalProperties( RepState, *InRepChangedPropertyTracker.Get(), FReplicationFlags() );
}

void FRepLayout::ConstructProperties( FRepState * RepState ) const
{
	uint8* StoredData = RepState->StaticBuffer.GetData();

	// Construct all items
	for ( int32 i = 0; i < Parents.Num(); i++ )
	{
		// Only construct the 0th element of static arrays (InitializeValue will handle the elements)
		if ( Parents[i].ArrayIndex == 0 )
		{
			PTRINT Offset = Parents[i].Property->ContainerPtrToValuePtr<uint8>( StoredData ) - StoredData;
			check( Offset >= 0 && Offset < RepState->StaticBuffer.Num() );

			Parents[i].Property->InitializeValue( StoredData + Offset );
		}
	}
}

void FRepLayout::InitProperties( FRepState * RepState, uint8* Src ) const
{
	uint8* StoredData = RepState->StaticBuffer.GetData();

	// Init all items
	for ( int32 i = 0; i < Parents.Num(); i++ )
	{
		// Only copy the 0th element of static arrays (CopyCompleteValue will handle the elements)
		if ( Parents[i].ArrayIndex == 0 )
		{
			PTRINT Offset = Parents[i].Property->ContainerPtrToValuePtr<uint8>( StoredData ) - StoredData;
			check( Offset >= 0 && Offset < RepState->StaticBuffer.Num() );

			Parents[i].Property->CopyCompleteValue( StoredData + Offset, Src + Offset );
		}
	}
}

void FRepLayout::DestructProperties( FRepState * RepState ) const
{
	uint8* StoredData = RepState->StaticBuffer.GetData();

	// Destruct all items
	for ( int32 i = 0; i < Parents.Num(); i++ )
	{
		// Only copy the 0th element of static arrays (DestroyValue will handle the elements)
		if ( Parents[i].ArrayIndex == 0 )
		{
			PTRINT Offset = Parents[i].Property->ContainerPtrToValuePtr<uint8>( StoredData ) - StoredData;
			check( Offset >= 0 && Offset < RepState->StaticBuffer.Num() );

			Parents[i].Property->DestroyValue( StoredData + Offset );
		}
	}

	RepState->StaticBuffer.Empty();
}

void FRepLayout::GetLifetimeCustomDeltaProperties(TArray< int32 > & OutCustom, TArray< ELifetimeCondition >	& OutConditions)
{
	OutCustom.Empty();
	OutConditions.Empty();

	for ( int32 i = 0; i < Parents.Num(); i++ )
	{
		if ( Parents[i].Flags & PARENT_IsCustomDelta )
		{
			check( Parents[i].Property->RepIndex + Parents[i].ArrayIndex == i );

			OutCustom.Add(i);
			OutConditions.Add(Parents[i].Condition);
		}
	}
}

FRepState::~FRepState()
{
	if (RepLayout.IsValid() && StaticBuffer.Num() > 0)
	{	
		RepLayout->DestructProperties( this );
	}
}
