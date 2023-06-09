// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "AutomationControllerPrivatePCH.h"


FAutomationReport::FAutomationReport(FAutomationTestInfo& InTestInfo, bool InIsParent)
	: bEnabled( false )
	, bIsParent(InIsParent)
	, bNodeExpandInUI(false)
	, bSelfPassesFilter(false)
	, SupportFlags(0)
	, TestInfo( InTestInfo )
	, bTrackingHistory(false)
	, NumRecordsToKeep(0)
{
	// Enable smoke tests
	if ( TestInfo.GetTestFlags() == EAutomationTestFlags::SmokeFilter )
	{
		bEnabled = true;
	}

	if (!bIsParent)
	{
		LoadHistory();
	}
}


void FAutomationReport::Empty()
{
	//release references to all child tests
	ChildReports.Empty();
	ChildReportNameHashes.Empty();
	FilteredChildReports.Empty();
}


FString FAutomationReport::GetAssetName() const
{
	return TestInfo.GetTestParameter();
}


FString FAutomationReport::GetCommand() const
{
	return TestInfo.GetTestName();
}


const FString& FAutomationReport::GetDisplayName() const
{
	return TestInfo.GetDisplayName();
}


FString FAutomationReport::GetDisplayNameWithDecoration() const
{
	FString FinalDisplayName = TestInfo.GetDisplayName();
	//if this is an internal leaf node and the "decoration" name is being requested
	if (ChildReports.Num())
	{
		int32 NumChildren = GetTotalNumChildren();
		//append on the number of child tests
		return TestInfo.GetDisplayName() + FString::Printf(TEXT(" (%d)"), NumChildren);
	}
	return FinalDisplayName;
}


int32 FAutomationReport::GetTotalNumChildren() const
{
	int32 Total = 0;
	for (int32 ChildIndex = 0; ChildIndex < ChildReports.Num(); ++ChildIndex)
	{
		int ChildCount = ChildReports[ChildIndex]->GetTotalNumChildren();
		//Only count leaf nodes
		if (ChildCount == 0)
		{
			Total ++;
		}
		Total += ChildCount;
	}
	return Total;
}


void FAutomationReport::GetEnabledTestNames(TArray<FString>& OutEnabledTestNames, FString CurrentPath) const
{
	//if this is a leaf and this test is enabled
	if ((ChildReports.Num() == 0) && IsEnabled())
	{
		const FString FullTestName = CurrentPath.Len() > 0 ? CurrentPath.AppendChar(TCHAR('.')) + TestInfo.GetDisplayName() : TestInfo.GetDisplayName();
		OutEnabledTestNames.Add(FullTestName);
	}
	else
	{
		if( !CurrentPath.IsEmpty() )
		{
			CurrentPath += TEXT(".");
		}
		CurrentPath += TestInfo.GetDisplayName();
		//recurse through the hierarchy
		for (int32 ChildIndex = 0; ChildIndex < ChildReports.Num(); ++ChildIndex)
		{
			ChildReports[ChildIndex]->GetEnabledTestNames(OutEnabledTestNames,CurrentPath);
		}
	}
	return;
}


void FAutomationReport::SetEnabledTests(const TArray<FString>& EnabledTests, FString CurrentPath)
{
	if (ChildReports.Num() == 0)
	{
		//Find of the full name of this test and see if it is in our list
		const FString FullTestName = CurrentPath.Len() > 0 ? CurrentPath.AppendChar(TCHAR('.')) + TestInfo.GetDisplayName() : TestInfo.GetDisplayName();
		const bool bNewEnabled = EnabledTests.Contains(FullTestName);
		SetEnabled(bNewEnabled);
	}
	else
	{
		if( !CurrentPath.IsEmpty() )
		{
			CurrentPath += TEXT(".");
		}
		CurrentPath += TestInfo.GetDisplayName();

		//recurse through the hierarchy
		for (int32 ChildIndex = 0; ChildIndex < ChildReports.Num(); ++ChildIndex)
		{
			ChildReports[ChildIndex]->SetEnabledTests(EnabledTests,CurrentPath);
		}

		//Parent nodes should be checked if all of its children are
		const int32 TotalNumChildern = GetTotalNumChildren();
		const int32 EnabledChildren = GetEnabledTestsNum();
		bEnabled = (TotalNumChildern == EnabledChildren);
	}
}


int32 FAutomationReport::GetEnabledTestsNum() const
{
	int32 Total = 0;
	//if this is a leaf and this test is enabled
	if ((ChildReports.Num() == 0) && IsEnabled())
	{
		Total++;
	}
	else
	{
		//recurse through the hierarchy
		for (int32 ChildIndex = 0; ChildIndex < ChildReports.Num(); ++ChildIndex)
		{
			Total += ChildReports[ChildIndex]->GetEnabledTestsNum();
		}
	}
	return Total;
}

bool FAutomationReport::IsEnabled() const
{
	return bEnabled;
}


void FAutomationReport::SetEnabled(bool bShouldBeEnabled)
{
	bEnabled = bShouldBeEnabled;
	//set children to the same value
	for (int32 ChildIndex = 0; ChildIndex < FilteredChildReports.Num(); ++ChildIndex)
	{
		FilteredChildReports[ChildIndex]->SetEnabled(bShouldBeEnabled);
	}
}


void FAutomationReport::SetSupport(const int32 ClusterIndex)
{
	SupportFlags |= (1<<ClusterIndex);

	//ensure there is enough room in the array for status per platform
	for (int32 i = 0; i <= ClusterIndex; ++i)
	{
		//Make sure we have enough results for a single pass
		TArray<FAutomationTestResults> AutomationTestResult;
		AutomationTestResult.Add( FAutomationTestResults() );
		Results.Add( AutomationTestResult );
	}
}


bool FAutomationReport::IsSupported(const int32 ClusterIndex) const
{
	return (SupportFlags & (1<<ClusterIndex)) ? true : false;
}


uint32 FAutomationReport::GetTestFlags( ) const
{
	return TestInfo.GetTestFlags();
}

FString FAutomationReport::GetSourceFile() const
{
	return TestInfo.GetSourceFile();
}

int32 FAutomationReport::GetSourceFileLine() const
{
	return TestInfo.GetSourceFileLine();
}

void FAutomationReport::SetTestFlags( const uint32 InTestFlags)
{
	TestInfo.AddTestFlags( InTestFlags );

	if ( InTestFlags == EAutomationTestFlags::SmokeFilter )
	{
		bEnabled = true;
	}
}

const bool FAutomationReport::IsParent()
{
	return bIsParent;
}

const bool FAutomationReport::IsSmokeTest( )
{
	return GetTestFlags( ) & EAutomationTestFlags::SmokeFilter ? true : false;
}

bool FAutomationReport::SetFilter( TSharedPtr< AutomationFilterCollection > InFilter, const bool ParentPassedFilter )
{
	//assume that this node and all its children fail to pass the filter test
	bool bSelfOrChildPassedFilter = false;

	//assume this node should not be expanded in the UI
	bNodeExpandInUI = false;

	//test for empty search string or matching search string
	bSelfPassesFilter = InFilter->PassesAllFilters( SharedThis( this ) );

	//clear the currently filtered tests array
	FilteredChildReports.Empty();

	//see if any children pass the filter
	for( int32 ChildIndex = 0; ChildIndex < ChildReports.Num(); ++ChildIndex )
	{
		bool ThisChildPassedFilter = ChildReports[ChildIndex]->SetFilter( InFilter, bSelfPassesFilter );

		if( ThisChildPassedFilter || bSelfPassesFilter || ParentPassedFilter )
		{
			FilteredChildReports.Add( ChildReports[ ChildIndex ] );
		}

		if ( bNodeExpandInUI == false && ThisChildPassedFilter == true )
		{
			// A child node has passed the filter, so we want to expand this node in the UI
			bNodeExpandInUI = true;
		}
	}

	//if we passed name, speed, and status tests
	if( bSelfPassesFilter || bNodeExpandInUI )
	{
		//Passed the filter!
		bSelfOrChildPassedFilter = true;
	}

	return bSelfOrChildPassedFilter;
}

TArray<TSharedPtr<IAutomationReport> >& FAutomationReport::GetFilteredChildren()
{
	return FilteredChildReports;
}

TArray<TSharedPtr<IAutomationReport> >& FAutomationReport::GetChildReports()
{
	return ChildReports;
}

void FAutomationReport::ClustersUpdated(const int32 NumClusters)
{
	TestInfo.ResetNumDevicesRunningTest();

	//Fixup Support flags
	SupportFlags = 0;
	for (int32 i = 0; i <= NumClusters; ++i)
	{
		SupportFlags |= (1<<i);
	}

	//Fixup Results array
	if( NumClusters > Results.Num() )
	{
		for( int32 ClusterIndex = Results.Num(); ClusterIndex < NumClusters; ++ClusterIndex )
		{
			//Make sure we have enough results for a single pass
			TArray<FAutomationTestResults> AutomationTestResult;
			AutomationTestResult.Add( FAutomationTestResults() );
			Results.Add( AutomationTestResult );
		}
	}
	else if( NumClusters < Results.Num() )
	{
		Results.RemoveAt(NumClusters, Results.Num() - NumClusters);
	}

	//recurse to children
	for (int32 ChildIndex = 0; ChildIndex < ChildReports.Num(); ++ChildIndex)
	{
		ChildReports[ChildIndex]->ClustersUpdated(NumClusters);
	}
}

void FAutomationReport::ResetForExecution(const int32 NumTestPasses)
{
	TestInfo.ResetNumDevicesRunningTest();

	//if this test is enabled
	if (IsEnabled())
	{
		for (int32 ClusterIndex = 0; ClusterIndex < Results.Num(); ++ClusterIndex)
		{
			//Make sure we have enough results
			if( NumTestPasses > Results[ClusterIndex].Num() )
			{
				for(int32 PassCount = Results[ClusterIndex].Num(); PassCount < NumTestPasses; ++PassCount)
				{
					Results[ClusterIndex].Add( FAutomationTestResults() );
				}
			}
			else if( NumTestPasses < Results[ClusterIndex].Num() )
			{
				Results[ClusterIndex].RemoveAt(NumTestPasses, Results[ClusterIndex].Num() - NumTestPasses);
			}

			for( int32 PassIndex = 0; PassIndex < Results[ClusterIndex].Num(); ++PassIndex)
			{
				//reset all stats
				Results[ClusterIndex][PassIndex].State = EAutomationState::NotRun;
				Results[ClusterIndex][PassIndex].Warnings.Empty();
				Results[ClusterIndex][PassIndex].Errors.Empty();
			}
		}
	}
	//recurse to children
	for (int32 ChildIndex = 0; ChildIndex < ChildReports.Num(); ++ChildIndex)
	{
		ChildReports[ChildIndex]->ResetForExecution(NumTestPasses);
	}
}


void FAutomationReport::TrackHistory(const bool bShouldTrack, const int32 NumReportsToTrack)
{
	bTrackingHistory = bShouldTrack;
	NumRecordsToKeep = NumReportsToTrack;

	if (bTrackingHistory && ChildReports.Num() == 0)
	{
		LoadHistory();
	}

	//recurse to children
	for (auto& NextChildReport : ChildReports)
	{
		NextChildReport->TrackHistory(bTrackingHistory, NumRecordsToKeep);
	}
}


void FAutomationReport::AddToHistory()
{
	// Dictate the file path we are writing this run as history to.
	const FDateTime FileDate = FDateTime::Now();
	const FString FileName = GetDisplayName() + FileDate.ToString() + TEXT(".log");
	const FString FileLocation = FPaths::ConvertRelativePathToFull(FPaths::AutomationLogDir()) + GetDisplayName();
	const FString FullPath = FPaths::Combine(*FileLocation, *FileName);

	// Write any Errors and Warnings to the log, if none, then simply report that it was successful.
	if (FArchive* LogFile = IFileManager::Get().CreateFileWriter(*FullPath))
	{
		bool bExportedAnyErrors = false;
		for (int32 ClusterIndex = 0; ClusterIndex < Results.Num(); ++ClusterIndex)
		{
			for (int32 PassIndex = 0; PassIndex < Results[ClusterIndex].Num(); ++PassIndex)
			{
				for (int32 ErrorIndex = 0; ErrorIndex < Results[ClusterIndex][PassIndex].Errors.Num(); ++ErrorIndex)
				{
					if (!bExportedAnyErrors)
					{
						bExportedAnyErrors = true;

						FString ErrorIdentifier(TEXT("<<ERRORS>>"));
						ErrorIdentifier += LINE_TERMINATOR;
						
						LogFile->Serialize(TCHAR_TO_ANSI(*ErrorIdentifier), ErrorIdentifier.Len());
					}
					FString NextError = Results[ClusterIndex][PassIndex].Errors[ErrorIndex] + LINE_TERMINATOR;
					LogFile->Serialize(TCHAR_TO_ANSI(*NextError), NextError.Len());
				}
			}
		}

		bool bExportedAnyWarnings = false;
		for (int32 ClusterIndex = 0; ClusterIndex < Results.Num(); ++ClusterIndex)
		{
			for (int32 PassIndex = 0; PassIndex < Results[ClusterIndex].Num(); ++PassIndex)
			{
				for (int32 WarningIndex = 0; WarningIndex < Results[ClusterIndex][PassIndex].Warnings.Num(); ++WarningIndex)
				{
					if (!bExportedAnyWarnings)
					{
						bExportedAnyWarnings = true;

						FString WarningIdentifier(TEXT("<<WARNINGS>>"));
						WarningIdentifier += LINE_TERMINATOR;

						LogFile->Serialize(TCHAR_TO_ANSI(*WarningIdentifier), WarningIdentifier.Len());
					}
					const FString NextWarning = Results[ClusterIndex][PassIndex].Warnings[WarningIndex] + LINE_TERMINATOR;
					LogFile->Serialize(TCHAR_TO_ANSI(*NextWarning), NextWarning.Len());
				}
			}
		}

		if (bExportedAnyErrors == false && bExportedAnyWarnings == false)
		{
			FString SuccessIdentifier(TEXT("<<SUCCESS>>"));
			SuccessIdentifier += LINE_TERMINATOR;

			LogFile->Serialize(TCHAR_TO_ANSI(*SuccessIdentifier), SuccessIdentifier.Len());
		}

		LogFile->Close();
		delete LogFile;


		// Cache an automation history item for tracking in this session
		TSharedRef<FAutomationHistoryItem> HistoryItem = MakeShareable(new FAutomationHistoryItem);
		HistoryItem->LogLocation = FileName;
		HistoryItem->RunDate = FileDate;
		HistoryItem->RunResult = 
			(bExportedAnyErrors ? FAutomationHistoryItem::EAutomationHistoryResult::Errors : 
			(bExportedAnyWarnings ? FAutomationHistoryItem::EAutomationHistoryResult::Warnings : 
			FAutomationHistoryItem::EAutomationHistoryResult::Successful));

		HistoryItems.Add(HistoryItem);
	}
}


void FAutomationReport::MaintainHistory(TArray<FString>& InLogFiles)
{
	// Find all the logs in this reports log location
	const FString LogsLocation = FPaths::ConvertRelativePathToFull(FPaths::AutomationLogDir()) + GetDisplayName();

	// Sort the logs in reverse chronological order
	struct FLogSortPredicate
	{
		FString DisplayName;
		FLogSortPredicate(const FString& InDisplayName) : DisplayName(InDisplayName) {}

		/** Sort predicate operator */
		bool operator ()(FString LHS, FString RHS) const
		{
			FString LogExt = TEXT(".log");

			FString LHSDateStr = LHS.RightChop(DisplayName.Len());
			LHSDateStr = LHSDateStr.LeftChop(LogExt.Len());

			FDateTime LHSDate;
			FDateTime::Parse(LHSDateStr, LHSDate);

			FString RHSDateStr = RHS.RightChop(DisplayName.Len());
			RHSDateStr = RHSDateStr.LeftChop(LogExt.Len());

			FDateTime RHSDate;
			FDateTime::Parse(RHSDateStr, RHSDate);

			return LHSDate > RHSDate;
		}
	};
	InLogFiles.Sort(FLogSortPredicate(GetDisplayName()));

	// For logs, we keep the number equal to AutomationReportConstants::MaximumLogsToKeep around.
	// This will mean that we can extend or history to see when changed within the report
	for (int32 LogIndex = InLogFiles.Num() - 1; LogIndex >= AutomationReportConstants::MaximumLogsToKeep; LogIndex--)
	{
		check(IFileManager::Get().Delete(*FPaths::Combine(*LogsLocation, *InLogFiles[LogIndex])));
	}


	// Sort the history items in reverse chronological order
	struct FHistorySortPredicate
	{
		FHistorySortPredicate() {}

		/** Sort predicate operator */
		bool operator ()(const TSharedPtr<FAutomationHistoryItem>& LHS, const TSharedPtr<FAutomationHistoryItem>& RHS) const
		{
			check(LHS.IsValid() && RHS.IsValid());
			return LHS->RunDate > RHS->RunDate;
		}
	};
	HistoryItems.Sort(FHistorySortPredicate());

	for (int32 ItemIndex = HistoryItems.Num() - 1; ItemIndex >= NumRecordsToKeep; ItemIndex--)
	{
		HistoryItems.RemoveAt(ItemIndex);
	}
}


void FAutomationReport::LoadHistory()
{
	// Clear out the previous results before we rebuild our list
	HistoryItems.Empty();

	// Load the logs from this reports automation log location
	const FString LogsLocation = FPaths::ConvertRelativePathToFull(FPaths::AutomationLogDir()) + GetDisplayName();

	TArray<FString> LogFiles;
	IFileManager::Get().FindFiles(LogFiles, *(LogsLocation / "*.log"), true, false);

	for (FString& NextLogFile : LogFiles)
	{
		FString FileContents;
		if (FFileHelper::LoadFileToString(FileContents, *FPaths::Combine(*LogsLocation, *NextLogFile)))
		{
			TSharedRef<FAutomationHistoryItem> HistoryItem = MakeShareable(new FAutomationHistoryItem);

			// Cache the log location
			HistoryItem->LogLocation = NextLogFile;

			// Parse the date and time from the log name
			{
				FString LogExt = TEXT(".log");

				FString DateStr = NextLogFile.RightChop(GetDisplayName().Len());
				DateStr = DateStr.LeftChop(LogExt.Len());

				FDateTime::Parse(DateStr, HistoryItem->RunDate);
			}

			// Parse whether the previous runs had errors, warnings or were successful
			{
				if (FileContents.StartsWith(TEXT("<<ERRORS>>")))
				{
					HistoryItem->RunResult = FAutomationHistoryItem::EAutomationHistoryResult::Errors;
				}
				else if (FileContents.StartsWith(TEXT("<<WARNINGS>>")))
				{
					HistoryItem->RunResult = FAutomationHistoryItem::EAutomationHistoryResult::Warnings;
				}
				else if (FileContents.StartsWith(TEXT("<<SUCCESS>>")))
				{
					HistoryItem->RunResult = FAutomationHistoryItem::EAutomationHistoryResult::Successful;
				}
			}

			// Add our log to the tracking
			HistoryItems.Add(HistoryItem);
		}
	}

	// Do a pass on the existing logs for any we no longer wish to maintain.
	if (LogFiles.Num())
	{
		MaintainHistory(LogFiles);
	}
}


const TArray<TSharedPtr<FAutomationHistoryItem>>& FAutomationReport::GetHistory() const
{
	return HistoryItems;
}


void FAutomationReport::SetResults( const int32 ClusterIndex, const int32 PassIndex, const FAutomationTestResults& InResults )
{
	//verify this is a platform this test is aware of
	check((ClusterIndex >= 0) && (ClusterIndex < Results.Num()));
	check((PassIndex >= 0) && (PassIndex < Results[ClusterIndex].Num()));

	if( InResults.State == EAutomationState::InProcess )
	{
		TestInfo.InformOfNewDeviceRunningTest();
	}

	Results[ ClusterIndex ][ PassIndex ] = InResults;
	// Add an error report if none was received
	if ( InResults.State == EAutomationState::Fail && InResults.Errors.Num() == 0 && InResults.Warnings.Num() == 0 )
	{
		Results[ClusterIndex][PassIndex].Errors.Add( "No Report Generated" );
	}

	// If we are tracking history, then export it.
	if (bTrackingHistory && (InResults.State == EAutomationState::Success || InResults.State == EAutomationState::Fail))
	{
		AddToHistory();
		//Remove find files as it was too expensive.  And definitely too expensive for just updating one test
		//MaintainHistory();
	}
}


void FAutomationReport::GetCompletionStatus(const int32 ClusterIndex, const int32 PassIndex, FAutomationCompleteState& OutCompletionState)
{
	//if this test is enabled and a leaf test
	if (IsSupported(ClusterIndex) && (ChildReports.Num()==0))
	{
		EAutomationState::Type CurrentState = Results[ClusterIndex][PassIndex].State;
		//Enabled and In-Process
		if (IsEnabled())
		{
			OutCompletionState.TotalEnabled++;
			if (CurrentState == EAutomationState::InProcess)
			{
				OutCompletionState.NumEnabledInProcess++;
			}
		}

		//Warnings
		if (Results[ClusterIndex][PassIndex].Warnings.Num() > 0)
		{
			IsEnabled() ? OutCompletionState.NumEnabledTestsWarnings++ : OutCompletionState.NumDisabledTestsWarnings++;
		}

		//Test results
		if (CurrentState == EAutomationState::Success)
		{
			IsEnabled() ? OutCompletionState.NumEnabledTestsPassed++ : OutCompletionState.NumDisabledTestsPassed++;
		}
		else if (CurrentState == EAutomationState::Fail)
		{
			IsEnabled() ? OutCompletionState.NumEnabledTestsFailed++ : OutCompletionState.NumDisabledTestsFailed++;
		}
		else if( CurrentState == EAutomationState::NotEnoughParticipants )
		{
			IsEnabled() ? OutCompletionState.NumEnabledTestsCouldntBeRun++ : OutCompletionState.NumDisabledTestsCouldntBeRun++;
		}
	}
	//recurse to children
	for (int32 ChildIndex = 0; ChildIndex < ChildReports.Num(); ++ChildIndex)
	{
		ChildReports[ChildIndex]->GetCompletionStatus(ClusterIndex,PassIndex, OutCompletionState);
	}
}


EAutomationState::Type FAutomationReport::GetState(const int32 ClusterIndex, const int32 PassIndex) const
{
	if ((ClusterIndex >= 0) && (ClusterIndex < Results.Num()) &&
		(PassIndex >= 0) && (PassIndex < Results[ClusterIndex].Num()))
	{
		return Results[ClusterIndex][PassIndex].State;
	}
	return EAutomationState::NotRun;
}


const FAutomationTestResults& FAutomationReport::GetResults( const int32 ClusterIndex, const int32 PassIndex ) 
{
	return Results[ClusterIndex][PassIndex];
}

const int32 FAutomationReport::GetNumResults( const int32 ClusterIndex )
{
	return Results[ClusterIndex].Num();
}

const int32 FAutomationReport::GetCurrentPassIndex( const int32 ClusterIndex )
{
	int32 PassIndex = 1;

	if( IsSupported(ClusterIndex) )
	{
		for(; PassIndex < Results[ClusterIndex].Num(); ++PassIndex )
		{
			if( Results[ClusterIndex][PassIndex].State == EAutomationState::NotRun )
			{
				break;
			}
		}
	}

	return PassIndex - 1;
}

FString FAutomationReport::GetGameInstanceName( const int32 ClusterIndex )
{
	return Results[ClusterIndex][0].GameInstance;
}

TSharedPtr<IAutomationReport> FAutomationReport::EnsureReportExists(FAutomationTestInfo& InTestInfo, const int32 ClusterIndex, const int32 NumPasses)
{
	//Split New Test Name by the first "." found
	FString NameToMatch = InTestInfo.GetDisplayName();
	FString NameRemainder;
	//if this is a leaf test (no ".")
	if (!InTestInfo.GetDisplayName().Split(TEXT("."), &NameToMatch, &NameRemainder))
	{
		NameToMatch = InTestInfo.GetDisplayName();
	}

	if ( NameRemainder.Len() != 0 )
	{
		// Set the test info name to be the remaining string
		InTestInfo.SetDisplayName( NameRemainder );
	}

	uint32 NameToMatchHash = GetTypeHash(NameToMatch);

	TSharedPtr<IAutomationReport> MatchTest;
	//check hash table first to see if it exists yet
	if (ChildReportNameHashes.Contains(NameToMatchHash))
	{
		//go backwards.  Most recent event most likely matches
		int32 TestIndex = ChildReports.Num() - 1;
		for (; TestIndex >= 0; --TestIndex)
		{
			//if the name matches
			if (ChildReports[TestIndex]->GetDisplayName() == NameToMatch)
			{
				MatchTest = ChildReports[TestIndex];
				break;
			}
		}
	}

	//if there isn't already a test like this
	if (!MatchTest.IsValid())
	{
		if ( NameRemainder.Len() == 0 )
		{
			// Create a new leaf node
			MatchTest = MakeShareable(new FAutomationReport(InTestInfo));			
		}
		else
		{
			// Create a parent node
			FAutomationTestInfo ParentTestInfo( NameToMatch, "", InTestInfo.GetTestFlags(), InTestInfo.GetNumParticipantsRequired() ) ;
			MatchTest = MakeShareable(new FAutomationReport(ParentTestInfo, true));
		}
		//make new test
		ChildReports.Add(MatchTest);
		ChildReportNameHashes.Add(NameToMatchHash, NameToMatchHash);
	}
	//mark this test as supported on a particular platform
	MatchTest->SetSupport(ClusterIndex);

	MatchTest->SetTestFlags( InTestInfo.GetTestFlags() );
	MatchTest->SetNumParticipantsRequired( MatchTest->GetNumParticipantsRequired() > InTestInfo.GetNumParticipantsRequired() ? MatchTest->GetNumParticipantsRequired() : InTestInfo.GetNumParticipantsRequired() );

	TSharedPtr<IAutomationReport> FoundTest;
	//if this is a leaf node
	if (NameRemainder.Len() == 0)
	{
		FoundTest = MatchTest;
	}
	else
	{
		//recurse to add to the proper layer
		FoundTest = MatchTest->EnsureReportExists(InTestInfo, ClusterIndex, NumPasses);
	}

	return FoundTest;
}


TSharedPtr<IAutomationReport> FAutomationReport::GetNextReportToExecute(bool& bOutAllTestsComplete, const int32 ClusterIndex, const int32 PassIndex, const int32 NumDevicesInCluster)
{
	TSharedPtr<IAutomationReport> NextReport;
	//if this is not a leaf node
	if (ChildReports.Num())
	{
		for (int32 ReportIndex = 0; ReportIndex < ChildReports.Num(); ++ReportIndex)
		{
			NextReport = ChildReports[ReportIndex]->GetNextReportToExecute(bOutAllTestsComplete, ClusterIndex, PassIndex, NumDevicesInCluster);
			//if we found one, return it
			if (NextReport.IsValid())
			{
				break;
			}
		}
	}
	else
	{
		//consider self
		if (IsEnabled() && IsSupported(ClusterIndex))
		{
			EAutomationState::Type TestState = GetState(ClusterIndex,PassIndex);
			//if any enabled test hasn't been run yet or is in process
			if ((TestState != EAutomationState::Success) && (TestState != EAutomationState::Fail) && (TestState != EAutomationState::NotEnoughParticipants))
			{
				//make sure we announce we are NOT done with all tests
				bOutAllTestsComplete = false;
			}
			if (TestState == EAutomationState::NotRun)
			{
				//Found one to run next
				NextReport = AsShared();
			}
		}
	}
	return NextReport;
}
const bool FAutomationReport::HasErrors()
{
	bool bHasErrors = false;
	for (int32 ClusterIndex = 0; ClusterIndex < Results.Num(); ++ClusterIndex )
	{
		for( int32 PassIndex = 0; PassIndex < Results[ClusterIndex].Num(); ++PassIndex)
		{
			//if we want tests with errors and this test had them OR we want tests warnings and this test had them
			if( Results[ ClusterIndex ][ PassIndex ].Errors.Num() ) 
			{
				//mark this test as having passed the results filter
				bHasErrors = true;
				break;
			}
		}
	}
	return bHasErrors;
}

const bool FAutomationReport::HasWarnings()
{
	bool bHasWarnings = false;
	for (int32 ClusterIndex = 0; ClusterIndex < Results.Num(); ++ClusterIndex )
	{
		for( int32 PassIndex = 0; PassIndex < Results[ClusterIndex].Num(); ++PassIndex)
		{
			//if we want tests with errors and this test had them OR we want tests warnings and this test had them
			if( Results[ ClusterIndex ][ PassIndex ].Warnings.Num() ) 
			{
				//mark this test as having passed the results filter
				bHasWarnings = true;
				break;
			}
		}
	}
	return bHasWarnings;
}


const bool FAutomationReport::GetDurationRange(float& OutMinTime, float& OutMaxTime)
{
	//assume we haven't found any tests that have completed successfully
	OutMinTime = MAX_FLT;
	OutMaxTime = 0.0f;
	bool bAnyResultsFound = false;

	//keep sum of all child tests
	float ChildTotalMinTime = 0.0f;
	float ChildTotalMaxTime = 0.0f;
	for (int32 ReportIndex = 0; ReportIndex < ChildReports.Num(); ++ReportIndex)
	{
		float ChildMinTime = MAX_FLT;
		float ChildMaxTime = 0.0f;
		if (ChildReports[ReportIndex]->GetDurationRange(ChildMinTime, ChildMaxTime))
		{
			ChildTotalMinTime += ChildMinTime;
			ChildTotalMaxTime += ChildMaxTime;
			bAnyResultsFound = true;
		}
	}

	//if any child test had valid timings
	if (bAnyResultsFound)
	{
		OutMinTime = ChildTotalMinTime;
		OutMaxTime = ChildTotalMaxTime;
	}

	for (int32 ClusterIndex = 0; ClusterIndex < Results.Num(); ++ClusterIndex )
	{
		for( int32 PassIndex = 0; PassIndex < Results[ClusterIndex].Num(); ++PassIndex)
		{
			//if we want tests with errors and this test had them OR we want tests warnings and this test had them
			if( Results[ClusterIndex][PassIndex].State == EAutomationState::Success ||
				Results[ClusterIndex][PassIndex].State == EAutomationState::Fail)
			{
				OutMinTime = FMath::Min(OutMinTime, Results[ClusterIndex][PassIndex].Duration );
				OutMaxTime = FMath::Max(OutMaxTime, Results[ClusterIndex][PassIndex].Duration );
				bAnyResultsFound = true;
			}
		}
	}
	return bAnyResultsFound;
}


const int32 FAutomationReport::GetNumDevicesRunningTest() const
{
	return TestInfo.GetNumDevicesRunningTest();
}


const int32 FAutomationReport::GetNumParticipantsRequired() const
{
	return TestInfo.GetNumParticipantsRequired();
}


void FAutomationReport::SetNumParticipantsRequired( const int32 NewCount )
{
	TestInfo.SetNumParticipantsRequired( NewCount );
}


bool FAutomationReport::IncrementNetworkCommandResponses()
{
	NumberNetworkResponsesReceived++;
	return (NumberNetworkResponsesReceived == TestInfo.GetNumParticipantsRequired());
}


void FAutomationReport::ResetNetworkCommandResponses()
{
	NumberNetworkResponsesReceived = 0;
}


const bool FAutomationReport::ExpandInUI() const
{
	return bNodeExpandInUI;
}


void FAutomationReport::StopRunningTest()
{
	if( IsEnabled() )
	{
		for( int32 ResultsIndex = 0; ResultsIndex < Results.Num(); ++ResultsIndex )
		{
			for( int32 PassIndex = 0; PassIndex < Results[ResultsIndex].Num(); ++PassIndex)
			{
				if( Results[ResultsIndex][PassIndex].State == EAutomationState::InProcess )
				{
					Results[ResultsIndex][PassIndex].State = EAutomationState::NotRun;
				}
			}
		}
	}

	// Recurse to children
	for( int32 ChildIndex = 0; ChildIndex < ChildReports.Num(); ++ChildIndex )
	{
		ChildReports[ChildIndex]->StopRunningTest();
	}
}