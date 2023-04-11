﻿// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "UnrealEd.h"
#include "Culture.h"
#include "JsonInternationalizationManifestSerializer.h"
#include "JsonInternationalizationArchiveSerializer.h"
#include "PortableObjectFormatDOM.h"

DEFINE_LOG_CATEGORY_STATIC(LogInternationalizationExportCommandlet, Log, All);

static const TCHAR* NewLineDelimiter = TEXT("\n");


/**
*	Helper Functions
*/
bool operator==(const FPortableObjectEntryIdentity& LHS, const FPortableObjectEntryIdentity& RHS)
{
	return LHS.MsgCtxt == RHS.MsgCtxt && LHS.MsgId == RHS.MsgId && LHS.MsgIdPlural == RHS.MsgIdPlural;
}

uint32 GetTypeHash(const FPortableObjectEntryIdentity& ID)
{
	const uint32 HashA = HashCombine(GetTypeHash(ID.MsgCtxt), GetTypeHash(ID.MsgId));
	const uint32 HashB = GetTypeHash(ID.MsgIdPlural);
	return HashCombine(HashA, HashB);
}

namespace
{
	FString ConditionIdentityForPOMsgCtxt(const FString& Namespace, const FString& Key, const TSharedPtr<FLocMetadataObject>& KeyMetaData)
	{
		const auto& EscapeMsgCtxtParticle = [](const FString& InStr) -> FString
		{
			FString Result;
			for (const TCHAR C : InStr)
			{
				switch (C)
				{
				case TEXT(','):		Result += TEXT("\\,");	break;
				default:			Result += C;			break;
				}
			}
			return Result;
		};

		FString EscapedNamespace = EscapeMsgCtxtParticle(Namespace);
		FString EscapedKey = EscapeMsgCtxtParticle(Key);

		return KeyMetaData.IsValid() ? FString::Printf(TEXT("%s,%s"), *EscapedNamespace, *EscapedKey) : EscapedNamespace;
	}

	void ParsePOMsgCtxtForIdentity(const FString& MsgCtxt, FString& OutNamespace, FString& OutKey)
	{
		static const int32 OutputBufferCount = 2;
		FString* OutputBuffers[OutputBufferCount] = { &OutNamespace, &OutKey };
		int32 OutputBufferIndex = 0;

		FString EscapeSequenceBuffer;

		auto HandleEscapeSequenceBuffer = [&]()
		{
			// Insert unescaped sequence if needed.
			if (!EscapeSequenceBuffer.IsEmpty())
			{
				bool EscapeSequenceIdentified = true;

				// Identify escape sequence
				TCHAR UnescapedCharacter = 0;
				if (EscapeSequenceBuffer == TEXT("\\,"))
				{
					UnescapedCharacter = ',';
				}
				else
				{
					EscapeSequenceIdentified = false;
				}

				// If identified, append the processed sequence as the unescaped character.
				if (EscapeSequenceIdentified)
				{
					*OutputBuffers[OutputBufferIndex] += UnescapedCharacter;
				}
				// If it was not identified, preserve the escape sequence and append it.
				else
				{
					*OutputBuffers[OutputBufferIndex] += EscapeSequenceBuffer;
				}
				// Either way, we've appended something based on the buffer and it should be reset.
				EscapeSequenceBuffer.Empty();
			}
		};

		for (const TCHAR C : MsgCtxt)
		{
			// If we're out of buffers, break out. The particle list is longer than expected.
			if (OutputBufferIndex >= OutputBufferCount)
			{
				UE_LOG( LogInternationalizationExportCommandlet, Warning, TEXT("msgctxt found in PO has too many parts: %s"), *MsgCtxt );
				break;
			}

			// Not in an escape sequence.
			if (EscapeSequenceBuffer.IsEmpty())
			{
				// Comma marks the delimiter between namespace and key, if present.
				if(C == TEXT(','))
				{
					++OutputBufferIndex;
				}
				// Regular character, just copy over.
				else if (C != TEXT('\\'))
				{
					*OutputBuffers[OutputBufferIndex] += C;
				}
				// Start of an escape sequence, put in escape sequence buffer.
				else
				{
					EscapeSequenceBuffer += C;
				}
			}
			// If already in an escape sequence.
			else
			{
				// Append to escape sequence buffer.
				EscapeSequenceBuffer += C;

				HandleEscapeSequenceBuffer();
			}
		}
		// Catch any trailing backslashes.
		HandleEscapeSequenceBuffer();
	}

	FString ConditionArchiveStrForPo(const FString& InStr)
	{
	FString Result;
	for (const TCHAR C : InStr)
	{
		switch (C)
		{
		case TEXT('\\'):	Result += TEXT("\\\\");	break;
		case TEXT('"'):		Result += TEXT("\\\"");	break;
		case TEXT('\r'):	Result += TEXT("\\r");	break;
		case TEXT('\n'):	Result += TEXT("\\n");	break;
		case TEXT('\t'):	Result += TEXT("\\t");	break;
		default:			Result += C;			break;
		}
	}
	return Result;
	}

	FString ConditionPoStringForArchive(const FString& InStr)
	{
	FString Result;
	FString EscapeSequenceBuffer;

	auto HandleEscapeSequenceBuffer = [&]()
	{
		// Insert unescaped sequence if needed.
		if (!EscapeSequenceBuffer.IsEmpty())
		{
			bool EscapeSequenceIdentified = true;

			// Identify escape sequence
			TCHAR UnescapedCharacter = 0;
			if (EscapeSequenceBuffer == TEXT("\\\\"))
			{
				UnescapedCharacter = '\\';
			}
			else if (EscapeSequenceBuffer == TEXT("\\\""))
			{
				UnescapedCharacter = '"';
			}
			else if (EscapeSequenceBuffer == TEXT("\\r"))
			{
				UnescapedCharacter = '\r';
			}
			else if (EscapeSequenceBuffer == TEXT("\\n"))
			{
				UnescapedCharacter = '\n';
			}
			else if (EscapeSequenceBuffer == TEXT("\\t"))
			{
				UnescapedCharacter = '\t';
			}
			else
			{
				EscapeSequenceIdentified = false;
			}

			// If identified, append the processed sequence as the unescaped character.
			if (EscapeSequenceIdentified)
			{
				Result += UnescapedCharacter;
			}
			// If it was not identified, preserve the escape sequence and append it.
			else
			{
				Result += EscapeSequenceBuffer;
			}
			// Either way, we've appended something based on the buffer and it should be reset.
			EscapeSequenceBuffer.Empty();
		}
	};

	for (const TCHAR C : InStr)
	{
		// Not in an escape sequence.
		if (EscapeSequenceBuffer.IsEmpty())
		{
			// Regular character, just copy over.
			if (C != TEXT('\\'))
			{
				Result += C;
			}
			// Start of an escape sequence, put in escape sequence buffer.
			else
			{
				EscapeSequenceBuffer += C;
			}
		}
		// If already in an escape sequence.
		else
		{
			// Append to escape sequence buffer.
			EscapeSequenceBuffer += C;

			HandleEscapeSequenceBuffer();
		}
	}
	// Catch any trailing backslashes.
	HandleEscapeSequenceBuffer();
	return Result;
	}

	FString ConvertSrcLocationToPORef(const FString& InSrcLocation)
	{
	// Source location format: /Path1/Path2/file.cpp - line 123
	// PO Reference format: /Path1/Path2/file.cpp:123
	// @TODO: Note, we assume the source location format here but it could be arbitrary.
	return InSrcLocation.Replace(TEXT(" - line "), TEXT(":"));
	}

	FString GetConditionedKeyForExtractedComment(const FString& Key)
	{
		return FString::Printf(TEXT("Key:\t%s"), *Key);
	}

	FString GetConditionedReferenceForExtractedComment(const FString& PORefString)
	{
		return FString::Printf(TEXT("SourceLocation:\t%s"), *PORefString);
	}

	FString GetConditionedInfoMetaDataForExtractedComment(const FString& KeyName, const FString& ValueString)
	{
		return FString::Printf(TEXT("InfoMetaData:\t\"%s\" : \"%s\""), *KeyName, *ValueString);
	}
}

/**
*	UInternationalizationExportCommandlet
*/
bool UInternationalizationExportCommandlet::DoExport( const FString& SourcePath, const FString& DestinationPath, const FString& Filename )
{
	// Get native culture.
	FString NativeCultureName;
	if( !GetStringFromConfig( *SectionName, TEXT("NativeCulture"), NativeCultureName, ConfigPath ) )
	{
		UE_LOG( LogInternationalizationExportCommandlet, Error, TEXT("No native culture specified.") );
		return false;
	}

	// Get manifest name.
	FString ManifestName;
	if( !GetStringFromConfig( *SectionName, TEXT("ManifestName"), ManifestName, ConfigPath ) )
	{
		UE_LOG( LogInternationalizationExportCommandlet, Error, TEXT("No manifest name specified.") );
		return false;
	}

	// Get archive name.
	FString ArchiveName;
	if( !( GetStringFromConfig(* SectionName, TEXT("ArchiveName"), ArchiveName, ConfigPath ) ) )
	{
		UE_LOG(LogInternationalizationExportCommandlet, Error, TEXT("No archive name specified."));
		return false;
	}

	// Get culture directory setting, default to true if not specified (used to allow picking of export directory with windows file dialog from Translation Editor)
	bool bUseCultureDirectory = true;
	if (!(GetBoolFromConfig(*SectionName, TEXT("bUseCultureDirectory"), bUseCultureDirectory, ConfigPath)))
	{
		bUseCultureDirectory = true;
	}

	bool ShouldAddSourceLocationsAsComments = true;
	GetBoolFromConfig(*SectionName, TEXT("ShouldAddSourceLocationsAsComments"), ShouldAddSourceLocationsAsComments, ConfigPath);

	// Prepare the manifest
	TSharedRef<FInternationalizationManifest> InternationalizationManifest = MakeShareable(new FInternationalizationManifest());
	{
		const FString ManifestFileName = SourcePath / ManifestName;
		if (!FPaths::FileExists(ManifestFileName))
		{
			UE_LOG(LogInternationalizationExportCommandlet, Error, TEXT("Failed to find manifest '%s'."), *ManifestFileName);
			return false;
		}

		const TSharedPtr<FJsonObject> ManifestJsonObject = ReadJSONTextFile(ManifestFileName);
		if (!ManifestJsonObject.IsValid())
		{
			UE_LOG(LogInternationalizationExportCommandlet, Error, TEXT("Failed to parse manifest '%s'."), *ManifestFileName);
			return false;
		}

		FJsonInternationalizationManifestSerializer ManifestSerializer;
		InternationalizationManifest = MakeShareable(new FInternationalizationManifest());
		if (!ManifestSerializer.DeserializeManifest(ManifestJsonObject.ToSharedRef(), InternationalizationManifest))
		{
			UE_LOG(LogInternationalizationExportCommandlet, Error, TEXT("Failed to deserialize manifest '%s'."), *ManifestFileName);
			return false;
		}
	}

	// Load the native archive info
	TSharedRef<FInternationalizationArchive> NativeArchive = MakeShareable(new FInternationalizationArchive());
	{
		const FString NativeCulturePath = SourcePath / NativeCultureName;

		const FString NativeArchiveFileName = NativeCulturePath / ArchiveName;
		if (!FPaths::FileExists(NativeArchiveFileName))
		{
			UE_LOG(LogInternationalizationExportCommandlet, Error, TEXT("Failed to find archive '%s'."), *NativeArchiveFileName);
			return false;
		}

		const TSharedPtr<FJsonObject> ArchiveJsonObject = ReadJSONTextFile(NativeArchiveFileName);
		if (!ArchiveJsonObject.IsValid())
		{
			UE_LOG(LogInternationalizationExportCommandlet, Error, TEXT("Failed to read archive file '%s'."), *NativeArchiveFileName);
			return false;
		}

		FJsonInternationalizationArchiveSerializer ArchiveSerializer;
		if (!ArchiveSerializer.DeserializeArchive(ArchiveJsonObject.ToSharedRef(), NativeArchive))
		{
			UE_LOG(LogInternationalizationExportCommandlet, Error, TEXT("Failed to deserialize archive '%s'."), *NativeArchiveFileName);
			return false;
		}
	}

	// Process the desired cultures
	for(int32 Culture = 0; Culture < CulturesToGenerate.Num(); Culture++)
	{
		// Load the archive
		const FString CultureName = CulturesToGenerate[Culture];
		const FString CulturePath = SourcePath / CultureName;

		FString ArchiveFileName = CulturePath / ArchiveName;
		if( FPaths::FileExists(ArchiveFileName) )
		{
			TSharedPtr< FJsonObject > ArchiveJsonObject = ReadJSONTextFile(ArchiveFileName);

			FJsonInternationalizationArchiveSerializer ArchiveSerializer;
			TSharedRef< FInternationalizationArchive > InternationalizationArchive = MakeShareable( new FInternationalizationArchive );
			ArchiveSerializer.DeserializeArchive( ArchiveJsonObject.ToSharedRef(), InternationalizationArchive );

			{
				FPortableObjectFormatDOM NewPortableObject;

				FString LocLang;
				if( !NewPortableObject.SetLanguage( CultureName ) )
				{
					UE_LOG( LogInternationalizationExportCommandlet, Error, TEXT("Skipping export of loc language %s because it is not recognized."), *LocLang );
					continue;
				}

				NewPortableObject.SetProjectName( FPaths::GetBaseFilename( ManifestName ) );
				NewPortableObject.CreateNewHeader();

				{
					for(TManifestEntryBySourceTextContainer::TConstIterator ManifestIter = InternationalizationManifest->GetEntriesBySourceTextIterator(); ManifestIter; ++ManifestIter)
					{
						// Gather relevant info from manifest entry.
						const TSharedRef<FManifestEntry>& ManifestEntry = ManifestIter.Value();
						const FString& Namespace = ManifestEntry->Namespace;
						const FLocItem& Source = ManifestEntry->Source;

						// For each context, we may need to create a different or even multiple PO entries.
						for( auto ContextIter = ManifestEntry->Contexts.CreateConstIterator(); ContextIter; ++ContextIter )
						{
							const FManifestContext& Context = *ContextIter;

							bool bHasAddedPOEntry = false;

							// If we're exporting for something other than the native culture, we'll need to create PO entries for archive entries based on the native archive's translation.
							if (CultureName != NativeCultureName)
							{
								// Find the native archive entry which matches the exact same namespace, source, and key metadata, if it exists.
								TSharedPtr<FArchiveEntry> NativeArchiveEntry = NativeArchive->FindEntryBySource( Namespace, Source, Context.KeyMetadataObj );
								if (NativeArchiveEntry.IsValid())
								{
									// Only need to create this PO entry if the native archive entry's translation differs from its source, in which case we need to find the our translation of the native translation.
									if (!NativeArchiveEntry->Source.IsExactMatch(NativeArchiveEntry->Translation))
									{
										const TSharedPtr<FArchiveEntry> ArchiveEntry = InternationalizationArchive->FindEntryBySource( Namespace, NativeArchiveEntry->Translation, NativeArchiveEntry->KeyMetadataObj );
										if (ArchiveEntry.IsValid())
										{
											const FString ConditionedArchiveSource = ConditionArchiveStrForPo(ArchiveEntry->Source.Text);
											const FString ConditionedArchiveTranslation = ConditionArchiveStrForPo(ArchiveEntry->Translation.Text);

											TSharedRef<FPortableObjectEntry> PoEntry = MakeShareable( new FPortableObjectEntry );
											//@TODO: We support additional metadata entries that can be translated.  How do those fit in the PO file format?  Ex: isMature
											PoEntry->MsgId = ConditionedArchiveSource;
											PoEntry->MsgCtxt = ConditionIdentityForPOMsgCtxt(Namespace, Context.Key, Context.KeyMetadataObj);
											PoEntry->MsgStr.Add( ConditionedArchiveTranslation );

											const FString PORefString = ConvertSrcLocationToPORef( Context.SourceLocation );
											PoEntry->AddReference( PORefString ); // Source location.

											PoEntry->AddExtractedComment( FString::Printf(TEXT("Key:\t%s"), *Context.Key) ); // "Notes from Programmer" in the form of the Key.
											PoEntry->AddExtractedComment( FString::Printf(TEXT("SourceLocation:\t%s"), *PORefString) ); // "Notes from Programmer" in the form of the Source Location, since this comes in handy too and OneSky doesn't properly show references, only comments.
											TArray<FString> InfoMetaDataStrings;
											if (Context.InfoMetadataObj.IsValid())
											{
												for (auto InfoMetaDataPair : Context.InfoMetadataObj->Values)
												{
													const FString KeyName = InfoMetaDataPair.Key;
													const TSharedPtr<FLocMetadataValue> Value = InfoMetaDataPair.Value;
													InfoMetaDataStrings.Add(FString::Printf(TEXT("InfoMetaData:\t\"%s\" : \"%s\""), *KeyName, *Value->ToString()));
												}
											}
											if (InfoMetaDataStrings.Num())
											{
												PoEntry->AddExtractedComments(InfoMetaDataStrings);
											}

											bHasAddedPOEntry = true;
											NewPortableObject.AddEntry( PoEntry );
										}
									}
								}
							}

							// Create the typical PO entry from the archive entry which matches the exact same namespace, source, and key metadata, if it exists.
							if (!bHasAddedPOEntry)
							{
								const TSharedPtr<FArchiveEntry> ArchiveEntry = InternationalizationArchive->FindEntryBySource( Namespace, Source, Context.KeyMetadataObj );
								if( ArchiveEntry.IsValid() )
								{
									const FString ConditionedArchiveSource = ConditionArchiveStrForPo(ArchiveEntry->Source.Text);
									const FString ConditionedArchiveTranslation = ConditionArchiveStrForPo(ArchiveEntry->Translation.Text);

									TSharedRef<FPortableObjectEntry> PoEntry = MakeShareable( new FPortableObjectEntry );
									//@TODO: We support additional metadata entries that can be translated.  How do those fit in the PO file format?  Ex: isMature
									PoEntry->MsgId = ConditionedArchiveSource;
									PoEntry->MsgCtxt = ConditionIdentityForPOMsgCtxt(Namespace, Context.Key, Context.KeyMetadataObj);
									PoEntry->MsgStr.Add( ConditionedArchiveTranslation );

									const FString PORefString = ConvertSrcLocationToPORef( Context.SourceLocation );
									PoEntry->AddReference(PORefString); // Source location.

									PoEntry->AddExtractedComment( GetConditionedKeyForExtractedComment(Context.Key) ); // "Notes from Programmer" in the form of the Key.

									if (ShouldAddSourceLocationsAsComments)
									{
										PoEntry->AddExtractedComment(GetConditionedReferenceForExtractedComment(PORefString)); // "Notes from Programmer" in the form of the Source Location, since this comes in handy too and OneSky doesn't properly show references, only comments.
									}

									TArray<FString> InfoMetaDataStrings;
									if (Context.InfoMetadataObj.IsValid())
									{
										for (auto InfoMetaDataPair : Context.InfoMetadataObj->Values)
										{
											const FString KeyName = InfoMetaDataPair.Key;
											const TSharedPtr<FLocMetadataValue> Value = InfoMetaDataPair.Value;
											InfoMetaDataStrings.Add(GetConditionedInfoMetaDataForExtractedComment(KeyName, Value->ToString()));
										}
									}
									if (InfoMetaDataStrings.Num())
									{
										PoEntry->AddExtractedComments(InfoMetaDataStrings);
									}

									bHasAddedPOEntry = true;
									NewPortableObject.AddEntry( PoEntry );
								}
							}
						}
					}
				}

				// Write out the Portable Object to .po file.
				{
					FString OutputFileName;
					if (bUseCultureDirectory)
					{
						OutputFileName = DestinationPath / CultureName / Filename;
					}
					else
					{
						OutputFileName = DestinationPath / Filename;
					}

					// Persist comments if requested.
					if (ShouldPersistComments)
					{
						// Preserve comments from the specified file now, if they haven't already been.
						if (!HasPreservedComments)
						{
							FPortableObjectFormatDOM ExistingPortableObject;
							const bool HasLoadedPOFile = LoadPOFile(OutputFileName, ExistingPortableObject);
							if (!HasLoadedPOFile)
							{
								return false;
							}

							PreserveExtractedCommentsForPersistence(ExistingPortableObject);
						}

						// Persist the comments into the new portable object we're going to be saving.
						for (const auto& Pair : POEntryToCommentMap)
						{
							const TSharedPtr<FPortableObjectEntry> FoundEntry = NewPortableObject.FindEntry(Pair.Key.MsgId, Pair.Key.MsgIdPlural, Pair.Key.MsgCtxt);
							if (FoundEntry.IsValid())
							{
								FoundEntry->AddExtractedComments(Pair.Value);
							}
						}
					}

					NewPortableObject.SortEntries();

					const bool DidFileExist = FPaths::FileExists(OutputFileName);
					if (DidFileExist)
					{
						if( SourceControlInfo.IsValid() )
						{
							FText SCCErrorText;
							if (!SourceControlInfo->CheckOutFile(OutputFileName, SCCErrorText))
							{
								UE_LOG(LogInternationalizationExportCommandlet, Error, TEXT("Check out of file %s failed: %s"), *OutputFileName, *SCCErrorText.ToString());
								return false;
							}
						}
					}

					//@TODO We force UTF8 at the moment but we want this to be based on the format found in the header info.
					const FString OutputString = NewPortableObject.ToString();
					if (!FFileHelper::SaveStringToFile(OutputString, *OutputFileName, FFileHelper::EEncodingOptions::ForceUTF8))
					{
						UE_LOG( LogInternationalizationExportCommandlet, Error, TEXT("Could not write file %s"), *OutputFileName );
						return false;
					}

					if (!DidFileExist)
					{
						// Checkout on a new file will cause it to be added
						if( SourceControlInfo.IsValid() )
						{
							FText SCCErrorText;
							if (!SourceControlInfo->CheckOutFile(OutputFileName, SCCErrorText))
							{
								UE_LOG(LogInternationalizationExportCommandlet, Error, TEXT("Check out of file %s failed: %s"), *OutputFileName, *SCCErrorText.ToString());
								return false;
							}
						}
					}
				}
			}
		}
	}
	return true;
}

bool UInternationalizationExportCommandlet::DoImport(const FString& SourcePath, const FString& DestinationPath, const FString& Filename)
{
	// Get manifest name.
	FString ManifestName;
	if( !GetStringFromConfig( *SectionName, TEXT("ManifestName"), ManifestName, ConfigPath ) )
	{
		UE_LOG( LogInternationalizationExportCommandlet, Error, TEXT("No manifest name specified.") );
		return false;
	}

	// Get archive name.
	FString ArchiveName;
	if( !( GetStringFromConfig(* SectionName, TEXT("ArchiveName"), ArchiveName, ConfigPath ) ) )
	{
		UE_LOG(LogInternationalizationExportCommandlet, Error, TEXT("No archive name specified."));
		return false;
	}

	// Get culture directory setting, default to true if not specified (used to allow picking of import directory with file open dialog from Translation Editor)
	bool bUseCultureDirectory = true;
	if (!(GetBoolFromConfig(*SectionName, TEXT("bUseCultureDirectory"), bUseCultureDirectory, ConfigPath)))
	{
		bUseCultureDirectory = true;
	}

	// Process the desired cultures
	for(int32 Culture = 0; Culture < CulturesToGenerate.Num(); Culture++)
	{
		// Load the Portable Object file if found
		const FString CultureName = CulturesToGenerate[Culture];
		FString POFilePath = "";
		if (bUseCultureDirectory)
		{
			POFilePath = SourcePath / CultureName / Filename;
		}
		else
		{
			POFilePath = SourcePath / Filename;
		}

		FPortableObjectFormatDOM PortableObject;
		const bool HasLoadedPOFile = LoadPOFile(POFilePath, PortableObject);
		if (!HasLoadedPOFile)
		{
			continue;
		}

		if (ShouldPersistComments)
		{
			PreserveExtractedCommentsForPersistence(PortableObject);
		}

		if (PortableObject.GetProjectName() != ManifestName.Replace(TEXT(".manifest"), TEXT("")))
		{
			UE_LOG(LogInternationalizationExportCommandlet, Log, TEXT("The project name (%s) in the file (%s) did not match the target manifest project (%s)."), *POFilePath, *PortableObject.GetProjectName(), *ManifestName.Replace(TEXT(".manifest"), TEXT("")));
		}

		const FString ManifestFileName = DestinationPath / ManifestName;

		TSharedPtr< FJsonObject > ManifestJsonObject = NULL;
		ManifestJsonObject = ReadJSONTextFile( ManifestFileName );

		FJsonInternationalizationManifestSerializer ManifestSerializer;
		TSharedRef< FInternationalizationManifest > InternationalizationManifest = MakeShareable( new FInternationalizationManifest );
		ManifestSerializer.DeserializeManifest( ManifestJsonObject.ToSharedRef(), InternationalizationManifest );

		if( !FPaths::FileExists(ManifestFileName) )
		{
			UE_LOG( LogInternationalizationExportCommandlet, Error, TEXT("Failed to find manifest %s."), *ManifestFileName);
			continue;
		}

		const FString DestinationCulturePath = DestinationPath / CultureName;
		const FString ArchiveFileName = DestinationCulturePath / ArchiveName;

		if( !FPaths::FileExists(ArchiveFileName) )
		{
			UE_LOG( LogInternationalizationExportCommandlet, Error, TEXT("Failed to find destination archive %s."), *ArchiveFileName);
			continue;
		}

		TSharedPtr< FJsonObject > ArchiveJsonObject = ReadJSONTextFile(ArchiveFileName);

		FJsonInternationalizationArchiveSerializer ArchiveSerializer;
		TSharedRef< FInternationalizationArchive > InternationalizationArchive = MakeShareable( new FInternationalizationArchive );
		ArchiveSerializer.DeserializeArchive( ArchiveJsonObject.ToSharedRef(), InternationalizationArchive );

		bool bModifiedArchive = false;
		{
			for( auto EntryIter = PortableObject.GetEntriesIterator(); EntryIter; ++EntryIter )
			{
				auto POEntry = *EntryIter;
				if( POEntry->MsgId.IsEmpty() || POEntry->MsgStr.Num() == 0 || POEntry->MsgStr[0].Trim().IsEmpty() )
				{
					// We ignore the header entry or entries with no translation.
					continue;
				}

				// Some warning messages for data we don't process at the moment
				if( !POEntry->MsgIdPlural.IsEmpty() || POEntry->MsgStr.Num() > 1 )
				{
					UE_LOG( LogInternationalizationExportCommandlet, Error, TEXT("Portable Object entry has plural form we did not process.  File: %s  MsgCtxt: %s  MsgId: %s"), *POFilePath, *POEntry->MsgCtxt, *POEntry->MsgId );
				}

				FString Key;
				FString Namespace;
				ParsePOMsgCtxtForIdentity(POEntry->MsgCtxt, Namespace, Key);
				const FString& SourceText = ConditionPoStringForArchive(POEntry->MsgId);
				const FString& Translation = ConditionPoStringForArchive(POEntry->MsgStr[0]);

				TSharedPtr<FLocMetadataObject> KeyMetaDataObject;
				// Get key metadata from the manifest, using the namespace and key.
				if (!Key.IsEmpty())
				{
					// Find manifest entry by namespace
					for (auto ManifestEntryIterator = InternationalizationManifest->GetEntriesByContextIdIterator(); ManifestEntryIterator; ++ManifestEntryIterator)
					{
						const FString& ManifestEntryNamespace = ManifestEntryIterator->Key;
						const TSharedRef<FManifestEntry>& ManifestEntry = ManifestEntryIterator->Value;
						if (ManifestEntry->Namespace == Namespace)
						{
							FManifestContext* const MatchingContext = ManifestEntry->Contexts.FindByPredicate([&](FManifestContext& Context) -> bool
								{
									return Context.Key == Key;
								});
							if (MatchingContext)
							{
								KeyMetaDataObject = MatchingContext->KeyMetadataObj;
							}
						}
					}
				}

				//@TODO: Take into account optional entries and entries that differ by keymetadata.  Ex. Each optional entry needs a unique msgCtxt
				const TSharedPtr< FArchiveEntry > FoundEntry = InternationalizationArchive->FindEntryBySource( Namespace, SourceText, KeyMetaDataObject );
				if( !FoundEntry.IsValid() )
				{
					UE_LOG(LogInternationalizationExportCommandlet, Log, TEXT("Could not find corresponding archive entry for PO entry.  File: %s  MsgCtxt: %s  MsgId: %s"), *POFilePath, *POEntry->MsgCtxt, *POEntry->MsgId );
					continue;
				}

				if( FoundEntry->Translation != Translation )
				{
					FoundEntry->Translation = Translation;
					bModifiedArchive = true;
				}
			}
		}

		if( bModifiedArchive )
		{
			TSharedRef<FJsonObject> FinalArchiveJsonObj = MakeShareable( new FJsonObject );
			ArchiveSerializer.SerializeArchive( InternationalizationArchive, FinalArchiveJsonObj );

			if( !WriteJSONToTextFile(FinalArchiveJsonObj, ArchiveFileName, SourceControlInfo ) )
			{
				UE_LOG( LogInternationalizationExportCommandlet, Error, TEXT("Failed to write archive to %s."), *ArchiveFileName );				
				return false;
			}
		}
	}

	return true;
}

int32 UInternationalizationExportCommandlet::Main( const FString& Params )
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamVals;


	UCommandlet::ParseCommandLine(*Params, Tokens, Switches, ParamVals);

	const FString* ParamVal = ParamVals.Find(FString(TEXT("Config")));

	if ( ParamVal )
	{
		ConfigPath = *ParamVal;
	}
	else
	{
		UE_LOG(LogInternationalizationExportCommandlet, Error, TEXT("No config specified."));
		return -1;
	}

	//Set config section
	ParamVal = ParamVals.Find(FString(TEXT("Section")));


	if ( ParamVal )
	{
		SectionName = *ParamVal;
	}
	else
	{
		UE_LOG(LogInternationalizationExportCommandlet, Error, TEXT("No config section specified."));
		return -1;
	}


	FString SourcePath; // Source path to the root folder that manifest/archive files live in.
	if( !GetPathFromConfig( *SectionName, TEXT("SourcePath"), SourcePath, ConfigPath ) )
	{
		UE_LOG( LogInternationalizationExportCommandlet, Error, TEXT("No source path specified.") );
		return -1;
	}

	FString DestinationPath; // Destination path that we will write files to.
	if( !GetPathFromConfig( *SectionName, TEXT("DestinationPath"), DestinationPath, ConfigPath ) )
	{
		UE_LOG( LogInternationalizationExportCommandlet, Error, TEXT("No destination path specified.") );
		return -1;
	}

	FString Filename; // Name of the file to read or write from
	if (!GetStringFromConfig(*SectionName, TEXT("PortableObjectName"), Filename, ConfigPath))
	{
		UE_LOG(LogInternationalizationExportCommandlet, Error, TEXT("No portable object name specified."));
		return -1;
	}

	// Get cultures to generate.
	if( GetStringArrayFromConfig(*SectionName, TEXT("CulturesToGenerate"), CulturesToGenerate, ConfigPath) == 0 )
	{
		UE_LOG(LogInternationalizationExportCommandlet, Error, TEXT("No cultures specified for generation."));
		return -1;
	}


	bool bDoExport = false;
	bool bDoImport = false;
	ShouldPersistComments = false;

	GetBoolFromConfig( *SectionName, TEXT("bImportLoc"), bDoImport, ConfigPath );
	GetBoolFromConfig( *SectionName, TEXT("bExportLoc"), bDoExport, ConfigPath );
	GetBoolFromConfig(*SectionName, TEXT("ShouldPersistCommentsOnExport"), ShouldPersistComments, ConfigPath);

	// Reject the ShouldPersistComments flag and warn if not exporting - we're not writing to anything, so we can't persist.
	if (ShouldPersistComments && !bDoExport)
	{
		UE_LOG(LogInternationalizationExportCommandlet, Warning, TEXT("ShouldPersistComments is true, but bExportLoc is false - can't persist comments if not writing PO files."));
		ShouldPersistComments = false;
	}

	if( !bDoImport && !bDoExport )
	{
		UE_LOG(LogInternationalizationExportCommandlet, Error, TEXT("Import/Export operation not detected.  Use bExportLoc or bImportLoc in config section."));
		return -1;
	}

	if( bDoImport )
	{
		if (!DoImport(SourcePath, DestinationPath, Filename))
		{
			UE_LOG(LogInternationalizationExportCommandlet, Error, TEXT("Failed to import localization files."));
			return -1;
		}
	}

	if( bDoExport )
	{
		if (!DoExport(SourcePath, DestinationPath, Filename))
		{
			UE_LOG(LogInternationalizationExportCommandlet, Error, TEXT("Failed to export localization files."));
			return -1;
		}
	}

	return 0;
}

bool UInternationalizationExportCommandlet::LoadPOFile(const FString& POFilePath, FPortableObjectFormatDOM& OutPortableObject)
{
	if (!FPaths::FileExists(POFilePath))
	{
		UE_LOG(LogInternationalizationExportCommandlet, Warning, TEXT("Could not find file %s"), *POFilePath);
		return false;
	}

	FString POFileContents;
	if (!FFileHelper::LoadFileToString(POFileContents, *POFilePath))
	{
		UE_LOG(LogInternationalizationExportCommandlet, Error, TEXT("Failed to load file %s."), *POFilePath);
		return false;
	}

	if (!OutPortableObject.FromString(POFileContents))
	{
		UE_LOG(LogInternationalizationExportCommandlet, Error, TEXT("Failed to parse Portable Object file %s."), *POFilePath);
		return false;
	}

	return true;
}

void UInternationalizationExportCommandlet::PreserveExtractedCommentsForPersistence(FPortableObjectFormatDOM& PortableObject)
{
	// Preserve comments for later.
	for (auto EntriesIterator = PortableObject.GetEntriesIterator(); EntriesIterator; ++EntriesIterator)
	{
		const TSharedPtr< FPortableObjectEntry >& Entry = *EntriesIterator;

		// Preserve only non-procedurally generated extracted comments.
		const TArray<FString> CommentsToPreserve = Entry->ExtractedComments.FilterByPredicate([=](const FString& ExtractedComment) -> bool
		{
			return !ExtractedComment.StartsWith("Key:") && !ExtractedComment.StartsWith("SourceLocation:") && !ExtractedComment.StartsWith("InfoMetaData:");
		});

		if (CommentsToPreserve.Num())
		{
			POEntryToCommentMap.Add(FPortableObjectEntryIdentity{ Entry->MsgCtxt, Entry->MsgId, Entry->MsgIdPlural }, CommentsToPreserve);
		}
	}
	HasPreservedComments = true;
}