// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.
// .

#include "ShaderCompilerCommon.h"
#include "ModuleManager.h"
#include "CrossCompilerCommon.h"
#include "TypeHash.h"


IMPLEMENT_MODULE(FDefaultModuleImpl, ShaderCompilerCommon);

/**
 * The shader frequency.
 */
enum EHlslShaderFrequency
{
	HSF_VertexShader,
	HSF_PixelShader,
	HSF_GeometryShader,
	HSF_HullShader,
	HSF_DomainShader,
	HSF_ComputeShader,
	HSF_FrequencyCount,
	HSF_InvalidFrequency = -1
};

/**
 * Compilation flags. See PackUniformBuffers.h for details on Grouping/Packing uniforms.
 */
enum EHlslCompileFlag
{
	/** Disables validation of the IR. */
	HLSLCC_NoValidation = 0x1,
	/** Disabled preprocessing. */
	HLSLCC_NoPreprocess = 0x2,
	/** Pack uniforms into typed arrays. */
	HLSLCC_PackUniforms = 0x4,
	/** Assume that input shaders output into DX11 clip space,
	 * and adjust them for OpenGL clip space. */
	HLSLCC_DX11ClipSpace = 0x8,
	/** Print AST for debug purposes. */
	HLSLCC_PrintAST = 0x10,
	// Removed any structures embedded on uniform buffers flattens them into elements of the uniform buffer (Mostly for ES 2: this implies PackUniforms).
	HLSLCC_FlattenUniformBufferStructures = 0x20 | HLSLCC_PackUniforms,
	// Removes uniform buffers and flattens them into globals (Mostly for ES 2: this implies PackUniforms & Flatten Structures).
	HLSLCC_FlattenUniformBuffers = 0x40 | HLSLCC_PackUniforms | HLSLCC_FlattenUniformBufferStructures,
	// Groups flattened uniform buffers per uniform buffer source/precision (Implies Flatten UBs)
	HLSLCC_GroupFlattenedUniformBuffers = 0x80 | HLSLCC_FlattenUniformBuffers,
	// Remove redundant subexpressions [including texture fetches] (to workaround certain drivers who can't optimize redundant texture fetches)
	HLSLCC_ApplyCommonSubexpressionElimination = 0x100,
	// Expand subexpressions/obfuscate (to workaround certain drivers who can't deal with long nested expressions)
	HLSLCC_ExpandSubexpressions = 0x200,
	// Generate shaders compatible with the separate_shader_objects extension
	HLSLCC_SeparateShaderObjects = 0x400,
	// Finds variables being used as atomics and changes all references to use atomic reads/writes
	HLSLCC_FixAtomicReferences = 0x800,
	// Packs global uniforms & flattens structures, and makes each packed array its own uniform buffer
	HLSLCC_PackUniformsIntoUniformBuffers = 0x1000 | HLSLCC_PackUniforms,
	// Set default precision to highp in a pixel shader (default is mediump on ES2 platforms)
	HLSLCC_UseFullPrecisionInPS = 0x2000,
};

int16 GetNumUniformBuffersUsed(const FShaderCompilerResourceTable& InSRT)
{
	auto CountLambda = [&](const TArray<uint32>& In)
					{
						int16 LastIndex = -1;
						for (int32 i = 0; i < In.Num(); ++i)
						{
							auto BufferIndex = FRHIResourceTableEntry::GetUniformBufferIndex(In[i]);
							if (BufferIndex != static_cast<uint16>(FRHIResourceTableEntry::GetEndOfStreamToken()) )
							{
								LastIndex = FMath::Max(LastIndex, (int16)BufferIndex);
							}
						}

						return LastIndex + 1;
					};
	int16 Num = CountLambda(InSRT.SamplerMap);
	Num = FMath::Max(Num, (int16)CountLambda(InSRT.ShaderResourceViewMap));
	Num = FMath::Max(Num, (int16)CountLambda(InSRT.TextureMap));
	Num = FMath::Max(Num, (int16)CountLambda(InSRT.UnorderedAccessViewMap));
	return Num;
}


void BuildResourceTableTokenStream(const TArray<uint32>& InResourceMap, int32 MaxBoundResourceTable, TArray<uint32>& OutTokenStream)
{
	// First we sort the resource map.
	TArray<uint32> SortedResourceMap = InResourceMap;
	SortedResourceMap.Sort();

	// The token stream begins with a table that contains offsets per bound uniform buffer.
	// This offset provides the start of the token stream.
	OutTokenStream.AddZeroed(MaxBoundResourceTable+1);
	auto LastBufferIndex = FRHIResourceTableEntry::GetEndOfStreamToken();
	for (int32 i = 0; i < SortedResourceMap.Num(); ++i)
	{
		auto BufferIndex = FRHIResourceTableEntry::GetUniformBufferIndex(SortedResourceMap[i]);
		if (BufferIndex != LastBufferIndex)
		{
			// Store the offset for resources from this buffer.
			OutTokenStream[BufferIndex] = OutTokenStream.Num();
			LastBufferIndex = BufferIndex;
		}
		OutTokenStream.Add(SortedResourceMap[i]);
	}

	// Add a token to mark the end of the stream. Not needed if there are no bound resources.
	if (OutTokenStream.Num())
	{
		OutTokenStream.Add(FRHIResourceTableEntry::GetEndOfStreamToken());
	}
}


void BuildResourceTableMapping(
	const TMap<FString,FResourceTableEntry>& ResourceTableMap,
	const TMap<FString,uint32>& ResourceTableLayoutHashes,
	TBitArray<>& UsedUniformBufferSlots,
	FShaderParameterMap& ParameterMap,
	FShaderCompilerResourceTable& OutSRT)
{
	check(OutSRT.ResourceTableBits == 0);
	check(OutSRT.ResourceTableLayoutHashes.Num() == 0);

	// Build resource table mapping
	int32 MaxBoundResourceTable = -1;
	TArray<uint32> ResourceTableSRVs;
	TArray<uint32> ResourceTableSamplerStates;
	TArray<uint32> ResourceTableUAVs;

	for( auto MapIt = ResourceTableMap.CreateConstIterator(); MapIt; ++MapIt )
	{
		const FString& Name	= MapIt->Key;
		const FResourceTableEntry& Entry = MapIt->Value;

		uint16 BufferIndex, BaseIndex, Size;
		if (ParameterMap.FindParameterAllocation( *Name, BufferIndex, BaseIndex, Size ) )
		{
			ParameterMap.RemoveParameterAllocation(*Name);

			uint16 UniformBufferIndex = INDEX_NONE, UBBaseIndex, UBSize;
			if (ParameterMap.FindParameterAllocation(*Entry.UniformBufferName, UniformBufferIndex, UBBaseIndex, UBSize) == false)
			{
				UniformBufferIndex = UsedUniformBufferSlots.FindAndSetFirstZeroBit();
				ParameterMap.AddParameterAllocation(*Entry.UniformBufferName,UniformBufferIndex,0,0);
			}

			OutSRT.ResourceTableBits |= (1 << UniformBufferIndex);
			MaxBoundResourceTable = FMath::Max<int32>(MaxBoundResourceTable, (int32)UniformBufferIndex);

			while (OutSRT.ResourceTableLayoutHashes.Num() <= MaxBoundResourceTable)
			{
				OutSRT.ResourceTableLayoutHashes.Add(0);
			}
			OutSRT.ResourceTableLayoutHashes[UniformBufferIndex] = ResourceTableLayoutHashes.FindChecked(Entry.UniformBufferName);

			auto ResourceMap = FRHIResourceTableEntry::Create(UniformBufferIndex, Entry.ResourceIndex, BaseIndex);
			switch( Entry.Type )
			{
			case UBMT_TEXTURE:
				OutSRT.TextureMap.Add(ResourceMap);
				break;
			case UBMT_SAMPLER:
				OutSRT.SamplerMap.Add(ResourceMap);
				break;
			case UBMT_SRV:
				OutSRT.ShaderResourceViewMap.Add(ResourceMap);
				break;
			case UBMT_UAV:
				OutSRT.UnorderedAccessViewMap.Add(ResourceMap);
				break;
			default:
				check(0);
			}
		}
	}

	OutSRT.MaxBoundResourceTable = MaxBoundResourceTable;
}

// Specialized version of FString::ReplaceInline that checks that the search word is not inside a #line directive
static void WholeWordReplaceInline(FString& String, TCHAR* StartPtr, const TCHAR* SearchText, const TCHAR* ReplacementText)
{
	if (String.Len() > 0
		&&	SearchText != nullptr && *SearchText != 0
		&&	ReplacementText != nullptr && FCString::Strcmp(SearchText, ReplacementText) != 0)
	{
		const int32 NumCharsToReplace = FCString::Strlen(SearchText);
		const int32 NumCharsToInsert = FCString::Strlen(ReplacementText);

		check(NumCharsToInsert == NumCharsToReplace);
		check(*StartPtr);
		TCHAR* Pos = FCString::Strstr(StartPtr, SearchText);
		while (Pos != nullptr)
		{
			// Find a " character, indicating we might be inside a #line directive
			TCHAR* FoundQuote = nullptr;
			auto* ValidatePos = Pos;
			do
			{
				--ValidatePos;
				if (*ValidatePos == '\"')
				{
					FoundQuote = ValidatePos;
					break;
				}
			}
			while (ValidatePos >= StartPtr && *ValidatePos != '\n');

			bool bReplace = true;
			if (FoundQuote)
			{
				// Validate that we're indeed inside a #line directive by first finding the last \n character
				TCHAR* FoundEOL = nullptr;
				do 
				{
					--ValidatePos;
					if (*ValidatePos == '\n')
					{
						FoundEOL = ValidatePos;
						break;
					}
				}
				while (ValidatePos > StartPtr);

				// Finally make sure the directive is between the \n and the and the quote
				if (FoundEOL)
				{
					auto* FoundInclude = FCString::Strstr(FoundEOL + 1, TEXT("#line"));
					if (FoundInclude && FoundInclude < FoundQuote)
					{
						bReplace = false;
					}
				}
			}
			
			// Make sure this is not part of an identifier
			if (bReplace && Pos > StartPtr)
			{
				const auto Char = Pos[-1];
				if ((Char >= 'a' && Char <= 'z') ||
					(Char >= 'A' && Char <= 'Z') ||
					(Char >= '0' && Char <= '9') ||
					Char == '_')
				{
					bReplace = false;
				}
			}

			if (bReplace)
			{
				// FCString::Strcpy inserts a terminating zero so can't use that
				for (int32 i = 0; i < NumCharsToInsert; i++)
				{
					Pos[i] = ReplacementText[i];
				}
			}

			if (Pos + NumCharsToReplace - *String < String.Len())
			{
				Pos = FCString::Strstr(Pos + NumCharsToReplace, SearchText);
			}
			else
			{
				break;
			}
		}
	}
}


bool RemoveUniformBuffersFromSource(FString& SourceCode)
{
	static const FString StaticStructToken(TEXT("static const struct"));
	int32 StaticStructTokenPos = SourceCode.Find(StaticStructToken, ESearchCase::CaseSensitive, ESearchDir::FromStart);
	while (StaticStructTokenPos != INDEX_NONE)
	{
		static const FString CloseBraceSpaceToken(TEXT("} "));
		int32 CloseBraceSpaceTokenPos = SourceCode.Find(CloseBraceSpaceToken, ESearchCase::CaseSensitive, ESearchDir::FromStart, StaticStructTokenPos + StaticStructToken.Len());
		if (CloseBraceSpaceTokenPos == INDEX_NONE)
		{
			check(0);	//@todo-rco: ERROR
			return false;
		}

		int32 NamePos = CloseBraceSpaceTokenPos + CloseBraceSpaceToken.Len();
		static const FString SpaceEqualsToken(TEXT(" ="));
		int32 SpaceEqualsTokenPos = SourceCode.Find(SpaceEqualsToken, ESearchCase::CaseSensitive, ESearchDir::FromStart, NamePos);
		if (SpaceEqualsTokenPos == INDEX_NONE)
		{
			check(0);	//@todo-rco: ERROR
			return false;
		}

		FString UniformBufferName = SourceCode.Mid(NamePos, SpaceEqualsTokenPos - NamePos);
		check(UniformBufferName.Len() > 0);

		static const FString CloseBraceSemicolorToken(TEXT("};"));
		int32 CloseBraceSemicolonTokenPos = SourceCode.Find(CloseBraceSemicolorToken, ESearchCase::CaseSensitive, ESearchDir::FromStart, SpaceEqualsTokenPos + SpaceEqualsToken.Len());
		if (CloseBraceSemicolonTokenPos == INDEX_NONE)
		{
			check(0);	//@todo-rco: ERROR
			return false;
		}

		// Comment out this UB
		auto& SourceCharArray = SourceCode.GetCharArray();
		SourceCharArray[StaticStructTokenPos] = TCHAR('/');
		SourceCharArray[StaticStructTokenPos + 1] = TCHAR('*');
		SourceCharArray[CloseBraceSemicolonTokenPos] = TCHAR('*');
		SourceCharArray[CloseBraceSemicolonTokenPos + 1] = TCHAR('/');

		// Find & Replace this UB
		FString UBSource = UniformBufferName + FString(TEXT("."));
		FString UBDest = UniformBufferName + FString(TEXT("_"));
		WholeWordReplaceInline(SourceCode, &SourceCharArray[CloseBraceSemicolonTokenPos + 2], *UBSource, *UBDest);

		// Find next UB
		StaticStructTokenPos = SourceCode.Find(StaticStructToken, ESearchCase::CaseSensitive, ESearchDir::FromStart, CloseBraceSemicolonTokenPos + 2);
	}

	return true;
}


FString CreateShaderCompilerWorkerDirectCommandLine(const FShaderCompilerInput& Input)
{
	FString Text(TEXT("-directcompile -format="));
	Text += Input.ShaderFormat.GetPlainNameString();
	Text += TEXT(" -entry=");
	Text += Input.EntryPointName;
	switch (Input.Target.Frequency)
	{
	case SF_Vertex:		Text += TEXT(" -vs"); break;
	case SF_Hull:		Text += TEXT(" -hs"); break;
	case SF_Domain:		Text += TEXT(" -ds"); break;
	case SF_Geometry:	Text += TEXT(" -gs"); break;
	case SF_Pixel:		Text += TEXT(" -ps"); break;
	case SF_Compute:	Text += TEXT(" -cs"); break;
	default: ensure(0); break;
	}
	if (Input.bCompilingForShaderPipeline)
	{
		Text += TEXT(" -pipeline");
	}
	if (Input.bIncludeUsedOutputs)
	{
		Text += TEXT(" -usedoutputs=");
		for (int32 Index = 0; Index < Input.UsedOutputs.Num(); ++Index)
		{
			if (Index != 0)
			{
				Text += TEXT("+");
			}
			Text += Input.UsedOutputs[Index];
		}
	}

	Text += TEXT(" ");
	Text += Input.DumpDebugInfoPath / Input.SourceFilename + TEXT(".usf");

	uint64 CFlags = 0;
	for (int32 Index = 0; Index < Input.Environment.CompilerFlags.Num(); ++Index)
	{
		CFlags = CFlags | ((uint64)1 << (uint64)Input.Environment.CompilerFlags[Index]);
	}
	if (CFlags)
	{
		Text += TEXT(" -cflags=");
		Text += FString::Printf(TEXT("%llu"), CFlags);
	}
	
	return Text;
}


namespace CrossCompiler
{
	FString CreateBatchFileContents(const FString& ShaderFile, const FString& OutputFile, uint32 Frequency, const FString& EntryPoint, const FString& VersionSwitch, uint32 CCFlags, const FString& ExtraArguments)
	{
		const TCHAR* FrequencySwitch = TEXT("");
		switch (Frequency)
		{
		case HSF_PixelShader:		FrequencySwitch = TEXT(" -ps"); break;
		case HSF_VertexShader:		FrequencySwitch = TEXT(" -vs"); break;
		case HSF_HullShader:		FrequencySwitch = TEXT(" -hs"); break;
		case HSF_DomainShader:		FrequencySwitch = TEXT(" -ds"); break;
		case HSF_ComputeShader:		FrequencySwitch = TEXT(" -cs"); break;
		case HSF_GeometryShader:	FrequencySwitch = TEXT(" -gs"); break;
		default:					check(0); break;
		}

		FString CCTCmdLine = ExtraArguments;
		CCTCmdLine += ((CCFlags & HLSLCC_NoValidation) == HLSLCC_NoValidation) ? TEXT(" -novalidate") : TEXT("");
		CCTCmdLine += ((CCFlags & HLSLCC_DX11ClipSpace) == HLSLCC_DX11ClipSpace) ? TEXT(" -dx11clip") : TEXT("");
		CCTCmdLine += ((CCFlags & HLSLCC_NoPreprocess) == HLSLCC_NoPreprocess) ? TEXT(" -nopp") : TEXT("");
		CCTCmdLine += ((CCFlags & HLSLCC_FlattenUniformBuffers) == HLSLCC_FlattenUniformBuffers) ? TEXT(" -flattenub") : TEXT("");
		CCTCmdLine += ((CCFlags & HLSLCC_FlattenUniformBufferStructures) == HLSLCC_FlattenUniformBufferStructures) ? TEXT(" -flattenubstruct") : TEXT("");
		CCTCmdLine += ((CCFlags & HLSLCC_GroupFlattenedUniformBuffers) == HLSLCC_GroupFlattenedUniformBuffers) ? TEXT(" -groupflatub") : TEXT("");
		CCTCmdLine += ((CCFlags & HLSLCC_ApplyCommonSubexpressionElimination) == HLSLCC_ApplyCommonSubexpressionElimination) ? TEXT(" -cse") : TEXT("");
		CCTCmdLine += ((CCFlags & HLSLCC_ExpandSubexpressions) == HLSLCC_ExpandSubexpressions) ? TEXT(" -xpxpr") : TEXT("");
		CCTCmdLine += ((CCFlags & HLSLCC_SeparateShaderObjects) == HLSLCC_SeparateShaderObjects) ? TEXT(" -separateshaders") : TEXT("");
		CCTCmdLine += ((CCFlags & HLSLCC_PackUniformsIntoUniformBuffers) == HLSLCC_PackUniformsIntoUniformBuffers) ? TEXT(" -packintoubs") : TEXT("");
		CCTCmdLine += ((CCFlags & HLSLCC_FixAtomicReferences) == HLSLCC_FixAtomicReferences) ? TEXT(" -fixatomics") : TEXT("");
		CCTCmdLine += ((CCFlags & HLSLCC_UseFullPrecisionInPS) == HLSLCC_UseFullPrecisionInPS) ? TEXT(" -usefullprecision") : TEXT("");
		FString BatchFile;
		if (PLATFORM_MAC)
		{
			BatchFile = FPaths::RootDir() / FString::Printf(TEXT("Engine/Source/ThirdParty/hlslcc/hlslcc/bin/Mac/hlslcc_64 %s -o=%s %s -entry=%s %s %s"), *ShaderFile, *OutputFile, FrequencySwitch, *EntryPoint, *VersionSwitch, *CCTCmdLine);
		}
		else if (PLATFORM_LINUX)
		{
			BatchFile = TEXT("#!/bin/sh\n");
			// add an extra '/' to the file name (which is absolute at this point) because CrossCompilerTool will strip out first '/' considering it a legacy DOS-style switch marker.
			BatchFile += FPaths::RootDir() / FString::Printf(TEXT("Engine/Binaries/Linux/CrossCompilerTool /%s -o=%s %s -entry=%s %s %s"), *ShaderFile, *OutputFile, FrequencySwitch, *EntryPoint, *VersionSwitch, *CCTCmdLine);
		}
		else if (PLATFORM_WINDOWS)
		{
			BatchFile = TEXT("@echo off");
			BatchFile += TEXT("\nif defined ue.hlslcc GOTO DONE\nset ue.hlslcc=");
			BatchFile += FPaths::RootDir() / TEXT("Engine\\Binaries\\Win64\\CrossCompilerTool.exe");
			BatchFile += TEXT("\n\n:DONE\n%ue.hlslcc% ");
			BatchFile += FString::Printf(TEXT("\"%s\" -o=\"%s\" %s -entry=%s %s %s"), *ShaderFile, *OutputFile, FrequencySwitch, *EntryPoint, *VersionSwitch, *CCTCmdLine);
			BatchFile += TEXT("\npause\n");
		}
		else
		{
			checkf(false, TEXT("CreateCrossCompilerBatchFileContents: unsupported platform!"));
		}

		return BatchFile;
	}


	/**
	 * Parse an error emitted by the HLSL cross-compiler.
	 * @param OutErrors - Array into which compiler errors may be added.
	 * @param InLine - A line from the compile log.
	 */
	void ParseHlslccError(TArray<FShaderCompilerError>& OutErrors, const FString& InLine)
	{
		const TCHAR* p = *InLine;
		FShaderCompilerError* Error = new(OutErrors) FShaderCompilerError();

		// Copy the filename.
		while (*p && *p != TEXT('(')) { Error->ErrorFile += (*p++); }
		Error->ErrorFile = GetRelativeShaderFilename(Error->ErrorFile);
		p++;

		// Parse the line number.
		int32 LineNumber = 0;
		while (*p && *p >= TEXT('0') && *p <= TEXT('9'))
		{
			LineNumber = 10 * LineNumber + (*p++ - TEXT('0'));
		}
		Error->ErrorLineString = *FString::Printf(TEXT("%d"), LineNumber);

		// Skip to the warning message.
		while (*p && (*p == TEXT(')') || *p == TEXT(':') || *p == TEXT(' ') || *p == TEXT('\t'))) { p++; }
		Error->StrippedErrorMessage = p;
	}

	static inline bool ParseIdentifier(const ANSICHAR*& Str, FString& OutStr)
	{
		OutStr = TEXT("");
		FString Result;
		while ((*Str >= 'A' && *Str <= 'Z')
			|| (*Str >= 'a' && *Str <= 'z')
			|| (*Str >= '0' && *Str <= '9')
			|| *Str == '_')
		{
			OutStr += (TCHAR)*Str;
			++Str;
		}

		return OutStr.Len() > 0;
	}

	static FORCEINLINE bool Match(const ANSICHAR*& Str, ANSICHAR Char)
	{
		if (*Str == Char)
		{
			++Str;
			return true;
		}

		return false;
	}

	template <typename T>
	static bool ParseIntegerNumber(const ANSICHAR*& Str, T& OutNum)
	{
		auto* OriginalStr = Str;
		OutNum = 0;
		while (*Str >= '0' && *Str <= '9')
		{
			OutNum = OutNum * 10 + *Str++ - '0';
		}

		return Str != OriginalStr;
	}

	static bool ParseSignedNumber(const ANSICHAR*& Str, int32& OutNum)
	{
		int32 Sign = Match(Str, '-') ? -1 : 1;
		uint32 Num = 0;
		if (ParseIntegerNumber(Str, Num))
		{
			OutNum = Sign * (int32)Num;
			return true;
		}

		return false;
	}

	/** Map shader frequency -> string for messages. */
	static const TCHAR* FrequencyStringTable[] =
	{
		TEXT("Vertex"),
		TEXT("Hull"),
		TEXT("Domain"),
		TEXT("Pixel"),
		TEXT("Geometry"),
		TEXT("Compute")
	};

	/** Compile time check to verify that the GL mapping tables are up-to-date. */
	static_assert(SF_NumFrequencies == ARRAY_COUNT(FrequencyStringTable), "NumFrequencies changed. Please update tables.");

	const TCHAR* GetFrequencyName(EShaderFrequency Frequency)
	{
		check((int32)Frequency >= 0 && Frequency < SF_NumFrequencies);
		return FrequencyStringTable[Frequency];
	}

	FHlslccHeader::FHlslccHeader() :
		Name(TEXT(""))
	{
		NumThreads[0] = NumThreads[1] = NumThreads[2] = 0;
	}

	bool FHlslccHeader::Read(const ANSICHAR*& ShaderSource, int32 SourceLen)
	{
#define DEF_PREFIX_STR(Str) \
		static const ANSICHAR* Str##Prefix = "// @" #Str ": "; \
		static const int32 Str##PrefixLen = FCStringAnsi::Strlen(Str##Prefix)
		DEF_PREFIX_STR(Inputs);
		DEF_PREFIX_STR(Outputs);
		DEF_PREFIX_STR(UniformBlocks);
		DEF_PREFIX_STR(Uniforms);
		DEF_PREFIX_STR(PackedGlobals);
		DEF_PREFIX_STR(PackedUB);
		DEF_PREFIX_STR(PackedUBCopies);
		DEF_PREFIX_STR(PackedUBGlobalCopies);
		DEF_PREFIX_STR(Samplers);
		DEF_PREFIX_STR(UAVs);
		DEF_PREFIX_STR(SamplerStates);
		DEF_PREFIX_STR(NumThreads);
#undef DEF_PREFIX_STR

		// Skip any comments that come before the signature.
		while (FCStringAnsi::Strncmp(ShaderSource, "//", 2) == 0 &&
			FCStringAnsi::Strncmp(ShaderSource + 2, " !", 2) != 0 &&
			FCStringAnsi::Strncmp(ShaderSource + 2, " @", 2) != 0)
		{
			ShaderSource += 2;
			while (*ShaderSource && *ShaderSource++ != '\n')
			{
				// Do nothing
			}
		}

		// Read shader name if any
		if (FCStringAnsi::Strncmp(ShaderSource, "// !", 4) == 0)
		{
			ShaderSource += 4;
			while (*ShaderSource && *ShaderSource != '\n')
			{
				Name += (TCHAR)*ShaderSource;
				++ShaderSource;
			}

			if (*ShaderSource == '\n')
			{
				++ShaderSource;
			}
		}

		// Skip any comments that come before the signature.
		while (FCStringAnsi::Strncmp(ShaderSource, "//", 2) == 0 &&
			FCStringAnsi::Strncmp(ShaderSource + 2, " @", 2) != 0)
		{
			ShaderSource += 2;
			while (*ShaderSource && *ShaderSource++ != '\n')
			{
				// Do nothing
			}
		}

		if (FCStringAnsi::Strncmp(ShaderSource, InputsPrefix, InputsPrefixLen) == 0)
		{
			ShaderSource += InputsPrefixLen;

			if (!ReadInOut(ShaderSource, Inputs))
			{
				return false;
			}
		}

		if (FCStringAnsi::Strncmp(ShaderSource, OutputsPrefix, OutputsPrefixLen) == 0)
		{
			ShaderSource += OutputsPrefixLen;

			if (!ReadInOut(ShaderSource, Outputs))
			{
				return false;
			}
		}

		if (FCStringAnsi::Strncmp(ShaderSource, UniformBlocksPrefix, UniformBlocksPrefixLen) == 0)
		{
			ShaderSource += UniformBlocksPrefixLen;

			while (*ShaderSource && *ShaderSource != '\n')
			{
				FAttribute UniformBlock;
				if (!ParseIdentifier(ShaderSource, UniformBlock.Name))
				{
					return false;
				}

				if (!Match(ShaderSource, '('))
				{
					return false;
				}
				
				if (!ParseIntegerNumber(ShaderSource, UniformBlock.Index))
				{
					return false;
				}

				if (!Match(ShaderSource, ')'))
				{
					return false;
				}

				UniformBlocks.Add(UniformBlock);

				if (Match(ShaderSource, '\n'))
				{
					break;
				}

				if (Match(ShaderSource, ','))
				{
					continue;
				}
			
				//#todo-rco: Need a log here
				//UE_LOG(ShaderCompilerCommon, Warning, TEXT("Invalid char '%c'"), *ShaderSource);
				return false;
			}
		}

		if (FCStringAnsi::Strncmp(ShaderSource, UniformsPrefix, UniformsPrefixLen) == 0)
		{
			// @todo-mobile: Will we ever need to support this code path?
			check(0);
			return false;
/*
			ShaderSource += UniformsPrefixLen;

			while (*ShaderSource && *ShaderSource != '\n')
			{
				uint16 ArrayIndex = 0;
				uint16 Offset = 0;
				uint16 NumComponents = 0;

				FString ParameterName = ParseIdentifier(ShaderSource);
				verify(ParameterName.Len() > 0);
				verify(Match(ShaderSource, '('));
				ArrayIndex = ParseNumber(ShaderSource);
				verify(Match(ShaderSource, ':'));
				Offset = ParseNumber(ShaderSource);
				verify(Match(ShaderSource, ':'));
				NumComponents = ParseNumber(ShaderSource);
				verify(Match(ShaderSource, ')'));

				ParameterMap.AddParameterAllocation(
					*ParameterName,
					ArrayIndex,
					Offset * BytesPerComponent,
					NumComponents * BytesPerComponent
					);

				if (ArrayIndex < OGL_NUM_PACKED_UNIFORM_ARRAYS)
				{
					PackedUniformSize[ArrayIndex] = FMath::Max<uint16>(
						PackedUniformSize[ArrayIndex],
						BytesPerComponent * (Offset + NumComponents)
						);
				}

				// Skip the comma.
				if (Match(ShaderSource, '\n'))
				{
					break;
				}

				verify(Match(ShaderSource, ','));
			}

			Match(ShaderSource, '\n');
*/
		}

		// @PackedGlobals: Global0(h:0,1),Global1(h:4,1),Global2(h:8,1)
		if (FCStringAnsi::Strncmp(ShaderSource, PackedGlobalsPrefix, PackedGlobalsPrefixLen) == 0)
		{
			ShaderSource += PackedGlobalsPrefixLen;
			while (*ShaderSource && *ShaderSource != '\n')
			{
				FPackedGlobal PackedGlobal;
				if (!ParseIdentifier(ShaderSource, PackedGlobal.Name))
				{
					return false;
				}

				if (!Match(ShaderSource, '('))
				{
					return false;
				}

				PackedGlobal.PackedType = *ShaderSource++;

				if (!Match(ShaderSource, ':'))
				{
					return false;
				}

				if (!ParseIntegerNumber(ShaderSource, PackedGlobal.Offset))
				{
					return false;
				}

				if (!Match(ShaderSource, ','))
				{
					return false;
				}

				if (!ParseIntegerNumber(ShaderSource, PackedGlobal.Count))
				{
					return false;
				}

				if (!Match(ShaderSource, ')'))
				{
					return false;
				}

				PackedGlobals.Add(PackedGlobal);

				// Break if EOL
				if (Match(ShaderSource, '\n'))
				{
					break;
				}

				// Has to be a comma!
				if (Match(ShaderSource, ','))
				{
					continue;
				}

				//#todo-rco: Need a log here
				//UE_LOG(ShaderCompilerCommon, Warning, TEXT("Invalid char '%c'"), *ShaderSource);
				return false;
			}
		}

		// Packed Uniform Buffers (Multiple lines)
		// @PackedUB: CBuffer(0): CBMember0(0,1),CBMember1(1,1)
		while (FCStringAnsi::Strncmp(ShaderSource, PackedUBPrefix, PackedUBPrefixLen) == 0)
		{
			ShaderSource += PackedUBPrefixLen;

			FPackedUB PackedUB;

			if (!ParseIdentifier(ShaderSource, PackedUB.Attribute.Name))
			{
				return false;
			}

			if (!Match(ShaderSource, '('))
			{
				return false;
			}
			
			if (!ParseIntegerNumber(ShaderSource, PackedUB.Attribute.Index))
			{
				return false;
			}

			if (!Match(ShaderSource, ')'))
			{
				return false;
			}

			if (!Match(ShaderSource, ':'))
			{
				return false;
			}

			if (!Match(ShaderSource, ' '))
			{
				return false;
			}

			while (*ShaderSource && *ShaderSource != '\n')
			{
				FPackedUB::FMember Member;
				ParseIdentifier(ShaderSource, Member.Name);
				if (!Match(ShaderSource, '('))
				{
					return false;
				}

				if (!ParseIntegerNumber(ShaderSource, Member.Offset))
				{
					return false;
				}
				
				if (!Match(ShaderSource, ','))
				{
					return false;
				}

				if (!ParseIntegerNumber(ShaderSource, Member.Count))
				{
					return false;
				}

				if (!Match(ShaderSource, ')'))
				{
					return false;
				}

				PackedUB.Members.Add(Member);

				// Break if EOL
				if (Match(ShaderSource, '\n'))
				{
					break;
				}

				// Has to be a comma!
				if (Match(ShaderSource, ','))
				{
					continue;
				}

				//#todo-rco: Need a log here
				//UE_LOG(ShaderCompilerCommon, Warning, TEXT("Invalid char '%c'"), *ShaderSource);
				return false;
			}

			PackedUBs.Add(PackedUB);
		}

		// @PackedUBCopies: 0:0-0:h:0:1,0:1-0:h:4:1,1:0-1:h:0:1
		if (FCStringAnsi::Strncmp(ShaderSource, PackedUBCopiesPrefix, PackedUBCopiesPrefixLen) == 0)
		{
			ShaderSource += PackedUBCopiesPrefixLen;
			if (!ReadCopies(ShaderSource, false, PackedUBCopies))
			{
				return false;
			}
		}

		// @PackedUBGlobalCopies: 0:0-h:12:1,0:1-h:16:1,1:0-h:20:1
		if (FCStringAnsi::Strncmp(ShaderSource, PackedUBGlobalCopiesPrefix, PackedUBGlobalCopiesPrefixLen) == 0)
		{
			ShaderSource += PackedUBGlobalCopiesPrefixLen;
			if (!ReadCopies(ShaderSource, true, PackedUBGlobalCopies))
			{
				return false;
			}
		}

		if (FCStringAnsi::Strncmp(ShaderSource, SamplersPrefix, SamplersPrefixLen) == 0)
		{
			ShaderSource += SamplersPrefixLen;

			while (*ShaderSource && *ShaderSource != '\n')
			{
				FSampler Sampler;

				if (!ParseIdentifier(ShaderSource, Sampler.Name))
				{
					return false;
				}

				if (!Match(ShaderSource, '('))
				{
					return false;
				}

				if (!ParseIntegerNumber(ShaderSource, Sampler.Offset))
				{
					return false;
				}

				if (!Match(ShaderSource, ':'))
				{
					return false;
				}

				if (!ParseIntegerNumber(ShaderSource, Sampler.Count))
				{
					return false;
				}

				if (Match(ShaderSource, '['))
				{
					// Sampler States
					do
					{
						FString SamplerState;
						
						if (!ParseIdentifier(ShaderSource, SamplerState))
						{
							return false;
						}

						Sampler.SamplerStates.Add(SamplerState);
					}
					while (Match(ShaderSource, ','));

					if (!Match(ShaderSource, ']'))
					{
						return false;
					}
				}

				if (!Match(ShaderSource, ')'))
				{
					return false;
				}

				Samplers.Add(Sampler);

				// Break if EOL
				if (Match(ShaderSource, '\n'))
				{
					break;
				}

				// Has to be a comma!
				if (Match(ShaderSource, ','))
				{
					continue;
				}

				//#todo-rco: Need a log here
				//UE_LOG(ShaderCompilerCommon, Warning, TEXT("Invalid char '%c'"), *ShaderSource);
				return false;
			}
		}

		if (FCStringAnsi::Strncmp(ShaderSource, UAVsPrefix, UAVsPrefixLen) == 0)
		{
			ShaderSource += UAVsPrefixLen;

			while (*ShaderSource && *ShaderSource != '\n')
			{
				FUAV UAV;

				if (!ParseIdentifier(ShaderSource, UAV.Name))
				{
					return false;
				}

				if (!Match(ShaderSource, '('))
				{
					return false;
				}

				if (!ParseIntegerNumber(ShaderSource, UAV.Offset))
				{
					return false;
				}

				if (!Match(ShaderSource, ':'))
				{
					return false;
				}

				if (!ParseIntegerNumber(ShaderSource, UAV.Count))
				{
					return false;
				}

				if (!Match(ShaderSource, ')'))
				{
					return false;
				}

				UAVs.Add(UAV);

				// Break if EOL
				if (Match(ShaderSource, '\n'))
				{
					break;
				}

				// Has to be a comma!
				if (Match(ShaderSource, ','))
				{
					continue;
				}

				//#todo-rco: Need a log here
				//UE_LOG(ShaderCompilerCommon, Warning, TEXT("Invalid char '%c'"), *ShaderSource);
				return false;
			}
		}

		if (FCStringAnsi::Strncmp(ShaderSource, SamplerStatesPrefix, SamplerStatesPrefixLen) == 0)
		{
			ShaderSource += SamplerStatesPrefixLen;
			while (*ShaderSource && *ShaderSource != '\n')
			{
				FAttribute SamplerState;
				if (!ParseIntegerNumber(ShaderSource, SamplerState.Index))
				{
					return false;
				}

				if (!Match(ShaderSource, ':'))
				{
					return false;
				}

				if (!ParseIdentifier(ShaderSource, SamplerState.Name))
				{
					return false;
				}

				SamplerStates.Add(SamplerState);

				// Break if EOL
				if (Match(ShaderSource, '\n'))
				{
					break;
				}

				// Has to be a comma!
				if (Match(ShaderSource, ','))
				{
					continue;
				}

				//#todo-rco: Need a log here
				//UE_LOG(ShaderCompilerCommon, Warning, TEXT("Invalid char '%c'"), *ShaderSource);
				return false;
			}
		}

		if (FCStringAnsi::Strncmp(ShaderSource, NumThreadsPrefix, NumThreadsPrefixLen) == 0)
		{
			ShaderSource += NumThreadsPrefixLen;
			if (!ParseIntegerNumber(ShaderSource, NumThreads[0]))
			{
				return false;
			}
			if (!Match(ShaderSource, ','))
			{
				return false;
			}

			if (!Match(ShaderSource, ' '))
			{
				return false;
			}

			if (!ParseIntegerNumber(ShaderSource, NumThreads[1]))
			{
				return false;
			}

			if (!Match(ShaderSource, ','))
			{
				return false;
			}

			if (!Match(ShaderSource, ' '))
			{
				return false;
			}

			if (!ParseIntegerNumber(ShaderSource, NumThreads[2]))
			{
				return false;
			}

			if (!Match(ShaderSource, '\n'))
			{
				return false;
			}
		}

		return true;
	}

	bool FHlslccHeader::ReadCopies(const ANSICHAR*& ShaderSource, bool bGlobals, TArray<FPackedUBCopy>& OutCopies)
	{
		while (*ShaderSource && *ShaderSource != '\n')
		{
			FPackedUBCopy PackedUBCopy;
			PackedUBCopy.DestUB = 0;

			if (!ParseIntegerNumber(ShaderSource, PackedUBCopy.SourceUB))
			{
				return false;
			}

			if (!Match(ShaderSource, ':'))
			{
				return false;
			}

			if (!ParseIntegerNumber(ShaderSource, PackedUBCopy.SourceOffset))
			{
				return false;
			}

			if (!Match(ShaderSource, '-'))
			{
				return false;
			}

			if (!bGlobals)
			{
				if (!ParseIntegerNumber(ShaderSource, PackedUBCopy.DestUB))
				{
					return false;
				}

				if (!Match(ShaderSource, ':'))
				{
					return false;
				}
			}

			PackedUBCopy.DestPackedType = *ShaderSource++;

			if (!Match(ShaderSource, ':'))
			{
				return false;
			}

			if (!ParseIntegerNumber(ShaderSource, PackedUBCopy.DestOffset))
			{
				return false;
			}

			if (!Match(ShaderSource, ':'))
			{
				return false;
			}

			if (!ParseIntegerNumber(ShaderSource, PackedUBCopy.Count))
			{
				return false;
			}

			OutCopies.Add(PackedUBCopy);

			// Break if EOL
			if (Match(ShaderSource, '\n'))
			{
				break;
			}

			// Has to be a comma!
			if (Match(ShaderSource, ','))
			{
				continue;
			}

			//#todo-rco: Need a log here
			//UE_LOG(ShaderCompilerCommon, Warning, TEXT("Invalid char '%c'"), *ShaderSource);
			return false;
		}

		return true;
	}

	bool FHlslccHeader::ReadInOut(const ANSICHAR*& ShaderSource, TArray<FInOut>& OutAttributes)
	{
		while (*ShaderSource && *ShaderSource != '\n')
		{
			FInOut Attribute;

			if (!ParseIdentifier(ShaderSource, Attribute.Type))
			{
				return false;
			}

			if (Match(ShaderSource, '['))
			{
				if (!ParseIntegerNumber(ShaderSource, Attribute.ArrayCount))
				{
					return false;
				}

				if (!Match(ShaderSource, ']'))
				{
					return false;
				}
			}
			else
			{
				Attribute.ArrayCount = 0;
			}

			if (Match(ShaderSource, ';'))
			{
				if (!ParseSignedNumber(ShaderSource, Attribute.Index))
				{
					return false;
				}
			}

			if (!Match(ShaderSource, ':'))
			{
				return false;
			}

			if (!ParseIdentifier(ShaderSource, Attribute.Name))
			{
				return false;
			}

			// Optional array suffix
			if (Match(ShaderSource, '['))
			{
				Attribute.Name += '[';
				while (*ShaderSource)
				{
					Attribute.Name += *ShaderSource;
					if (Match(ShaderSource, ']'))
					{
						break;
					}
					++ShaderSource;
				}
			}

			OutAttributes.Add(Attribute);

			// Break if EOL
			if (Match(ShaderSource, '\n'))
			{
				break;
			}

			// Has to be a comma!
			if (Match(ShaderSource, ','))
			{
				continue;
			}

			//#todo-rco: Need a log here
			//UE_LOG(ShaderCompilerCommon, Warning, TEXT("Invalid char '%c'"), *ShaderSource);
			return false;
		}

		return true;
	}
}
