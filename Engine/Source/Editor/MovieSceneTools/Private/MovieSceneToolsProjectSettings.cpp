// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "MovieSceneToolsPrivatePCH.h"
#include "MovieSceneToolsProjectSettings.h"


UMovieSceneToolsProjectSettings::UMovieSceneToolsProjectSettings()
	: DefaultStartTime(0.f)
	, DefaultDuration(5.f)
	, ShotDirectory(TEXT("shots"))
	, ShotPrefix(TEXT("shot"))
	, FirstShotNumber(10)
	, ShotIncrement(10)
	, ShotNumDigits(4)
	, TakeNumDigits(3)
	, FirstTakeNumber(1)
	, TakeSeparator(TEXT("_"))
{ }
