// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "UnrealEd.h"
#include "EditorProjectSettings.h"


EUnit ConvertDefaultInputUnits(EDefaultLocationUnit In)
{
	typedef EDefaultLocationUnit EDefaultLocationUnit;

	switch(In)
	{
	case EDefaultLocationUnit::Micrometers:		return EUnit::Micrometers;
	case EDefaultLocationUnit::Millimeters:		return EUnit::Millimeters;
	case EDefaultLocationUnit::Centimeters:		return EUnit::Centimeters;
	case EDefaultLocationUnit::Meters:			return EUnit::Meters;
	case EDefaultLocationUnit::Kilometers:		return EUnit::Kilometers;
	case EDefaultLocationUnit::Inches:			return EUnit::Inches;
	case EDefaultLocationUnit::Feet:			return EUnit::Feet;
	case EDefaultLocationUnit::Yards:			return EUnit::Yards;
	case EDefaultLocationUnit::Miles:			return EUnit::Miles;
	default:									return EUnit::Centimeters;
	}
}

TArray<EUnit> ToRawUnits(const TArray<TEnumAsByte<EUnit>>& In)
{
	TArray<EUnit> Units;
	for (const auto AsByte : In)
	{
		Units.Add(AsByte);
	}
	return Units;	
}

UEditorProjectAppearanceSettings::UEditorProjectAppearanceSettings(const FObjectInitializer& Initializer)
	: Super(Initializer)
	, UnitDisplay_DEPRECATED(EUnitDisplay::Invalid)
	, DefaultInputUnits_DEPRECATED(EDefaultLocationUnit::Invalid)
{
}

void UEditorProjectAppearanceSettings::PostEditChangeProperty( struct FPropertyChangedEvent& PropertyChangedEvent )
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName Name = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	auto& Settings = FUnitConversion::Settings();
	if (Name == GET_MEMBER_NAME_CHECKED(UEditorProjectAppearanceSettings, DistanceUnits))
	{
		TArray<EUnit> TempArray = ToRawUnits(DistanceUnits);
		Settings.SetDisplayUnits(EUnitType::Distance, TempArray);
	}
	else if (Name == GET_MEMBER_NAME_CHECKED(UEditorProjectAppearanceSettings, MassUnits))
	{
		TArray<EUnit> TempArray = ToRawUnits(MassUnits);
		Settings.SetDisplayUnits(EUnitType::Mass, TempArray);
	}
	else if (Name == GET_MEMBER_NAME_CHECKED(UEditorProjectAppearanceSettings, TimeUnits))
	{
		TArray<EUnit> TempArray = ToRawUnits(TimeUnits);
		Settings.SetDisplayUnits(EUnitType::Time, TempArray);
	}
	else if (Name == GET_MEMBER_NAME_CHECKED(UEditorProjectAppearanceSettings, AngleUnits))
	{
		Settings.SetDisplayUnits(EUnitType::Angle, AngleUnits);
	}
	else if (Name == GET_MEMBER_NAME_CHECKED(UEditorProjectAppearanceSettings, SpeedUnits))
	{
		Settings.SetDisplayUnits(EUnitType::Speed, SpeedUnits);
	}
	else if (Name == GET_MEMBER_NAME_CHECKED(UEditorProjectAppearanceSettings, TemperatureUnits))
	{
		Settings.SetDisplayUnits(EUnitType::Temperature, TemperatureUnits);
	}
	else if (Name == GET_MEMBER_NAME_CHECKED(UEditorProjectAppearanceSettings, ForceUnits))
	{
		Settings.SetDisplayUnits(EUnitType::Force, ForceUnits);
	}
	else if (Name == GET_MEMBER_NAME_CHECKED(UEditorProjectAppearanceSettings, bDisplayUnits))
	{
		Settings.SetShouldDisplayUnits(bDisplayUnits);
	}

	DefaultInputUnits_DEPRECATED = EDefaultLocationUnit::Invalid;
	UnitDisplay_DEPRECATED = EUnitDisplay::Invalid;

	SaveConfig();
}

void SetupEnumMetaData(UClass* Class, const FName& MemberName, const TCHAR* Values)
{
	UArrayProperty* Array = Cast<UArrayProperty>(Class->FindPropertyByName(MemberName));
	if (Array && Array->Inner)
	{
		Array->Inner->SetMetaData(TEXT("ValidEnumValues"), Values);
	}
}

void UEditorProjectAppearanceSettings::PostInitProperties()
{
	Super::PostInitProperties();

	/** Setup the meta data for the array properties */
	SetupEnumMetaData(GetClass(), GET_MEMBER_NAME_CHECKED(UEditorProjectAppearanceSettings, DistanceUnits), TEXT("Micrometers, Millimeters, Centimeters, Meters, Kilometers, Inches, Feet, Yards, Miles"));
	SetupEnumMetaData(GetClass(), GET_MEMBER_NAME_CHECKED(UEditorProjectAppearanceSettings, MassUnits), TEXT("Micrograms, Milligrams, Grams, Kilograms, MetricTons,	Ounces, Pounds, Stones"));
	SetupEnumMetaData(GetClass(), GET_MEMBER_NAME_CHECKED(UEditorProjectAppearanceSettings, TimeUnits), TEXT("Milliseconds, Seconds, Minutes, Hours, Days, Months, Years"));

	if (UnitDisplay_DEPRECATED != EUnitDisplay::Invalid)
	{
		bDisplayUnits = UnitDisplay_DEPRECATED != EUnitDisplay::None;
	}

	if (DefaultInputUnits_DEPRECATED != EDefaultLocationUnit::Invalid)
	{
		DistanceUnits.Empty();
		DistanceUnits.Add(ConvertDefaultInputUnits(DefaultInputUnits_DEPRECATED));
	}

	auto& Settings = FUnitConversion::Settings();

	TArray<EUnit> TempArray = ToRawUnits(DistanceUnits);
	Settings.SetDisplayUnits(EUnitType::Distance, TempArray);

	TempArray = ToRawUnits(MassUnits);
	Settings.SetDisplayUnits(EUnitType::Mass, TempArray);

	TempArray = ToRawUnits(TimeUnits);
	Settings.SetDisplayUnits(EUnitType::Time, TempArray);

	Settings.SetDisplayUnits(EUnitType::Angle, AngleUnits);
	Settings.SetDisplayUnits(EUnitType::Speed, SpeedUnits);
	Settings.SetDisplayUnits(EUnitType::Temperature, TemperatureUnits);
	Settings.SetDisplayUnits(EUnitType::Force, ForceUnits);

	Settings.SetShouldDisplayUnits(bDisplayUnits);
}


/* ULevelEditor2DSettings
*****************************************************************************/

ULevelEditor2DSettings::ULevelEditor2DSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, SnapAxis(ELevelEditor2DAxis::Y)
{
	SnapLayers.Emplace(TEXT("Foreground"), 100.0f);
	SnapLayers.Emplace(TEXT("Default"), 0.0f);
	SnapLayers.Emplace(TEXT("Background"), -100.0f);
}

void ULevelEditor2DSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	// Sort the snap layers
	SnapLayers.Sort([](const FMode2DLayer& LHS, const FMode2DLayer& RHS){ return LHS.Depth > RHS.Depth; });

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
