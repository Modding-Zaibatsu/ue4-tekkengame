// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LinuxPlatformStackWalk.cpp: Linux implementations of stack walk functions
=============================================================================*/

#include "CorePrivatePCH.h"
#include "Misc/App.h"
#include "LinuxPlatformCrashContext.h"
#include <execinfo.h>
#include <dlfcn.h>
#include <cxxabi.h>
#include <stdio.h>

// these are not actually system headers, but a TPS library (see ThirdParty/elftoolchain)
#include <libelf.h>
#include <_libelf.h>
#include <libdwarf.h>
#include <dwarf.h>

#ifndef DW_AT_MIPS_linkage_name
	#define DW_AT_MIPS_linkage_name		0x2007			// common extension, used before DW_AT_linkage_name became standard
#endif // DW_AT_MIPS_linkage_name

namespace LinuxStackWalkHelpers
{
	struct LinuxBacktraceSymbols
	{
		/** Lock for thread-safe initialization. */
		FCriticalSection CriticalSection;

		/** Initialized flag. If initialization failed, it don't run again. */
		bool Inited;

		/** File descriptor needed for libelf to open (our own) binary */
		int ExeFd;

		/** Elf header as used by libelf (forward-declared in the same way as libelf does it, it's normally of Elf type) */
		struct _Elf* ElfHdr;

		/** DWARF handle used by libdwarf (forward-declared in the same way as libdwarf does it, it's normally of Dwarf_Debug type) */
		struct _Dwarf_Debug	* DebugInfo;

		LinuxBacktraceSymbols()
		:	Inited(false)
		,	ExeFd(-1)
		,	ElfHdr(NULL)
		,	DebugInfo(NULL)
		{
		}

		~LinuxBacktraceSymbols();

		void Init();

		/**
		 * Gets information for the crash.
		 *
		 * @param Address the address to look up info for
		 * @param OutModuleNamePtr pointer to module name (may be null). Caller doesn't have to free it, but need to consider it temporary (i.e. next GetInfoForAddress() call on any thread may change it).
		 * @param OutFunctionNamePtr pointer to function name (may be null). Caller doesn't have to free it, but need to consider it temporary (i.e. next GetInfoForAddress() call on any thread may change it).
		 * @param OutSourceFilePtr pointer to source filename (may be null). Caller doesn't have to free it, but need to consider it temporary (i.e. next GetInfoForAddress() call on any thread may change it).
		 * @param OutLineNumberPtr pointer to line in a source file (may be null). Caller doesn't have to free it, but need to consider it temporary (i.e. next GetInfoForAddress() call on any thread may change it).
		 *
		 * @return true if succeeded in getting the info. If false is returned, none of above parameters should be trusted to contain valid data!
		 */
		bool GetInfoForAddress(void* Address, const char** OutModuleNamePtr, const char** OutFunctionNamePtr, const char** OutSourceFilePtr, int* OutLineNumberPtr);

		/**
		 * Checks check if address is inside this entry.
		 *
		 * @param DebugInfo DWARF debug info point
		 * @param Die pointer to Debugging Information Entry
		 * @param Addr address to check
		 *
		 * @return true if the address is in range
		 */
		static bool CheckAddressInRange(Dwarf_Debug DebugInfo, Dwarf_Die Die, Dwarf_Unsigned Addr);

		/**
		 * Finds a function name in DWARF DIE (Debug Information Entry) and its children.
		 * For more info on DWARF format, see http://www.dwarfstd.org/Download.php , http://www.ibm.com/developerworks/library/os-debugging/
		 * Note: that function is not exactly traversing the tree, but this "seems to work"(tm). Not sure if we need to descend properly (taking child of every sibling), this
		 * takes too much time (and callstacks seem to be fine without it).
		 */
		static void FindFunctionNameInDIEAndChildren(Dwarf_Debug DebugInfo, Dwarf_Die Die, Dwarf_Addr Addr, const char **OutFuncName);

		/**
		 * Finds a function name in DWARF DIE (Debug Information Entry).
		 * For more info on DWARF format, see http://www.dwarfstd.org/Download.php , http://www.ibm.com/developerworks/library/os-debugging/
		 *
		 * @return true if we need to stop search (i.e. either found it or some error happened)
		 */
		static bool FindFunctionNameInDIE(Dwarf_Debug DebugInfo, Dwarf_Die Die, Dwarf_Addr Addr, const char **OutFuncName);

		/**
		 * Tries all usable attributes in DIE to determine function name (i.e. DW_AT_MIPS_linkage_name, DW_AT_linkage_name, DW_AT_name)
		 *
		 * @param Die Debugging Infromation Entry
		 * @param OutFuncName Pointer to function name (volatile, copy it after call). Will not be touched if function returns false
		 *
		 * @return true if found
		 */
		static bool FindNameAttributeInDIE(Dwarf_Die Die, const char **OutFuncName);
	};

	enum
	{
		MaxMangledNameLength = 1024,
		MaxDemangledNameLength = 1024
	};

	void LinuxBacktraceSymbols::Init()
	{
		FScopeLock ScopeLock( &CriticalSection );

		if (!Inited)
		{
			// open ourselves for examination
			if (!FParse::Param( FCommandLine::Get(), TEXT(CMDARG_SUPPRESS_DWARF_PARSING)))
			{
				ExeFd = open("/proc/self/exe", O_RDONLY);
				if (ExeFd >= 0)
				{
					Dwarf_Error ErrorInfo;
					// allocate DWARF debug descriptor
					if (dwarf_init(ExeFd, DW_DLC_READ, NULL, NULL, &DebugInfo, &ErrorInfo) == DW_DLV_OK)
					{
						// get ELF descritor
						if (dwarf_get_elf(DebugInfo, &ElfHdr, &ErrorInfo) != DW_DLV_OK)
						{
							dwarf_finish(DebugInfo, &ErrorInfo);
							DebugInfo = NULL;

							close(ExeFd);
							ExeFd = -1;
						}
					}
					else
					{
						DebugInfo = NULL;
						close(ExeFd);
						ExeFd = -1;
					}
				}			
			}
			Inited = true;
		}
	}

	LinuxBacktraceSymbols::~LinuxBacktraceSymbols()
	{
		if (DebugInfo)
		{
			Dwarf_Error ErrorInfo;
			dwarf_finish(DebugInfo, &ErrorInfo);
			DebugInfo = NULL;
		}

		if (ElfHdr)
		{
			elf_end(ElfHdr);
			ElfHdr = NULL;
		}

		if (ExeFd >= 0)
		{
			close(ExeFd);
			ExeFd = -1;
		}
	}

	bool LinuxBacktraceSymbols::GetInfoForAddress(void* Address, const char** OutModuleNamePtr, const char** OutFunctionNamePtr, const char** OutSourceFilePtr, int* OutLineNumberPtr)
	{
		if (DebugInfo == NULL)
		{
			return false;
		}

		Dwarf_Die Die;
		Dwarf_Unsigned Addr = reinterpret_cast< Dwarf_Unsigned >( Address ), LineNumber = 0;
		const char * SrcFile = NULL;

		static_assert(sizeof(Dwarf_Unsigned) >= sizeof(Address), "Dwarf_Unsigned type should be long enough to represent pointers. Check libdwarf bitness.");

		int ReturnCode = DW_DLV_OK;
		Dwarf_Error ErrorInfo;
		bool bExitHeaderLoop = false;
		int32 MaxCompileUnitsAllowed = 16 * 1024 * 1024;	// safeguard to make sure we never get into an infinite loop
		const int32 kMaxBufferLinesAllowed = 16 * 1024 * 1024;	// safeguard to prevent too long line loop
		for(;;)
		{
			if (--MaxCompileUnitsAllowed <= 0)
			{
				fprintf(stderr, "Breaking out from what seems to be an infinite loop during DWARF parsing (too many compile units).\n");
				ReturnCode = DW_DLE_DIE_NO_CU_CONTEXT;	// invalidate
				break;
			}

			if (bExitHeaderLoop)
				break;

			ReturnCode = dwarf_next_cu_header(DebugInfo, NULL, NULL, NULL, NULL, NULL, &ErrorInfo);
			if (ReturnCode != DW_DLV_OK)
				break;

			Die = NULL;

			while(dwarf_siblingof(DebugInfo, Die, &Die, &ErrorInfo) == DW_DLV_OK)
			{
				Dwarf_Half Tag;
				if (dwarf_tag(Die, &Tag, &ErrorInfo) != DW_DLV_OK)
				{
					bExitHeaderLoop = true;
					break;
				}

				if (Tag == DW_TAG_compile_unit)
				{
					break;
				}
			}

			if (Die == NULL)
			{
				break;
			}

			// check if address is inside this CU
			if (!CheckAddressInRange(DebugInfo, Die, Addr))
			{
				continue;
			}

			Dwarf_Line * LineBuf;
			Dwarf_Signed NumLines = kMaxBufferLinesAllowed;
			if (dwarf_srclines(Die, &LineBuf, &NumLines, &ErrorInfo) != DW_DLV_OK)
			{
				// could not get line info for some reason
				continue;
			}

			if (NumLines >= kMaxBufferLinesAllowed)
			{
				fprintf(stderr, "Number of lines associated with a DIE looks unreasonable (%d), early quitting.\n", static_cast<int32>(NumLines));
				ReturnCode = DW_DLE_DIE_NO_CU_CONTEXT;	// invalidate
				break;
			}

			// look which line is that
			Dwarf_Addr LineAddress, PrevLineAddress = ~0ULL;
			Dwarf_Unsigned LineIdx = NumLines;
			for (int Idx = 0; Idx < NumLines; ++Idx)
			{
				if (dwarf_lineaddr(LineBuf[Idx], &LineAddress, &ErrorInfo) != 0)
				{
					bExitHeaderLoop = true;
					break;
				}
				// check if we hit the exact line
				if (Addr == LineAddress)
				{
					LineIdx = Idx;
					bExitHeaderLoop = true;
					break;
				}
				else if (PrevLineAddress < Addr && Addr < LineAddress)
				{
					LineIdx = Idx - 1;
					break;
				}
				PrevLineAddress = LineAddress;
			}
			if (LineIdx < NumLines)
			{
				if (dwarf_lineno(LineBuf[LineIdx], &LineNumber, &ErrorInfo) != 0)
				{
					fprintf(stderr, "Can't get line number by dwarf_lineno.\n");
					break;
				}
				for (int Idx = LineIdx; Idx >= 0; --Idx)
				{
					char * SrcFileTemp = NULL;
					if (!dwarf_linesrc(LineBuf[Idx], &SrcFileTemp, &ErrorInfo))
					{
						SrcFile = SrcFileTemp;
						break;
					}
				}
				bExitHeaderLoop = true;
			}
		}

		bool bSuccess = (ReturnCode == DW_DLV_OK);

		if (LIKELY(bSuccess))
		{
			if (LIKELY(OutFunctionNamePtr != nullptr))
			{
				const char * FunctionName = nullptr;
				FindFunctionNameInDIEAndChildren(DebugInfo, Die, Addr, &FunctionName);
				if (LIKELY(FunctionName != nullptr))
				{
					*OutFunctionNamePtr = FunctionName;
				}
				else
				{
					// make sure it's not null
					*OutFunctionNamePtr = "Unknown";
				}
			}

			if (LIKELY(OutSourceFilePtr != nullptr && OutLineNumberPtr != nullptr))
			{
				if (SrcFile != nullptr)
				{
					*OutSourceFilePtr = SrcFile;
					*OutLineNumberPtr = LineNumber;
				}
				else
				{
					*OutSourceFilePtr = "Unknown";
					*OutLineNumberPtr = -1;
				}
			}

			if (LIKELY(OutModuleNamePtr != nullptr))
			{
				const char* ModuleName = nullptr;

				Dl_info DlInfo;
				if (dladdr(Address, &DlInfo) != 0)
				{
					if (DlInfo.dli_fname != nullptr)
					{
						ModuleName = DlInfo.dli_fname;	// this is a pointer we don't own, but assuming it's good until at least the next dladdr call
					}
				}

				if (LIKELY(ModuleName != nullptr))
				{
					*OutModuleNamePtr = ModuleName;
				}
				else
				{
					*OutModuleNamePtr = "Unknown";
				}
			}
		}

		// Resets internal CU pointer, so next time we get here it begins from the start
		while (ReturnCode != DW_DLV_NO_ENTRY) 
		{
			if (ReturnCode == DW_DLV_ERROR)
			{
				break;
			}
			ReturnCode = dwarf_next_cu_header(DebugInfo, NULL, NULL, NULL, NULL, NULL, &ErrorInfo);
		}

		return bSuccess;
	}

	bool LinuxBacktraceSymbols::CheckAddressInRange(Dwarf_Debug DebugInfo, Dwarf_Die Die, Dwarf_Unsigned Addr)
	{
		Dwarf_Attribute *AttrList;
		Dwarf_Signed AttrCount;

		if (UNLIKELY(dwarf_attrlist(Die, &AttrList, &AttrCount, NULL) != DW_DLV_OK))
		{
			// assume not in range if we couldn't get the information
			return false;
		}

		Dwarf_Addr LowAddr = 0, HighAddr = 0, HighOffset = 0;

		for (int i = 0; i < AttrCount; i++)
		{
			Dwarf_Half Attr;
			if (dwarf_whatattr(AttrList[i], &Attr, nullptr) != DW_DLV_OK)
			{
				continue;
			}

			switch (Attr)
			{
				case DW_AT_low_pc:
					{
						Dwarf_Addr TempLowAddr;
						if (dwarf_formaddr(AttrList[i], &TempLowAddr, nullptr) == DW_DLV_OK)
						{
							if (LIKELY(TempLowAddr > Addr))	// shortcut
							{
								return false;
							}

							LowAddr = TempLowAddr;
						}
					}
					break;

				case DW_AT_high_pc:
					{
						Dwarf_Addr TempHighAddr;
						if (dwarf_formaddr(AttrList[i], &TempHighAddr, nullptr) == DW_DLV_OK)
						{
							if (LIKELY(TempHighAddr <= Addr))	// shortcut
							{
								return false;
							}

							HighAddr = TempHighAddr;
						}

						// Offset is used since DWARF-4. Store it, but don't compare right now in case
						// we haven't yet initialized LowAddr
						Dwarf_Unsigned TempHighOffset;
						if (dwarf_formudata(AttrList[i], &TempHighOffset, nullptr) == DW_DLV_OK)
						{
							HighOffset = TempHighOffset;
						}
					}
					break;

				case DW_AT_ranges:
					{
						Dwarf_Unsigned Offset;
						if (dwarf_formudata(AttrList[i], &Offset, NULL) != DW_DLV_OK)
						{
							continue;
						}

						Dwarf_Ranges *Ranges;
						Dwarf_Signed Count;
						if (dwarf_get_ranges(DebugInfo, (Dwarf_Off) Offset, &Ranges, &Count, nullptr, nullptr) != DW_DLV_OK)
						{
							continue;
						}

						for (int j = 0; j < Count; j++)
						{
							if (Ranges[j].dwr_type == DW_RANGES_END)
							{
								break;
							}
							if (Ranges[j].dwr_type == DW_RANGES_ENTRY)
							{
								if ((Ranges[j].dwr_addr1 <= Addr) && (Addr < Ranges[j].dwr_addr2))
								{
									return true;
								}
								continue;
							}
						}
						return false;
					}
					break;

				default:
					break;
			}
		}

		if (UNLIKELY(HighAddr == 0 && HighOffset != 0))
		{
			HighAddr = LowAddr + HighOffset;
		}

		return LowAddr <= Addr && Addr < HighAddr;
	}

	bool LinuxBacktraceSymbols::FindNameAttributeInDIE(Dwarf_Die Die, const char **OutFuncName)
	{
		Dwarf_Error ErrorInfo;
		int ReturnCode;

		// look first for DW_AT_linkage_name or DW_AT_MIPS_linkage_name, since they hold fully qualified (albeit mangled) name
		Dwarf_Attribute LinkageNameAt;
		// DW_AT_MIPS_linkage_name is preferred because we're using DWARF2 by default
		ReturnCode = dwarf_attr(Die, DW_AT_MIPS_linkage_name, &LinkageNameAt, &ErrorInfo);
		if (UNLIKELY(ReturnCode == DW_DLV_NO_ENTRY))
		{
			// retry with newer DW_AT_linkage_name
			ReturnCode = dwarf_attr(Die, DW_AT_linkage_name, &LinkageNameAt, &ErrorInfo);
		}

		if (LIKELY(ReturnCode == DW_DLV_OK))
		{
			char *TempFuncName;
			if (LIKELY(dwarf_formstring(LinkageNameAt, &TempFuncName, &ErrorInfo) == DW_DLV_OK))
			{
				// try to demangle
				int DemangleStatus = 0xBAD;
				char *Demangled = abi::__cxa_demangle(TempFuncName, nullptr, nullptr, &DemangleStatus);
				if (DemangleStatus == 0 && Demangled != nullptr)
				{
					// cache the demangled name
					static char CachedDemangledName[1024];
					FCStringAnsi::Strcpy(CachedDemangledName, sizeof(CachedDemangledName), Demangled);

					*OutFuncName = CachedDemangledName;
				}
				else
				{
					*OutFuncName = TempFuncName;
				}

				if (Demangled)
				{
					free(Demangled);
				}
				return true;
			}
		}

		// if everything else fails, just take DW_AT_name, but in case of class methods, it is only a method name, so the information will be incomplete and almost useless
		const char *TempMethodName;
		if (LIKELY(dwarf_attrval_string(Die, DW_AT_name, &TempMethodName, &ErrorInfo) == DW_DLV_OK))
		{
			*OutFuncName = TempMethodName;
			return true;
		}

		return false;
	}

	/**
	 * Finds a function name in DWARF DIE (Debug Information Entry).
	 * For more info on DWARF format, see http://www.dwarfstd.org/Download.php , http://www.ibm.com/developerworks/library/os-debugging/
	 *
	 * @return true if we need to stop search (i.e. either found it or some error happened)
	 */
	bool LinuxBacktraceSymbols::FindFunctionNameInDIE(Dwarf_Debug DebugInfo, Dwarf_Die Die, Dwarf_Addr Addr, const char **OutFuncName)
	{
		Dwarf_Error ErrorInfo;
		Dwarf_Half Tag;

		if (dwarf_tag(Die, &Tag, &ErrorInfo) != DW_DLV_OK || Tag != DW_TAG_subprogram)
		{
			return false;
		}

		// check if address is inside this entry
		if (!CheckAddressInRange(DebugInfo, Die, Addr))
		{
			return false;
		}

		// attempt to find the name in DW_TAG_subprogram DIE
		if (FindNameAttributeInDIE(Die, OutFuncName))
		{
			return true;
		}

		// If not found, navigate to specification DIE and look there
		Dwarf_Attribute SpecAt;
		if (UNLIKELY(dwarf_attr(Die, DW_AT_specification, &SpecAt, &ErrorInfo) != DW_DLV_OK))
		{
			// no specificaation die
			return false;
		}

		Dwarf_Off Offset;
		if (UNLIKELY(dwarf_global_formref(SpecAt, &Offset, &ErrorInfo) != DW_DLV_OK))
		{
			return false;
		}

		Dwarf_Die SpecDie;
		if (UNLIKELY(dwarf_offdie(DebugInfo, Offset, &SpecDie, &ErrorInfo) != DW_DLV_OK))
		{
			return false;
		}

		return FindNameAttributeInDIE(SpecDie, OutFuncName);
	}

	/**
	 * Finds a function name in DWARF DIE (Debug Information Entry) and its children.
	 * For more info on DWARF format, see http://www.dwarfstd.org/Download.php , http://www.ibm.com/developerworks/library/os-debugging/
	 * Note: that function is not exactly traversing the tree, but this "seems to work"(tm). Not sure if we need to descend properly (taking child of every sibling), this
	 * takes too much time (and callstacks seem to be fine without it).
	 */
	void LinuxBacktraceSymbols::FindFunctionNameInDIEAndChildren(Dwarf_Debug DebugInfo, Dwarf_Die Die, Dwarf_Addr Addr, const char **OutFuncName)
	{
		if (OutFuncName == NULL || *OutFuncName != NULL)
		{
			return;
		}

		// search for this Die
		if (FindFunctionNameInDIE(DebugInfo, Die, Addr, OutFuncName))
		{
			return;
		}

		Dwarf_Die PrevChild = Die, Current = NULL;
		Dwarf_Error ErrorInfo;

		int32 MaxChildrenAllowed = 32 * 1024 * 1024;	// safeguard to make sure we never get into an infinite loop
		for(;;)
		{
			if (--MaxChildrenAllowed <= 0)
			{
				fprintf(stderr, "Breaking out from what seems to be an infinite loop during DWARF parsing (too many children).\n");
				return;
			}

			// Get the child
			if (dwarf_child(PrevChild, &Current, &ErrorInfo) != DW_DLV_OK)
			{
				return;	// bail out
			}

			PrevChild = Current;

			// look for in the child
			if (FindFunctionNameInDIE(DebugInfo, Current, Addr, OutFuncName))
			{
				return;	// got the function name!
			}

			// search among child's siblings
			int32 MaxSiblingsAllowed = 64 * 1024 * 1024;	// safeguard to make sure we never get into an infinite loop
			for(;;)
			{
				if (--MaxSiblingsAllowed <= 0)
				{
					fprintf(stderr, "Breaking out from what seems to be an infinite loop during DWARF parsing (too many siblings).\n");
					break;
				}

				Dwarf_Die Prev = Current;
				if (dwarf_siblingof(DebugInfo, Prev, &Current, &ErrorInfo) != DW_DLV_OK || Current == NULL)
				{
					break;
				}

				if (FindFunctionNameInDIE(DebugInfo, Current, Addr, OutFuncName))
				{
					return;	// got the function name!
				}
			}
		};
	}

	/**
	 * Finds mangled name and returns it in internal buffer.
	 * Caller doesn't have to deallocate that.
	 */
	const char * GetMangledName(const char * SourceInfo)
	{
		static char MangledName[MaxMangledNameLength + 1];
		const char * Current = SourceInfo;

		MangledName[0] = 0;
		if (Current == NULL)
		{
			return MangledName;
		}

		// find '('
		for (; *Current != 0 && *Current != '('; ++Current);

		// if unable to find, return original
		if (*Current == 0)
		{
			return SourceInfo;
		}

		// copy everything until '+'
		++Current;
		size_t BufferIdx = 0;
		for (; *Current != 0 && *Current != '+' && BufferIdx < MaxMangledNameLength; ++Current, ++BufferIdx)
		{
			MangledName[BufferIdx] = *Current;
		}

		// if unable to find, return original
		if (*Current == 0)
		{
			return SourceInfo;
		}

		MangledName[BufferIdx] = 0;
		return MangledName;
	}

	/**
	 * Returns source filename for particular callstack depth (or NULL).
	 * Caller doesn't have to deallocate that.
	 */
	const char * GetFunctionName(FGenericCrashContext* Context, int32 CurrentCallDepth)
	{
		static char DemangledName[MaxDemangledNameLength + 1];
		if (Context == NULL || CurrentCallDepth < 0)
		{
			return NULL;
		}

		FLinuxCrashContext* LinuxContext = static_cast< FLinuxCrashContext* >( Context );

		if (LinuxContext->BacktraceSymbols == NULL)
		{
			return NULL;
		}

		const char * SourceInfo = LinuxContext->BacktraceSymbols[CurrentCallDepth];
		if (SourceInfo == NULL)
		{
			return NULL;
		}

		// see http://gcc.gnu.org/onlinedocs/libstdc++/libstdc++-html-USERS-4.3/a01696.html for C++ demangling
		int DemangleStatus = 0xBAD;
		char * Demangled = abi::__cxa_demangle(GetMangledName( SourceInfo ), NULL, NULL, &DemangleStatus);
		if (Demangled != NULL && DemangleStatus == 0)
		{
			FCStringAnsi::Strncpy(DemangledName, Demangled, ARRAY_COUNT(DemangledName) - 1);
		}
		else
		{
			FCStringAnsi::Strncpy(DemangledName, SourceInfo, ARRAY_COUNT(DemangledName) - 1);
		}

		if (Demangled)
		{
			free(Demangled);
		}
		return DemangledName;
	}

	void AppendToString(ANSICHAR * HumanReadableString, SIZE_T HumanReadableStringSize, FGenericCrashContext * Context, const ANSICHAR * Text)
	{
		FCStringAnsi::Strcat(HumanReadableString, HumanReadableStringSize, Text);
	}

	void AppendFunctionNameIfAny(FLinuxCrashContext & LinuxContext, const char * FunctionName, uint64 ProgramCounter)
	{
		if (FunctionName)
		{
			FCStringAnsi::Strcat(LinuxContext.MinidumpCallstackInfo, ARRAY_COUNT( LinuxContext.MinidumpCallstackInfo ) - 1, FunctionName);
			FCStringAnsi::Strcat(LinuxContext.MinidumpCallstackInfo, ARRAY_COUNT( LinuxContext.MinidumpCallstackInfo ) - 1, " + some bytes");	// this is just to conform to crashreporterue4 standard
		}
		else
		{
			ANSICHAR TempArray[MAX_SPRINTF];

			if (PLATFORM_64BITS)
			{
				FCStringAnsi::Sprintf(TempArray, "0x%016llx", ProgramCounter);
			}
			else
			{
				FCStringAnsi::Sprintf(TempArray, "0x%08x", (uint32)ProgramCounter);
			}
			FCStringAnsi::Strcat(LinuxContext.MinidumpCallstackInfo, ARRAY_COUNT( LinuxContext.MinidumpCallstackInfo ) - 1, TempArray);
		}
	}

	LinuxBacktraceSymbols *GetBacktraceSymbols()
	{
		static LinuxBacktraceSymbols Symbols;
		Symbols.Init();
		return &Symbols;
	}
}

void FLinuxPlatformStackWalk::ProgramCounterToSymbolInfo( uint64 ProgramCounter, FProgramCounterSymbolInfo& out_SymbolInfo )
{
	// Set the program counter.
	out_SymbolInfo.ProgramCounter = ProgramCounter;

	// Get function, filename and line number.
	const char* ModuleName = nullptr;
	const char* FunctionName = nullptr;
	const char* SourceFilename = nullptr;
	int LineNumber = 0;

	if (LinuxStackWalkHelpers::GetBacktraceSymbols()->GetInfoForAddress(reinterpret_cast<void*>(ProgramCounter), &ModuleName, &FunctionName, &SourceFilename, &LineNumber))
	{
		out_SymbolInfo.LineNumber = LineNumber;

		if (LIKELY(ModuleName != nullptr))
		{
			FCStringAnsi::Strcpy(out_SymbolInfo.ModuleName, sizeof(out_SymbolInfo.ModuleName), ModuleName);
		}

		if (LIKELY(SourceFilename != nullptr))
		{
			FCStringAnsi::Strcpy(out_SymbolInfo.Filename, sizeof(out_SymbolInfo.Filename), SourceFilename);
		}

		if (FunctionName != nullptr)
		{
			FCStringAnsi::Strcpy(out_SymbolInfo.FunctionName, sizeof(out_SymbolInfo.Filename), FunctionName);
		}
		else
		{
			sprintf(out_SymbolInfo.FunctionName, "0x%016llx", ProgramCounter);
		}
	}
}

bool FLinuxPlatformStackWalk::ProgramCounterToHumanReadableString( int32 CurrentCallDepth, uint64 ProgramCounter, ANSICHAR* HumanReadableString, SIZE_T HumanReadableStringSize, FGenericCrashContext* Context )
{
	if (HumanReadableString && HumanReadableStringSize > 0)
	{
		ANSICHAR TempArray[MAX_SPRINTF];
		if (CurrentCallDepth < 0)
		{
			if (PLATFORM_64BITS)
			{
				FCStringAnsi::Sprintf(TempArray, "[Callstack] 0x%016llx ", ProgramCounter);
			}
			else
			{
				FCStringAnsi::Sprintf(TempArray, "[Callstack] 0x%08x ", (uint32) ProgramCounter);
			}
			LinuxStackWalkHelpers::AppendToString(HumanReadableString, HumanReadableStringSize, Context, TempArray);

			// won't be able to display names here
		}
		else
		{
			if (PLATFORM_64BITS)
			{
				FCStringAnsi::Sprintf(TempArray, "[Callstack]  %02d  0x%016llx  ", CurrentCallDepth, ProgramCounter);
			}
			else
			{
				FCStringAnsi::Sprintf(TempArray, "[Callstack]  %02d  0x%08x  ", CurrentCallDepth, (uint32) ProgramCounter);
			}
			LinuxStackWalkHelpers::AppendToString(HumanReadableString, HumanReadableStringSize, Context, TempArray);

			// Get filename, source file and line number
			FLinuxCrashContext* LinuxContext = static_cast< FLinuxCrashContext* >( Context );
			if (LinuxContext)
			{
				const char * ModuleName = nullptr;
				const char * FunctionName = nullptr;
				const char * SourceFilename = nullptr;
				int LineNumber;

				// for ensure, use the fast path - do not even attempt to get detailed info as it will result in long hitch
				bool bAddDetailedInfo = !LinuxContext->GetIsEnsure();

				// attempt to get the said detailed info
				bAddDetailedInfo = bAddDetailedInfo && LinuxStackWalkHelpers::GetBacktraceSymbols()->GetInfoForAddress(reinterpret_cast<void*>(ProgramCounter), &ModuleName, &FunctionName, &SourceFilename, &LineNumber);

				if (bAddDetailedInfo)
				{
					// append FunctionName() [Source.cpp, line X] to HumanReadableString
					LinuxStackWalkHelpers::AppendToString(HumanReadableString, HumanReadableStringSize, Context, FunctionName);
					FCStringAnsi::Sprintf(TempArray, " [%s, line %d]", SourceFilename, LineNumber);
					LinuxStackWalkHelpers::AppendToString(HumanReadableString, HumanReadableStringSize, Context, TempArray);

					// append Module!FunctioName [Source.cpp:X] to MinidumpCallstackInfo
					FCStringAnsi::Strcat(LinuxContext->MinidumpCallstackInfo, ARRAY_COUNT( LinuxContext->MinidumpCallstackInfo ) - 1, ModuleName);
					FCStringAnsi::Strcat(LinuxContext->MinidumpCallstackInfo, ARRAY_COUNT( LinuxContext->MinidumpCallstackInfo ) - 1, "!");
					LinuxStackWalkHelpers::AppendFunctionNameIfAny(*LinuxContext, FunctionName, ProgramCounter);
					FCStringAnsi::Sprintf(TempArray, " [%s:%d]", SourceFilename, LineNumber);
					FCStringAnsi::Strcat(LinuxContext->MinidumpCallstackInfo, ARRAY_COUNT( LinuxContext->MinidumpCallstackInfo ) - 1, TempArray);
				}
				else
				{
					// get the function name for backtrace, may be incorrect
					FunctionName = LinuxStackWalkHelpers::GetFunctionName(Context, CurrentCallDepth);
					if (FunctionName)
					{
						LinuxStackWalkHelpers::AppendToString(HumanReadableString, HumanReadableStringSize, Context, FunctionName);
					}

					FCStringAnsi::Strcat(LinuxContext->MinidumpCallstackInfo, ARRAY_COUNT( LinuxContext->MinidumpCallstackInfo ) - 1, "Unknown!");
					LinuxStackWalkHelpers::AppendFunctionNameIfAny(*LinuxContext, FunctionName, ProgramCounter);
				}

				FCStringAnsi::Strcat(LinuxContext->MinidumpCallstackInfo, ARRAY_COUNT( LinuxContext->MinidumpCallstackInfo ) - 1, "\r\n");	// this one always uses Windows line terminators
			}
		}
		return true;
	}
	return true;
}

void FLinuxPlatformStackWalk::StackWalkAndDump( ANSICHAR* HumanReadableString, SIZE_T HumanReadableStringSize, int32 IgnoreCount, void* Context )
{
	if (Context == nullptr)
	{
		FLinuxCrashContext CrashContext;
		CrashContext.InitFromSignal(0, nullptr, nullptr);
		FGenericPlatformStackWalk::StackWalkAndDump(HumanReadableString, HumanReadableStringSize, IgnoreCount, &CrashContext);
	}
	else
	{
		FGenericPlatformStackWalk::StackWalkAndDump(HumanReadableString, HumanReadableStringSize, IgnoreCount, Context);
	}
}

void FLinuxPlatformStackWalk::StackWalkAndDumpEx(ANSICHAR* HumanReadableString, SIZE_T HumanReadableStringSize, int32 IgnoreCount, uint32 Flags, void* Context)
{
	const bool bHandlingEnsure = (Flags & EStackWalkFlags::FlagsUsedWhenHandlingEnsure) == EStackWalkFlags::FlagsUsedWhenHandlingEnsure;
	if (Context == nullptr)
	{
		FLinuxCrashContext CrashContext(bHandlingEnsure);
		CrashContext.InitFromSignal(0, nullptr, nullptr);
		FPlatformStackWalk::StackWalkAndDump(HumanReadableString, HumanReadableStringSize, IgnoreCount, &CrashContext);
	}
	else
	{
		/** Helper sets the ensure value in the context and guarantees it gets reset afterwards (even if an exception is thrown) */
		struct FLocalGuardHelper
		{
			FLocalGuardHelper(FLinuxCrashContext* InContext, bool bNewEnsureValue)
				: Context(InContext), bOldEnsureValue(Context->GetIsEnsure())
			{
				Context->SetIsEnsure(bNewEnsureValue);
			}
			~FLocalGuardHelper()
			{
				Context->SetIsEnsure(bOldEnsureValue);
			}

		private:
			FLinuxCrashContext* Context;
			bool bOldEnsureValue;
		};

		FLocalGuardHelper Guard(reinterpret_cast<FLinuxCrashContext*>(Context), bHandlingEnsure);
		FPlatformStackWalk::StackWalkAndDump(HumanReadableString, HumanReadableStringSize, IgnoreCount, Context);
	}
}

void FLinuxPlatformStackWalk::CaptureStackBackTrace( uint64* BackTrace, uint32 MaxDepth, void* Context )
{
	// Make sure we have place to store the information before we go through the process of raising
	// an exception and handling it.
	if( BackTrace == NULL || MaxDepth == 0 )
	{
		return;
	}

	size_t size = backtrace(reinterpret_cast< void** >( BackTrace ), MaxDepth);

	if (Context)
	{
		FLinuxCrashContext* LinuxContext = reinterpret_cast< FLinuxCrashContext* >( Context );

		if (LinuxContext->BacktraceSymbols == NULL)
		{
			// #CrashReport: 2014-09-29 Replace with backtrace_symbols_fd due to malloc()
			LinuxContext->BacktraceSymbols = backtrace_symbols(reinterpret_cast< void** >( BackTrace ), MaxDepth);
		}
	}
}

static FCriticalSection EnsureLock;
static bool bReentranceGuard = false;

void NewReportEnsure(const TCHAR* ErrorMessage)
{
	// Simple re-entrance guard.
	EnsureLock.Lock();

	if (bReentranceGuard)
	{
		EnsureLock.Unlock();
		return;
	}

	bReentranceGuard = true;

	const bool bIsEnsure = true;
	FLinuxCrashContext EnsureContext(bIsEnsure);
	EnsureContext.InitFromEnsureHandler(ErrorMessage, __builtin_return_address(0));

	EnsureContext.CaptureStackTrace();
	EnsureContext.GenerateCrashInfoAndLaunchReporter(true);

	bReentranceGuard = false;
	EnsureLock.Unlock();
}
