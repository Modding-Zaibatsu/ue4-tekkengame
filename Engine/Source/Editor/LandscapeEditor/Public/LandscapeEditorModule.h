// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LandscapeFileFormatInterface.h"

/**
 * LandscapeEditor module interface
 */
class ILandscapeEditorModule : public IModuleInterface
{
public:
	// Register / unregister a landscape file format plugin
	virtual void RegisterHeightmapFileFormat(TSharedRef<ILandscapeHeightmapFileFormat> FileFormat) = 0;
	virtual void RegisterWeightmapFileFormat(TSharedRef<ILandscapeWeightmapFileFormat> FileFormat) = 0;
	virtual void UnregisterHeightmapFileFormat(TSharedRef<ILandscapeHeightmapFileFormat> FileFormat) = 0;
	virtual void UnregisterWeightmapFileFormat(TSharedRef<ILandscapeWeightmapFileFormat> FileFormat) = 0;

	// Gets the type string used by the import/export file dialog
	virtual const TCHAR* GetHeightmapImportDialogTypeString() const = 0;
	virtual const TCHAR* GetWeightmapImportDialogTypeString() const = 0;
	virtual const TCHAR* GetHeightmapExportDialogTypeString() const = 0;
	virtual const TCHAR* GetWeightmapExportDialogTypeString() const = 0;

	// Gets the heightmap/weightmap format associated with a given extension (null if no plugin is registered for this extension)
	virtual const ILandscapeHeightmapFileFormat* GetHeightmapFormatByExtension(const TCHAR* Extension) = 0;
	virtual const ILandscapeWeightmapFileFormat* GetWeightmapFormatByExtension(const TCHAR* Extension) = 0;
};
