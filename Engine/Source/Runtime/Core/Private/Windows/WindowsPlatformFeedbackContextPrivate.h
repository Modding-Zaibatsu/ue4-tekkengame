// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/OutputDeviceConsole.h"


/**
 * Feedback context implementation for windows.
 */
class CORE_API FFeedbackContextWindows : public FFeedbackContext
{
	/** Context information for warning and error messages */
	FContextSupplier*	Context;

public:
	// Variables.

	// Constructor.
	FFeedbackContextWindows()
	: FFeedbackContext()
	, Context( NULL )
	{}
	void Serialize( const TCHAR* V, ELogVerbosity::Type Verbosity, const class FName& Category )
	{
		// if we set the color for warnings or errors, then reset at the end of the function
		// note, we have to set the colors directly without using the standard SET_WARN_COLOR macro
		if( Verbosity==ELogVerbosity::Error || Verbosity==ELogVerbosity::Warning )
		{
			if( TreatWarningsAsErrors && Verbosity==ELogVerbosity::Warning )
			{
				Verbosity = ELogVerbosity::Error;
			}

			FString Prefix;
			if( Context )
			{
				Prefix = Context->GetContext() + TEXT(" : ");
			}
			FString Format = Prefix + FOutputDeviceHelper::FormatLogLine(Verbosity, Category, V);

			if(Verbosity == ELogVerbosity::Error)
			{
				// Only store off the message if running a commandlet.
				if ( IsRunningCommandlet() )
				{
					AddError(Format);
				}
			}
			else
			{
				// Only store off the message if running a commandlet.
				if ( IsRunningCommandlet() )
				{
					AddWarning(Format);
				}
			}
		}

		if( GLogConsole && IsRunningCommandlet() )
		{
			GLogConsole->Serialize( V, Verbosity, Category );
		}
		if( !GLog->IsRedirectingTo( this ) )
		{
			GLog->Serialize( V, Verbosity, Category );
		}
	}

	virtual bool YesNof(const FText& Question) override;

	FContextSupplier* GetContext() const
	{
		return Context;
	}
	void SetContext( FContextSupplier* InSupplier )
	{
		Context = InSupplier;
	}
};

