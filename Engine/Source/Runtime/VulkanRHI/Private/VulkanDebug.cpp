// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VulkanDebug.cpp: Vulkan device RHI implementation.
=============================================================================*/

#include "VulkanRHIPrivate.h"

#if VK_HEADER_VERSION < 8 && (VK_API_VERSION < VK_MAKE_VERSION(1, 0, 3))
#include <vulkan/vk_ext_debug_report.h>
#endif

#define VULKAN_ENABLE_API_DUMP_DETAILED				0

#define CREATE_MSG_CALLBACK							"vkCreateDebugReportCallbackEXT"
#define DESTROY_MSG_CALLBACK						"vkDestroyDebugReportCallbackEXT"

DEFINE_LOG_CATEGORY(LogVulkanRHI);

#if VULKAN_HAS_DEBUGGING_ENABLED

static VkBool32 VKAPI_PTR DebugReportFunction(
	VkDebugReportFlagsEXT			MsgFlags,
	VkDebugReportObjectTypeEXT		ObjType,
    uint64_t						SrcObject,
    size_t							Location,
    int32							MsgCode,
	const ANSICHAR*					LayerPrefix,
	const ANSICHAR*					Msg,
    void*							UserData)
{
	if (MsgFlags != VK_DEBUG_REPORT_ERROR_BIT_EXT && 
		MsgFlags != VK_DEBUG_REPORT_WARNING_BIT_EXT &&
		MsgFlags != VK_DEBUG_REPORT_INFORMATION_BIT_EXT &&
		MsgFlags != VK_DEBUG_REPORT_DEBUG_BIT_EXT)
	{
		ensure(0);
	}

	if (MsgFlags & VK_DEBUG_REPORT_ERROR_BIT_EXT)
	{
		// Reaching this line should trigger a break/assert. 
		// Check to see if this is a code we've seen before.
		FString LayerCode = FString::Printf(TEXT("%s%d"), ANSI_TO_TCHAR(LayerPrefix), MsgCode);
		static TSet<FString> SeenCodes;
		if (!SeenCodes.Contains(LayerCode))
		{
#if VULKAN_ENABLE_DUMP_LAYER
			VulkanRHI::FlushDebugWrapperLog();
#endif
			FString Message = FString::Printf(TEXT("VK ERROR: [%s] Code %d : %s"), ANSI_TO_TCHAR(LayerPrefix), MsgCode, ANSI_TO_TCHAR(Msg));
			FPlatformMisc::LowLevelOutputDebugStringf(TEXT("VulkanRHI: %s\n"), *Message);
			UE_LOG(LogVulkanRHI, Error, TEXT("%s"), *Message);
    
#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
			//if (!FCStringAnsi::Strcmp(LayerPrefix, "MEM"))
			{
				//((FVulkanDynamicRHI*)GDynamicRHI)->DumpMemory();
			}
#endif

			// Break debugger on first instance of each message. 
			// Continuing will ignore the error and suppress this message in future.
			bool bIgnoreInFuture = true;
			ensureAlways(0);
			if (bIgnoreInFuture)
			{
				SeenCodes.Add(LayerCode);
			}
		}
		return VK_FALSE;
	}
	else if (MsgFlags & VK_DEBUG_REPORT_WARNING_BIT_EXT)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		VulkanRHI::FlushDebugWrapperLog();
#endif
		FString Message = FString::Printf(TEXT("VK WARNING: [%s] Code %d : %s\n"), ANSI_TO_TCHAR(LayerPrefix), MsgCode, ANSI_TO_TCHAR(Msg));
		FPlatformMisc::LowLevelOutputDebugStringf(TEXT("VulkanRHI: %s\n"), *Message);
		UE_LOG(LogVulkanRHI, Warning, TEXT("%s"), *Message);
		return VK_FALSE;
	}
#if VULKAN_ENABLE_API_DUMP
	else if (MsgFlags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT)
	{
#if !VULKAN_ENABLE_API_DUMP_DETAILED
		if (!FCStringAnsi::Strcmp(LayerPrefix, "MEM") || !FCStringAnsi::Strcmp(LayerPrefix, "DS"))
		{
			// Skip Mem messages
		}
		else
#endif
		{
			FString Message = FString::Printf(TEXT("VK INFO: [%s] Code %d : %s\n"), ANSI_TO_TCHAR(LayerPrefix), MsgCode, ANSI_TO_TCHAR(Msg));
			FPlatformMisc::LowLevelOutputDebugStringf(TEXT("VulkanRHI: %s\n"), *Message);
			UE_LOG(LogVulkanRHI, Display, TEXT("%s"), *Message);
		}

		return VK_FALSE;
	}
#if VULKAN_ENABLE_API_DUMP_DETAILED
	else if (MsgFlags & VK_DEBUG_REPORT_DEBUG_BIT_EXT)
	{
		FString Message = FString::Printf(TEXT("VK DEBUG: [%s] Code %d : %s\n"), ANSI_TO_TCHAR(LayerPrefix), MsgCode, ANSI_TO_TCHAR(Msg));
		FPlatformMisc::LowLevelOutputDebugStringf(TEXT("VulkanRHI: %s\n"), *Message);
		UE_LOG(LogVulkanRHI, Display, TEXT("%s"), *Message);
		return VK_FALSE;
	}
#endif
#endif

	return VK_TRUE;
}

void FVulkanDynamicRHI::SetupDebugLayerCallback()
{
#if !VULKAN_DISABLE_DEBUG_CALLBACK
	PFN_vkCreateDebugReportCallbackEXT CreateMsgCallback = (PFN_vkCreateDebugReportCallbackEXT)(void*)VulkanRHI::vkGetInstanceProcAddr(Instance, CREATE_MSG_CALLBACK);
	if (CreateMsgCallback)
	{
		VkDebugReportCallbackCreateInfoEXT CreateInfo;
		FMemory::Memzero(CreateInfo);
		CreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT;
		CreateInfo.pfnCallback = DebugReportFunction;
		CreateInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
#if VULKAN_ENABLE_API_DUMP
		CreateInfo.flags |= VK_DEBUG_REPORT_INFORMATION_BIT_EXT;
#if VULKAN_ENABLE_API_DUMP_DETAILED
		CreateInfo.flags |= VK_DEBUG_REPORT_DEBUG_BIT_EXT;
#endif
#endif
		VkResult Result = CreateMsgCallback(Instance, &CreateInfo, nullptr, &MsgCallback);
		switch (Result)
		{
		case VK_SUCCESS:
			break;
		case VK_ERROR_OUT_OF_HOST_MEMORY:
			UE_LOG(LogVulkanRHI, Warning, TEXT("CreateMsgCallback: out of host memory/CreateMsgCallback Failure; debug reporting skipped"));
			break;
		default:
			UE_LOG(LogVulkanRHI, Warning, TEXT("CreateMsgCallback: unknown failure %d/CreateMsgCallback Failure; debug reporting skipped"), (int32)Result);
			break;
		}
	}
	else
	{
		UE_LOG(LogVulkanRHI, Warning, TEXT("GetProcAddr: Unable to find vkDbgCreateMsgCallback/vkGetInstanceProcAddr; debug reporting skipped!"));
	}
#endif
}

void FVulkanDynamicRHI::RemoveDebugLayerCallback()
{
#if !VULKAN_DISABLE_DEBUG_CALLBACK
	if (MsgCallback != VK_NULL_HANDLE)
	{
		PFN_vkDestroyDebugReportCallbackEXT DestroyMsgCallback = (PFN_vkDestroyDebugReportCallbackEXT)(void*)VulkanRHI::vkGetInstanceProcAddr(Instance, DESTROY_MSG_CALLBACK);
		checkf(DestroyMsgCallback, TEXT("GetProcAddr: Unable to find vkDbgCreateMsgCallback\vkGetInstanceProcAddr Failure"));
		DestroyMsgCallback(Instance, MsgCallback, nullptr);
	}
#endif
}


#if VULKAN_ENABLE_DUMP_LAYER
namespace VulkanRHI
{
	static FString DebugLog;
	static int32 DebugLine = 1;

	static const TCHAR* Tabs = TEXT("\t\t\t\t\t\t\t\t\t");

	void FlushDebugWrapperLog()
	{
		if (DebugLog.Len() > 0)
		{
			GLog->Flush();
			UE_LOG(LogVulkanRHI, Display, TEXT("Vulkan Wrapper Log:\n%s"), *DebugLog);
			GLog->Flush();
			DebugLog = TEXT("");
		}
	}

	static void HandleFlushWrapperLog(const TArray<FString>& Args)
	{
		FlushDebugWrapperLog();
	}

	static FAutoConsoleCommand CVarVulkanFlushLog(
		TEXT("r.Vulkan.FlushLog"),
		TEXT("\n"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&HandleFlushWrapperLog)
		);

	static TAutoConsoleVariable<int32> CVarVulkanDumpLayer(
		TEXT("r.Vulkan.DumpLayer"),
		0,
		TEXT("1 to enable dump layer, 0 to disable (default)")
		);

	static FString GetVkFormatString(VkFormat Format)
	{
		switch (Format)
		{
			// + 10 to skip "VK_FORMAT"
#define VKSWITCHCASE(x)	case x: return TEXT(#x) + 10;
			VKSWITCHCASE(VK_FORMAT_UNDEFINED)
			VKSWITCHCASE(VK_FORMAT_R4G4_UNORM_PACK8)
			VKSWITCHCASE(VK_FORMAT_R4G4B4A4_UNORM_PACK16)
			VKSWITCHCASE(VK_FORMAT_B4G4R4A4_UNORM_PACK16)
			VKSWITCHCASE(VK_FORMAT_R5G6B5_UNORM_PACK16)
			VKSWITCHCASE(VK_FORMAT_B5G6R5_UNORM_PACK16)
			VKSWITCHCASE(VK_FORMAT_R5G5B5A1_UNORM_PACK16)
			VKSWITCHCASE(VK_FORMAT_B5G5R5A1_UNORM_PACK16)
			VKSWITCHCASE(VK_FORMAT_A1R5G5B5_UNORM_PACK16)
			VKSWITCHCASE(VK_FORMAT_R8_UNORM)
			VKSWITCHCASE(VK_FORMAT_R8_SNORM)
			VKSWITCHCASE(VK_FORMAT_R8_USCALED)
			VKSWITCHCASE(VK_FORMAT_R8_SSCALED)
			VKSWITCHCASE(VK_FORMAT_R8_UINT)
			VKSWITCHCASE(VK_FORMAT_R8_SINT)
			VKSWITCHCASE(VK_FORMAT_R8_SRGB)
			VKSWITCHCASE(VK_FORMAT_R8G8_UNORM)
			VKSWITCHCASE(VK_FORMAT_R8G8_SNORM)
			VKSWITCHCASE(VK_FORMAT_R8G8_USCALED)
			VKSWITCHCASE(VK_FORMAT_R8G8_SSCALED)
			VKSWITCHCASE(VK_FORMAT_R8G8_UINT)
			VKSWITCHCASE(VK_FORMAT_R8G8_SINT)
			VKSWITCHCASE(VK_FORMAT_R8G8_SRGB)
			VKSWITCHCASE(VK_FORMAT_R8G8B8_UNORM)
			VKSWITCHCASE(VK_FORMAT_R8G8B8_SNORM)
			VKSWITCHCASE(VK_FORMAT_R8G8B8_USCALED)
			VKSWITCHCASE(VK_FORMAT_R8G8B8_SSCALED)
			VKSWITCHCASE(VK_FORMAT_R8G8B8_UINT)
			VKSWITCHCASE(VK_FORMAT_R8G8B8_SINT)
			VKSWITCHCASE(VK_FORMAT_R8G8B8_SRGB)
			VKSWITCHCASE(VK_FORMAT_B8G8R8_UNORM)
			VKSWITCHCASE(VK_FORMAT_B8G8R8_SNORM)
			VKSWITCHCASE(VK_FORMAT_B8G8R8_USCALED)
			VKSWITCHCASE(VK_FORMAT_B8G8R8_SSCALED)
			VKSWITCHCASE(VK_FORMAT_B8G8R8_UINT)
			VKSWITCHCASE(VK_FORMAT_B8G8R8_SINT)
			VKSWITCHCASE(VK_FORMAT_B8G8R8_SRGB)
			VKSWITCHCASE(VK_FORMAT_R8G8B8A8_UNORM)
			VKSWITCHCASE(VK_FORMAT_R8G8B8A8_SNORM)
			VKSWITCHCASE(VK_FORMAT_R8G8B8A8_USCALED)
			VKSWITCHCASE(VK_FORMAT_R8G8B8A8_SSCALED)
			VKSWITCHCASE(VK_FORMAT_R8G8B8A8_UINT)
			VKSWITCHCASE(VK_FORMAT_R8G8B8A8_SINT)
			VKSWITCHCASE(VK_FORMAT_R8G8B8A8_SRGB)
			VKSWITCHCASE(VK_FORMAT_B8G8R8A8_UNORM)
			VKSWITCHCASE(VK_FORMAT_B8G8R8A8_SNORM)
			VKSWITCHCASE(VK_FORMAT_B8G8R8A8_USCALED)
			VKSWITCHCASE(VK_FORMAT_B8G8R8A8_SSCALED)
			VKSWITCHCASE(VK_FORMAT_B8G8R8A8_UINT)
			VKSWITCHCASE(VK_FORMAT_B8G8R8A8_SINT)
			VKSWITCHCASE(VK_FORMAT_B8G8R8A8_SRGB)
			VKSWITCHCASE(VK_FORMAT_A8B8G8R8_UNORM_PACK32)
			VKSWITCHCASE(VK_FORMAT_A8B8G8R8_SNORM_PACK32)
			VKSWITCHCASE(VK_FORMAT_A8B8G8R8_USCALED_PACK32)
			VKSWITCHCASE(VK_FORMAT_A8B8G8R8_SSCALED_PACK32)
			VKSWITCHCASE(VK_FORMAT_A8B8G8R8_UINT_PACK32)
			VKSWITCHCASE(VK_FORMAT_A8B8G8R8_SINT_PACK32)
			VKSWITCHCASE(VK_FORMAT_A8B8G8R8_SRGB_PACK32)
			VKSWITCHCASE(VK_FORMAT_A2R10G10B10_UNORM_PACK32)
			VKSWITCHCASE(VK_FORMAT_A2R10G10B10_SNORM_PACK32)
			VKSWITCHCASE(VK_FORMAT_A2R10G10B10_USCALED_PACK32)
			VKSWITCHCASE(VK_FORMAT_A2R10G10B10_SSCALED_PACK32)
			VKSWITCHCASE(VK_FORMAT_A2R10G10B10_UINT_PACK32)
			VKSWITCHCASE(VK_FORMAT_A2R10G10B10_SINT_PACK32)
			VKSWITCHCASE(VK_FORMAT_A2B10G10R10_UNORM_PACK32)
			VKSWITCHCASE(VK_FORMAT_A2B10G10R10_SNORM_PACK32)
			VKSWITCHCASE(VK_FORMAT_A2B10G10R10_USCALED_PACK32)
			VKSWITCHCASE(VK_FORMAT_A2B10G10R10_SSCALED_PACK32)
			VKSWITCHCASE(VK_FORMAT_A2B10G10R10_UINT_PACK32)
			VKSWITCHCASE(VK_FORMAT_A2B10G10R10_SINT_PACK32)
			VKSWITCHCASE(VK_FORMAT_R16_UNORM)
			VKSWITCHCASE(VK_FORMAT_R16_SNORM)
			VKSWITCHCASE(VK_FORMAT_R16_USCALED)
			VKSWITCHCASE(VK_FORMAT_R16_SSCALED)
			VKSWITCHCASE(VK_FORMAT_R16_UINT)
			VKSWITCHCASE(VK_FORMAT_R16_SINT)
			VKSWITCHCASE(VK_FORMAT_R16_SFLOAT)
			VKSWITCHCASE(VK_FORMAT_R16G16_UNORM)
			VKSWITCHCASE(VK_FORMAT_R16G16_SNORM)
			VKSWITCHCASE(VK_FORMAT_R16G16_USCALED)
			VKSWITCHCASE(VK_FORMAT_R16G16_SSCALED)
			VKSWITCHCASE(VK_FORMAT_R16G16_UINT)
			VKSWITCHCASE(VK_FORMAT_R16G16_SINT)
			VKSWITCHCASE(VK_FORMAT_R16G16_SFLOAT)
			VKSWITCHCASE(VK_FORMAT_R16G16B16_UNORM)
			VKSWITCHCASE(VK_FORMAT_R16G16B16_SNORM)
			VKSWITCHCASE(VK_FORMAT_R16G16B16_USCALED)
			VKSWITCHCASE(VK_FORMAT_R16G16B16_SSCALED)
			VKSWITCHCASE(VK_FORMAT_R16G16B16_UINT)
			VKSWITCHCASE(VK_FORMAT_R16G16B16_SINT)
			VKSWITCHCASE(VK_FORMAT_R16G16B16_SFLOAT)
			VKSWITCHCASE(VK_FORMAT_R16G16B16A16_UNORM)
			VKSWITCHCASE(VK_FORMAT_R16G16B16A16_SNORM)
			VKSWITCHCASE(VK_FORMAT_R16G16B16A16_USCALED)
			VKSWITCHCASE(VK_FORMAT_R16G16B16A16_SSCALED)
			VKSWITCHCASE(VK_FORMAT_R16G16B16A16_UINT)
			VKSWITCHCASE(VK_FORMAT_R16G16B16A16_SINT)
			VKSWITCHCASE(VK_FORMAT_R16G16B16A16_SFLOAT)
			VKSWITCHCASE(VK_FORMAT_R32_UINT)
			VKSWITCHCASE(VK_FORMAT_R32_SINT)
			VKSWITCHCASE(VK_FORMAT_R32_SFLOAT)
			VKSWITCHCASE(VK_FORMAT_R32G32_UINT)
			VKSWITCHCASE(VK_FORMAT_R32G32_SINT)
			VKSWITCHCASE(VK_FORMAT_R32G32_SFLOAT)
			VKSWITCHCASE(VK_FORMAT_R32G32B32_UINT)
			VKSWITCHCASE(VK_FORMAT_R32G32B32_SINT)
			VKSWITCHCASE(VK_FORMAT_R32G32B32_SFLOAT)
			VKSWITCHCASE(VK_FORMAT_R32G32B32A32_UINT)
			VKSWITCHCASE(VK_FORMAT_R32G32B32A32_SINT)
			VKSWITCHCASE(VK_FORMAT_R32G32B32A32_SFLOAT)
			VKSWITCHCASE(VK_FORMAT_R64_UINT)
			VKSWITCHCASE(VK_FORMAT_R64_SINT)
			VKSWITCHCASE(VK_FORMAT_R64_SFLOAT)
			VKSWITCHCASE(VK_FORMAT_R64G64_UINT)
			VKSWITCHCASE(VK_FORMAT_R64G64_SINT)
			VKSWITCHCASE(VK_FORMAT_R64G64_SFLOAT)
			VKSWITCHCASE(VK_FORMAT_R64G64B64_UINT)
			VKSWITCHCASE(VK_FORMAT_R64G64B64_SINT)
			VKSWITCHCASE(VK_FORMAT_R64G64B64_SFLOAT)
			VKSWITCHCASE(VK_FORMAT_R64G64B64A64_UINT)
			VKSWITCHCASE(VK_FORMAT_R64G64B64A64_SINT)
			VKSWITCHCASE(VK_FORMAT_R64G64B64A64_SFLOAT)
			VKSWITCHCASE(VK_FORMAT_B10G11R11_UFLOAT_PACK32)
			VKSWITCHCASE(VK_FORMAT_E5B9G9R9_UFLOAT_PACK32)
			VKSWITCHCASE(VK_FORMAT_D16_UNORM)
			VKSWITCHCASE(VK_FORMAT_X8_D24_UNORM_PACK32)
			VKSWITCHCASE(VK_FORMAT_D32_SFLOAT)
			VKSWITCHCASE(VK_FORMAT_S8_UINT)
			VKSWITCHCASE(VK_FORMAT_D16_UNORM_S8_UINT)
			VKSWITCHCASE(VK_FORMAT_D24_UNORM_S8_UINT)
			VKSWITCHCASE(VK_FORMAT_D32_SFLOAT_S8_UINT)
			VKSWITCHCASE(VK_FORMAT_BC1_RGB_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_BC1_RGB_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_BC1_RGBA_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_BC1_RGBA_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_BC2_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_BC2_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_BC3_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_BC3_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_BC4_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_BC4_SNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_BC5_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_BC5_SNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_BC6H_UFLOAT_BLOCK)
			VKSWITCHCASE(VK_FORMAT_BC6H_SFLOAT_BLOCK)
			VKSWITCHCASE(VK_FORMAT_BC7_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_BC7_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_EAC_R11_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_EAC_R11_SNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_EAC_R11G11_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_EAC_R11G11_SNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_4x4_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_4x4_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_5x4_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_5x4_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_5x5_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_5x5_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_6x5_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_6x5_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_6x6_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_6x6_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_8x5_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_8x5_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_8x6_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_8x6_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_8x8_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_8x8_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_10x5_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_10x5_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_10x6_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_10x6_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_10x8_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_10x8_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_10x10_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_10x10_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_12x10_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_12x10_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_12x12_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_12x12_SRGB_BLOCK)
#undef VKSWITCHCASE
		default:
			break;
		}
		return FString::Printf(TEXT("Unknown VkFormat %d"), (int32)Format);
	}

	static FString GetVkResultErrorString(VkResult Result)
	{
		switch (Result)
		{
			// + 3 to skip "VK_"
#define VKSWITCHCASE(x)	case x: return TEXT(#x) + 3;
			VKSWITCHCASE(VK_SUCCESS)
			VKSWITCHCASE(VK_NOT_READY)
			VKSWITCHCASE(VK_TIMEOUT)
			VKSWITCHCASE(VK_EVENT_SET)
			VKSWITCHCASE(VK_EVENT_RESET)
			VKSWITCHCASE(VK_INCOMPLETE)
			VKSWITCHCASE(VK_ERROR_OUT_OF_HOST_MEMORY)
			VKSWITCHCASE(VK_ERROR_OUT_OF_DEVICE_MEMORY)
			VKSWITCHCASE(VK_ERROR_INITIALIZATION_FAILED)
			VKSWITCHCASE(VK_ERROR_DEVICE_LOST)
			VKSWITCHCASE(VK_ERROR_MEMORY_MAP_FAILED)
			VKSWITCHCASE(VK_ERROR_LAYER_NOT_PRESENT)
			VKSWITCHCASE(VK_ERROR_EXTENSION_NOT_PRESENT)
			VKSWITCHCASE(VK_ERROR_FEATURE_NOT_PRESENT)
			VKSWITCHCASE(VK_ERROR_INCOMPATIBLE_DRIVER)
			VKSWITCHCASE(VK_ERROR_TOO_MANY_OBJECTS)
			VKSWITCHCASE(VK_ERROR_FORMAT_NOT_SUPPORTED)
			VKSWITCHCASE(VK_ERROR_SURFACE_LOST_KHR)
			VKSWITCHCASE(VK_ERROR_NATIVE_WINDOW_IN_USE_KHR)
			VKSWITCHCASE(VK_SUBOPTIMAL_KHR)
			VKSWITCHCASE(VK_ERROR_OUT_OF_DATE_KHR)
			VKSWITCHCASE(VK_ERROR_INCOMPATIBLE_DISPLAY_KHR)
			VKSWITCHCASE(VK_ERROR_VALIDATION_FAILED_EXT)
			VKSWITCHCASE(VK_ERROR_INVALID_SHADER_NV)
#undef VKSWITCHCASE
		default:
			break;
		}

		return FString::Printf(TEXT("Unknown VkResult %d"), (int32)Result);
	}

	FString GetImageTilingString(VkImageTiling Tiling)
	{
		switch (Tiling)
		{
			// + 16 to skip "VK_IMAGE_TILING_"
#define VKSWITCHCASE(x)	case x: return TEXT(#x) + 16;
			VKSWITCHCASE(VK_IMAGE_TILING_OPTIMAL)
			VKSWITCHCASE(VK_IMAGE_TILING_LINEAR)
#undef VKSWITCHCASE
		default:
			break;
		}

		return FString::Printf(TEXT("Unknown VkImageTiling %d"), (int32)Tiling);
	}

	static FString GetImageLayoutString(VkImageLayout Layout)
	{
		switch (Layout)
		{
			// + 16 to skip "VK_IMAGE_LAYOUT"
#define VKSWITCHCASE(x)	case x: return TEXT(#x) + 16;
			VKSWITCHCASE(VK_IMAGE_LAYOUT_UNDEFINED)
			VKSWITCHCASE(VK_IMAGE_LAYOUT_GENERAL)
			VKSWITCHCASE(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
			VKSWITCHCASE(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
			VKSWITCHCASE(VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
			VKSWITCHCASE(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
			VKSWITCHCASE(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
			VKSWITCHCASE(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
			VKSWITCHCASE(VK_IMAGE_LAYOUT_PREINITIALIZED)
			VKSWITCHCASE(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
#undef VKSWITCHCASE
		default:
			break;
		}

		return FString::Printf(TEXT("Unknown VkImageLayout %d"), (int32)Layout);
	}

	static FString GetImageViewTypeString(VkImageViewType Type)
	{
		switch (Type)
		{
			// + 19 to skip "VK_IMAGE_VIEW_TYPE_"
#define VKSWITCHCASE(x)	case x: return TEXT(#x) + 16;
		VKSWITCHCASE(VK_IMAGE_VIEW_TYPE_1D)
		VKSWITCHCASE(VK_IMAGE_VIEW_TYPE_2D)
		VKSWITCHCASE(VK_IMAGE_VIEW_TYPE_3D)
		VKSWITCHCASE(VK_IMAGE_VIEW_TYPE_CUBE)
		VKSWITCHCASE(VK_IMAGE_VIEW_TYPE_1D_ARRAY)
		VKSWITCHCASE(VK_IMAGE_VIEW_TYPE_2D_ARRAY)
		VKSWITCHCASE(VK_IMAGE_VIEW_TYPE_CUBE_ARRAY)
#undef VKSWITCHCASE
		default:
			break;
		}

		return FString::Printf(TEXT("Unknown VkImageViewType %d"), (int32)Type);
	}

	static FString GetComponentMappingString(const VkComponentMapping& Mapping)
	{
		auto GetSwizzle = [](VkComponentSwizzle Swizzle) -> const TCHAR*
			{
				switch (Swizzle)
				{
				case VK_COMPONENT_SWIZZLE_IDENTITY: return TEXT("ID");
				case VK_COMPONENT_SWIZZLE_ZERO:		return TEXT("0");
				case VK_COMPONENT_SWIZZLE_ONE:		return TEXT("1");
				case VK_COMPONENT_SWIZZLE_R:		return TEXT("R");
				case VK_COMPONENT_SWIZZLE_G:		return TEXT("G");
				case VK_COMPONENT_SWIZZLE_B:		return TEXT("B");
				case VK_COMPONENT_SWIZZLE_A:		return TEXT("A");
				default:
					check(0);
					return TEXT("-");
				}
			};
		return FString::Printf(TEXT("(r=%s, g=%s, b=%s, a=%s)"), GetSwizzle(Mapping.r), GetSwizzle(Mapping.g), GetSwizzle(Mapping.b), GetSwizzle(Mapping.a));
	}


#define AppendBitFieldName(BitField, Name) \
	if ((Flags & BitField) == BitField)\
	{\
		Flags &= ~BitField;\
		if (String.Len() > 0)\
		{\
			String += TEXT("|");\
		}\
		String += Name;\
	}

	static FString GetAspectMaskString(VkImageAspectFlags Flags)
	{
		if (Flags == 0)
		{
			return TEXT("0");
		}
		FString String;
		AppendBitFieldName(VK_IMAGE_ASPECT_COLOR_BIT, TEXT("COLOR"));
		AppendBitFieldName(VK_IMAGE_ASPECT_DEPTH_BIT, TEXT("DEPTH"));
		AppendBitFieldName(VK_IMAGE_ASPECT_STENCIL_BIT, TEXT("STENCIL"));
		AppendBitFieldName(VK_IMAGE_ASPECT_METADATA_BIT, TEXT("METADATA"));
		if (Flags != 0)
		{
			FString Unknown = FString::Printf(TEXT("%d"), Flags);
			AppendBitFieldName(Flags, Unknown);
		}
		return String;
	}

	static FString GetAccessFlagString(VkAccessFlags Flags)
	{
		if (Flags == 0)
		{
			return TEXT("0");
		}
		FString String;
		AppendBitFieldName(VK_ACCESS_INDIRECT_COMMAND_READ_BIT, TEXT("INDIRECT_COMMAND"));
		AppendBitFieldName(VK_ACCESS_INDEX_READ_BIT, TEXT("INDEX_READ"));
		AppendBitFieldName(VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, TEXT("VERTEX_ATTR_READ"));
		AppendBitFieldName(VK_ACCESS_UNIFORM_READ_BIT, TEXT("UNIF_READ"));
		AppendBitFieldName(VK_ACCESS_INPUT_ATTACHMENT_READ_BIT, TEXT("INPUT_ATT_READ"));
		AppendBitFieldName(VK_ACCESS_SHADER_READ_BIT, TEXT("SHADER_READ"));
		AppendBitFieldName(VK_ACCESS_SHADER_WRITE_BIT, TEXT("SHADER_WRITE"));
		AppendBitFieldName(VK_ACCESS_COLOR_ATTACHMENT_READ_BIT, TEXT("COLOR_ATT_READ"));
		AppendBitFieldName(VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, TEXT("COLOR_ATT_WRITE"));
		AppendBitFieldName(VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT, TEXT("DS_ATT_READ"));
		AppendBitFieldName(VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, TEXT("DS_ATT_WIRTE"));
		AppendBitFieldName(VK_ACCESS_TRANSFER_READ_BIT, TEXT("TRANSFER_READ"));
		AppendBitFieldName(VK_ACCESS_TRANSFER_WRITE_BIT, TEXT("TRANSFER_WRITE"));
		AppendBitFieldName(VK_ACCESS_HOST_READ_BIT, TEXT("HOST_READ"));
		AppendBitFieldName(VK_ACCESS_HOST_WRITE_BIT, TEXT("HOST_WRITE"));
		AppendBitFieldName(VK_ACCESS_MEMORY_READ_BIT, TEXT("MEM_READ"));
		AppendBitFieldName(VK_ACCESS_MEMORY_WRITE_BIT, TEXT("MEM_WRITE"));
		if (Flags != 0)
		{
			FString Unknown = FString::Printf(TEXT("%d"), Flags);
			AppendBitFieldName(Flags, Unknown);
		}
		return String;
	}

	FString GetSampleCountString(VkSampleCountFlags Flags)
	{
		if (Flags == 0)
		{
			return TEXT("0");
		}
		FString String;
		AppendBitFieldName(VK_SAMPLE_COUNT_1_BIT, TEXT("1"));
		AppendBitFieldName(VK_SAMPLE_COUNT_2_BIT, TEXT("2"));
		AppendBitFieldName(VK_SAMPLE_COUNT_4_BIT, TEXT("4"));
		AppendBitFieldName(VK_SAMPLE_COUNT_8_BIT, TEXT("8"));
		AppendBitFieldName(VK_SAMPLE_COUNT_16_BIT, TEXT("16"));
		AppendBitFieldName(VK_SAMPLE_COUNT_32_BIT, TEXT("32"));
		AppendBitFieldName(VK_SAMPLE_COUNT_64_BIT, TEXT("64"));
		if (Flags != 0)
		{
			FString Unknown = FString::Printf(TEXT("%d"), Flags);
			AppendBitFieldName(Flags, Unknown);
		}
		return String;
	}

	FString GetImageUsageString(VkImageUsageFlags Flags)
	{
		if (Flags == 0)
		{
			return TEXT("0");
		}
		FString String;
		AppendBitFieldName(VK_IMAGE_USAGE_TRANSFER_SRC_BIT, TEXT("XFER_SRC"));
		AppendBitFieldName(VK_IMAGE_USAGE_TRANSFER_DST_BIT, TEXT("XFER_DST"));
		AppendBitFieldName(VK_IMAGE_USAGE_SAMPLED_BIT, TEXT("SAMPLED"));
		AppendBitFieldName(VK_IMAGE_USAGE_STORAGE_BIT, TEXT("STORAGE"));
		AppendBitFieldName(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, TEXT("COLOR_ATT"));
		AppendBitFieldName(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, TEXT("DS_ATT"));
		AppendBitFieldName(VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT, TEXT("TRANS_ATT"));
		AppendBitFieldName(VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT, TEXT("IN_ATT"));
		if (Flags != 0)
		{
			FString Unknown = FString::Printf(TEXT("%d"), Flags);
			AppendBitFieldName(Flags, Unknown);
		}
		return String;
	}
#undef  AppendBitFieldName

	FString GetExtentString(const VkExtent3D& Extent)
	{
		return FString::Printf(TEXT("w:%d h:%d d:%d"), Extent.width, Extent.height, Extent.depth);
	}

	FString GetExtentString(const VkExtent2D& Extent)
	{
		return FString::Printf(TEXT("w:%d h:%d"), Extent.width, Extent.height);
	}

	static FString GetSubResourceRangeString(const VkImageSubresourceRange& Range)
	{
		return FString::Printf(TEXT("aspectMask=%s, baseMipLevel=%d, levelCount=%d, baseArrayLayer=%d, layerCount=%d"), *GetAspectMaskString(Range.aspectMask), Range.baseMipLevel, Range.levelCount, Range.baseArrayLayer, Range.layerCount);		
	}

	static FString GetStageMaskString(VkPipelineStageFlags Flags)
	{
		return FString::Printf(TEXT("VkPipelineStageFlags=0x%x"), (uint32)Flags);
	}

	void PrintfBeginResult(const FString& String)
	{
		if (CVarVulkanDumpLayer.GetValueOnAnyThread())
		{
			DebugLog += FString::Printf(TEXT("[GLOBAL METHOD]     %8d: %s"), DebugLine++, *String);
		}
	}

	void PrintfBegin(const FString& String)
	{
		if (CVarVulkanDumpLayer.GetValueOnAnyThread())
		{
			DebugLog += FString::Printf(TEXT("[GLOBAL METHOD]     %8d: %s\n"), DebugLine++, *String);
		}
	}

	void DevicePrintfBeginResult(VkDevice Device, const FString& String)
	{
		if (CVarVulkanDumpLayer.GetValueOnAnyThread())
		{
			DebugLog += FString::Printf(TEXT("[D:%p]%8d: %s"), Device, DebugLine++, *String);
		}
	}

	void DevicePrintfBegin(VkDevice Device, const FString& String)
	{
		if (CVarVulkanDumpLayer.GetValueOnAnyThread())
		{
			DebugLog += FString::Printf(TEXT("[D:%p]%8d: %s\n"), Device, DebugLine++, *String);
		}
	}

	void CmdPrintfBegin(VkCommandBuffer CmdBuffer, const FString& String)
	{
		if (CVarVulkanDumpLayer.GetValueOnAnyThread())
		{
			DebugLog += FString::Printf(TEXT("[C:%p]%8d: %s\n"), CmdBuffer, DebugLine++, *String);
		}
	}

	void CmdPrintfBeginResult(VkCommandBuffer CmdBuffer, const FString& String)
	{
		if (CVarVulkanDumpLayer.GetValueOnAnyThread())
		{
			DebugLog += FString::Printf(TEXT("[C:%p]%8d: %s"), CmdBuffer, DebugLine++, *String);
		}
	}

	void PrintResult(VkResult Result)
	{
		if (CVarVulkanDumpLayer.GetValueOnAnyThread())
		{
			DebugLog += FString::Printf(TEXT(" -> %s\n"), *GetVkResultErrorString(Result));
			if (Result < VK_SUCCESS)
			{
				FlushDebugWrapperLog();
			}
		}
	}

	void PrintResultAndPointer(VkResult Result, void* Handle)
	{
		if (CVarVulkanDumpLayer.GetValueOnAnyThread())
		{
			DebugLog += FString::Printf(TEXT(" -> %s => %p\n"), *GetVkResultErrorString(Result), Handle);
			if (Result < VK_SUCCESS)
			{
				FlushDebugWrapperLog();
			}
		}
	}

	void PrintResultAndNamedHandle(VkResult Result, const TCHAR* HandleName, void* Handle)
	{
		if (CVarVulkanDumpLayer.GetValueOnAnyThread())
		{
			DebugLog += FString::Printf(TEXT(" -> %s => %s=%p\n"), *GetVkResultErrorString(Result), HandleName, Handle);
			if (Result < VK_SUCCESS)
			{
				FlushDebugWrapperLog();
			}
		}
	}

	void DumpPhysicalDeviceProperties(VkPhysicalDeviceMemoryProperties* Properties)
	{
		if (CVarVulkanDumpLayer.GetValueOnAnyThread())
		{
			DebugLog += FString::Printf(TEXT(" -> VkPhysicalDeviceMemoryProperties[...]\n"));
		}
	}

	void DumpAllocateMemory(VkDevice Device, const VkMemoryAllocateInfo* AllocateInfo, VkDeviceMemory* Memory)
	{
		if (CVarVulkanDumpLayer.GetValueOnAnyThread())
		{
			DevicePrintfBeginResult(Device, FString::Printf(TEXT("vkAllocateMemory(OutMem=%p): Size=%d, MemTypeIndex=%d"), AllocateInfo, Memory, (uint32)AllocateInfo->allocationSize, AllocateInfo->memoryTypeIndex));
		}
	}

	void DumpMemoryRequirements(VkMemoryRequirements* MemoryRequirements)
	{
		if (CVarVulkanDumpLayer.GetValueOnAnyThread())
		{
			DebugLog += FString::Printf(TEXT(" -> Size=%d Align=%d MemTypeBits=0x%x\n"), (uint32)MemoryRequirements->size, (uint32)MemoryRequirements->alignment, MemoryRequirements->memoryTypeBits);
		}
	}

	void DumpCreateBuffer(VkDevice Device, const VkBufferCreateInfo* CreateInfo, VkBuffer* Buffer)
	{
		FlushDebugWrapperLog();
		DevicePrintfBeginResult(Device, FString::Printf(TEXT("vkCreateBuffer(Info=%p, OutBuffer=%p)[...]"), CreateInfo, Buffer));
	}

	void DumpCreateBufferView(VkDevice Device, const VkBufferViewCreateInfo* CreateInfo, VkBufferView* BufferView)
	{
		DevicePrintfBeginResult(Device, FString::Printf(TEXT("VkBufferViewCreateInfo(Info=%p, OutBufferView=%p)[...]"), CreateInfo, BufferView));
	}

	void DumpCreateImage(VkDevice Device, const VkImageCreateInfo* CreateInfo, VkImage* Image)
	{
		if (CVarVulkanDumpLayer.GetValueOnAnyThread())
		{
			FlushDebugWrapperLog();
			DevicePrintfBegin(Device, FString::Printf(TEXT("vkCreateImage(Info=%p, OutImage=%p)"), CreateInfo, Image));
			DebugLog += FString::Printf(TEXT("%sVkImageCreateInfo: Flags=%d, ImageType=%d, Format=%s, MipLevels=%d, ArrayLayers=%d, Samples=%s\n"), Tabs, CreateInfo->flags, (uint32)CreateInfo->imageType,
				*GetVkFormatString(CreateInfo->format), CreateInfo->mipLevels, CreateInfo->arrayLayers, *GetSampleCountString(CreateInfo->samples));
			DebugLog += FString::Printf(TEXT("%s\tExtent=(%s) Tiling=%s, Usage=%s, Initial=%s\n"), Tabs, *GetExtentString(CreateInfo->extent), 
				*GetImageTilingString(CreateInfo->tiling), *GetImageUsageString(CreateInfo->usage), *GetImageLayoutString(CreateInfo->initialLayout));
		}
	}

	void DumpCreateImageView(VkDevice Device, const VkImageViewCreateInfo* CreateInfo, VkImageView* ImageView)
	{
		if (CVarVulkanDumpLayer.GetValueOnAnyThread())
		{
			DevicePrintfBegin(Device, FString::Printf(TEXT("vkCreateImageView(Info=%p, OutImageView=%p)"), CreateInfo, ImageView));
			DebugLog += FString::Printf(TEXT("%sVkImageViewCreateInfo: Flags=%d, Image=%p, ViewType=%s, Format=%s, Components=%s\n"), Tabs, CreateInfo->flags, CreateInfo->image, 
				*GetImageViewTypeString(CreateInfo->viewType), *GetVkFormatString(CreateInfo->format), *GetComponentMappingString(CreateInfo->components));
			DebugLog += FString::Printf(TEXT("%s\tSubresourceRange=(%s)"), Tabs, *GetSubResourceRangeString(CreateInfo->subresourceRange));
		}
	}

	void DumpFenceCreate(VkDevice Device, const VkFenceCreateInfo* CreateInfo, VkFence* Fence)
	{
		DevicePrintfBeginResult(Device, FString::Printf(TEXT("vkCreateFence(CreateInfo=%p, OutFence=%p)[...]"), CreateInfo, Fence));
	}

	void DumpFenceList(uint32 FenceCount, const VkFence* Fences)
	{
		if (CVarVulkanDumpLayer.GetValueOnAnyThread())
		{
			for (uint32 Index = 0; Index < FenceCount; ++Index)
			{
				DebugLog += Tabs;
				DebugLog += '\t';
				DebugLog += FString::Printf(TEXT("Fence[%d]=%p"), Index, Fences[Index]);
				if (Index < FenceCount - 1)
				{
					DebugLog += TEXT("\n");
				}
			}
		}
	}
	
	void DumpSemaphoreCreate(VkDevice Device, const VkSemaphoreCreateInfo* CreateInfo, VkSemaphore* Semaphore)
	{
		DevicePrintfBeginResult(Device, FString::Printf(TEXT("vkCreateSemaphore(CreateInfo=%p, OutSemaphore=%p)[...]"), CreateInfo, Semaphore));
	}

	void DumpMappedMemoryRanges(uint32 memoryRangeCount, const VkMappedMemoryRange* MemoryRanges)
	{
		ensure(0);
	}

	void DumpResolveImage(VkCommandBuffer CommandBuffer, VkImage SrcImage, VkImageLayout SrcImageLayout, VkImage DstImage, VkImageLayout DstImageLayout, uint32 RegionCount, const VkImageResolve* Regions)
	{
		if (CVarVulkanDumpLayer.GetValueOnAnyThread())
		{
			CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdResolveImage(SrcImage=%p, SrcImageLayout=%s, DestImage=%p, DestImageLayout=%s, NumRegions=%d, Regions=%p)[...]"),
				CommandBuffer, SrcImage, *GetImageLayoutString(SrcImageLayout), DstImage, *GetImageLayoutString(DstImageLayout), RegionCount, Regions));
			for (uint32 Index = 0; Index < RegionCount; ++Index)
			{
				DebugLog += Tabs;
				DebugLog += FString::Printf(TEXT("Region %d: "), Index);
				/*
							typedef struct VkImageResolve {
								VkImageSubresourceLayers    srcSubresource;
								VkOffset3D                  srcOffset;
								VkImageSubresourceLayers    dstSubresource;
								VkOffset3D                  dstOffset;
								VkExtent3D                  extent;

				*/
			}
		}
	}

	void DumpFreeDescriptorSets(VkDevice Device, VkDescriptorPool DescriptorPool, uint32 DescriptorSetCount, const VkDescriptorSet* DescriptorSets)
	{
		if (CVarVulkanDumpLayer.GetValueOnAnyThread())
		{
			DevicePrintfBegin(Device, FString::Printf(TEXT("vkFreeDescriptorSets(Pool=%p, NumSets=%d, Sets=%p)"), DescriptorPool, DescriptorSetCount, DescriptorSets));
			for (uint32 Index = 0; Index < DescriptorSetCount; ++Index)
			{
				DebugLog += Tabs;
				DebugLog += FString::Printf(TEXT("Set %d: %p\n"), Index, DescriptorSets[Index]);
			}
		}
	}

	void DumpCreateInstance(const VkInstanceCreateInfo* CreateInfo, VkInstance* Instance)
	{
		PrintfBegin(FString::Printf(TEXT("vkCreateInstance(Info=%p, OutInstance=%p)[...]"), CreateInfo, Instance));
	}

	void DumpEnumeratePhysicalDevicesEpilog(uint32* PhysicalDeviceCount, VkPhysicalDevice* PhysicalDevices)
	{
		if (CVarVulkanDumpLayer.GetValueOnAnyThread())
		{
			if (PhysicalDeviceCount)
			{
				DebugLog += Tabs;
				DebugLog += FString::Printf(TEXT("OutCount=%d\n"), *PhysicalDeviceCount);
				if (PhysicalDevices)
				{
					for (uint32 Index = 0; Index < *PhysicalDeviceCount; ++Index)
					{
						DebugLog += Tabs;
						DebugLog += FString::Printf(TEXT("\tOutDevice[%d]=%p\n"), Index, PhysicalDevices[Index]);
					}
				}
			}
		}
	}

	void DumpCmdPipelineBarrier(VkCommandBuffer CommandBuffer, VkPipelineStageFlags SrcStageMask, VkPipelineStageFlags DstStageMask, VkDependencyFlags DependencyFlags, uint32 MemoryBarrierCount, const VkMemoryBarrier* MemoryBarriers, uint32 BufferMemoryBarrierCount, const VkBufferMemoryBarrier* BufferMemoryBarriers, uint32 ImageMemoryBarrierCount, const VkImageMemoryBarrier* ImageMemoryBarriers)
	{
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdPipelineBarrier(SrcMask=%s, DestMask=%s, Flags=%d, NumMemB=%d, MemB=%p,"), *GetStageMaskString(SrcStageMask), *GetStageMaskString(DstStageMask), (uint32)DependencyFlags, MemoryBarrierCount, MemoryBarriers));
		if (CVarVulkanDumpLayer.GetValueOnAnyThread())
		{
			DebugLog += FString::Printf(TEXT("%s\tNumBufferB=%d, BufferB=%p, NumImageB=%d, ImageB=%p)[...]\n"), Tabs, BufferMemoryBarrierCount, BufferMemoryBarriers, ImageMemoryBarrierCount, ImageMemoryBarriers);
			if (ImageMemoryBarrierCount)
			{
				for (uint32 Index = 0; Index < ImageMemoryBarrierCount; ++Index)
				{
					DebugLog += FString::Printf(TEXT("%s\tImageBarrier[%d]: srcAccess=%s, oldLayout=%s, srcQueueFamilyIndex=%d\n"), Tabs, Index, *GetAccessFlagString(ImageMemoryBarriers[Index].srcAccessMask), *GetImageLayoutString(ImageMemoryBarriers[Index].oldLayout), ImageMemoryBarriers[Index].srcQueueFamilyIndex);
					DebugLog += FString::Printf(TEXT("%s\t\tdstAccess=%s, newLayout=%s, dstQueueFamilyIndex=%d\n"), Tabs, *GetAccessFlagString(ImageMemoryBarriers[Index].dstAccessMask), *GetImageLayoutString(ImageMemoryBarriers[Index].newLayout), ImageMemoryBarriers[Index].dstQueueFamilyIndex);
					DebugLog += FString::Printf(TEXT("%s\t\tImage=%p, subresourceRange=(%s)\n"), Tabs, ImageMemoryBarriers[Index].image, *GetSubResourceRangeString(ImageMemoryBarriers[Index].subresourceRange));
				}
			}
		}
	}

	void DumpBindDescriptorSets(VkCommandBuffer CommandBuffer, VkPipelineBindPoint PipelineBindPoint, VkPipelineLayout Layout, uint32 FirstSet, uint32 DescriptorSetCount, const VkDescriptorSet* DescriptorSets, uint32 DynamicOffsetCount, const uint32* DynamicOffsets)
	{
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdBindDescriptorSets(PipelineBindPoint=%d, Layout=%p, FirstSet=%d, NumDS=%d, DS=%p, NumDynamicOffset=%d, DynamicOffsets=%p)[...]"), (int32)PipelineBindPoint, Layout, FirstSet, DescriptorSetCount, DescriptorSets, DynamicOffsetCount, DynamicOffsets));
		if (CVarVulkanDumpLayer.GetValueOnAnyThread())
		{
			for (uint32 Index = 0; Index < DescriptorSetCount; ++Index)
			{
				DebugLog += FString::Printf(TEXT("%s\tDS[%d]=%p\n"), Tabs, Index, DescriptorSets[Index]);
			}
			for (uint32 Index = 0; Index < DynamicOffsetCount; ++Index)
			{
				DebugLog += FString::Printf(TEXT("%s\tDynamicOffset[%d]=%p\n"), Tabs, Index, DynamicOffsets[Index]);
			}
		}
	}

	void DumpCreateDescriptorSetLayout(VkDevice Device, const VkDescriptorSetLayoutCreateInfo* CreateInfo, VkDescriptorSetLayout* SetLayout)
	{
		DevicePrintfBegin(Device, FString::Printf(TEXT("vkCreateDescriptorSetLayout(Info=%p, OutLayout=%p)[...]"), CreateInfo, SetLayout));
	}

	void DumpAllocateDescriptorSets(VkDevice Device, const VkDescriptorSetAllocateInfo* AllocateInfo, VkDescriptorSet* DescriptorSets)
	{
		DevicePrintfBegin(Device, FString::Printf(TEXT("vkAllocateDescriptorSets(Info=%p, OutSets=%p)[...]"), AllocateInfo, DescriptorSets));
	}

	void DumpUpdateDescriptorSets(VkDevice Device, uint32 DescriptorWriteCount, const VkWriteDescriptorSet* DescriptorWrites, uint32 DescriptorCopyCount, const VkCopyDescriptorSet* DescriptorCopies)
	{
		DevicePrintfBegin(Device, FString::Printf(TEXT("vkUpdateDescriptorSets(NumWrites=%d, Writes=%p, NumCopies=%d, Copies=%p)[...]"), DescriptorWriteCount, DescriptorWrites, DescriptorCopyCount, DescriptorCopies));
	}

	void DumpCreateFramebuffer(VkDevice Device, const VkFramebufferCreateInfo* CreateInfo, VkFramebuffer* Framebuffer)
	{
		if (CVarVulkanDumpLayer.GetValueOnAnyThread())
		{
			DevicePrintfBegin(Device, FString::Printf(TEXT("vkCreateFramebuffer(Info=%p, OutFramebuffer=%p)"), CreateInfo, Framebuffer));
			DebugLog += FString::Printf(TEXT("%sVkFramebufferCreateInfo: Flags=%d, RenderPass=%p, NumAttachments=%d\n"), Tabs, CreateInfo->flags, CreateInfo->renderPass, CreateInfo->attachmentCount);
			for (uint32 Index = 0; Index < CreateInfo->attachmentCount; ++Index)
			{
				DebugLog += FString::Printf(TEXT("%s\tAttachment[%d]: ImageView=%p\n"), Tabs, Index, CreateInfo->pAttachments[Index]);
			}
			DebugLog += FString::Printf(TEXT("%s\twidth=%d, height=%d, layers=%d"), Tabs, CreateInfo->width, CreateInfo->height, CreateInfo->layers);
		}
	}

	void DumpCreateRenderPass(VkDevice Device, const VkRenderPassCreateInfo* CreateInfo, VkRenderPass* RenderPass)
	{
		if (CVarVulkanDumpLayer.GetValueOnAnyThread())
		{
			DevicePrintfBegin(Device, FString::Printf(TEXT("vkCreateRenderPass(Info=%p, OutRenderPass=%p)[...]"), CreateInfo, RenderPass));
			DebugLog += FString::Printf(TEXT("%s\tVkRenderPassCreateInfo: NumAttachments=%d, Attachments=%p, NumSubPasses=%d, SubPasses=%p\n"), Tabs, CreateInfo->attachmentCount, CreateInfo->pAttachments, CreateInfo->subpassCount, CreateInfo->pSubpasses);
			for (uint32 Index = 0; Index < CreateInfo->attachmentCount; ++Index)
			{
				auto GetLoadOpString = [](VkAttachmentLoadOp Op) -> FString
					{
						switch (Op)
						{
						case VK_ATTACHMENT_LOAD_OP_LOAD: return TEXT("LOAD");
						case VK_ATTACHMENT_LOAD_OP_CLEAR: return TEXT("CLEAR");
						case VK_ATTACHMENT_LOAD_OP_DONT_CARE: return TEXT("DONT_CARE");
						default: return FString::Printf(TEXT("Invalid(%d)"), (uint32)Op);
						}
					};
				auto GetStoreOpString = [](VkAttachmentStoreOp Op) -> FString
					{
						switch(Op)
						{
						case VK_ATTACHMENT_STORE_OP_STORE: return TEXT("STORE");
						case VK_ATTACHMENT_STORE_OP_DONT_CARE: return TEXT("DONT_CARE");
						default: return FString::Printf(TEXT("Invalid(%d)"), (uint32)Op);
						}
					};
				/*
				typedef struct VkAttachmentDescription {
					VkAttachmentDescriptionFlags    flags;
					VkFormat                        format;
					VkSampleCountFlagBits           samples;
					VkAttachmentLoadOp              loadOp;
					VkAttachmentStoreOp             storeOp;
					VkAttachmentLoadOp              stencilLoadOp;
					VkAttachmentStoreOp             stencilStoreOp;
					VkImageLayout                   initialLayout;
					VkImageLayout                   finalLayout;
				} VkAttachmentDescription;
*/
				const VkAttachmentDescription& Desc = CreateInfo->pAttachments[Index];
				DebugLog += FString::Printf(TEXT("%s\t\tAttachment[%d]: Flags=%s, Format=%s, Samples=%s, Load=%s, Store=%s\n"), Tabs, Index,
					(Desc.flags == VK_ATTACHMENT_DESCRIPTION_MAY_ALIAS_BIT ? TEXT("MAY_ALIAS") : TEXT("0")),
					*GetVkFormatString(Desc.format), *GetSampleCountString(Desc.samples), *GetLoadOpString(Desc.loadOp), *GetStoreOpString(Desc.storeOp));
				DebugLog += FString::Printf(TEXT("%s\t\t\tLoadStencil=%s, StoreStencil=%s, Initial=%s, Final=%s\n"), Tabs, 
					*GetLoadOpString(Desc.stencilLoadOp), *GetStoreOpString(Desc.stencilStoreOp), *VulkanRHI::GetImageLayoutString(Desc.initialLayout), *VulkanRHI::GetImageLayoutString(Desc.finalLayout));
			}

			for (uint32 Index = 0; Index < CreateInfo->subpassCount; ++Index)
			{
				const VkSubpassDescription& Desc = CreateInfo->pSubpasses[Index];
				DebugLog += FString::Printf(TEXT("%s\t\tSubpass[%d]: Flags=%d, Bind=%s, NumInputAttach=%d, InputAttach=%p, NumColorAttach=%d, ColorAttach=%p, DSAttch=%p\n"), Tabs, Index,
					Desc.flags,
					Desc.pipelineBindPoint == VK_PIPELINE_BIND_POINT_COMPUTE ? TEXT("Compute") : TEXT("Gfx"),
					Desc.inputAttachmentCount, Desc.pInputAttachments, Desc.colorAttachmentCount, Desc.pColorAttachments, Desc.pDepthStencilAttachment);
				for (uint32 SubIndex = 0; SubIndex < Desc.inputAttachmentCount; ++SubIndex)
				{
					DebugLog += FString::Printf(TEXT("%s\t\t\tInputAttach[%d]: Attach=%d, Layout=%s\n"), Tabs, Index,
						Desc.pInputAttachments[SubIndex].attachment, *GetImageLayoutString(Desc.pInputAttachments[SubIndex].layout));
				}
				for (uint32 SubIndex = 0; SubIndex < Desc.inputAttachmentCount; ++SubIndex)
				{
					DebugLog += FString::Printf(TEXT("%s\t\t\tColorAttach[%d]: Attach=%d, Layout=%s\n"), Tabs, Index,
						Desc.pColorAttachments[SubIndex].attachment, *GetImageLayoutString(Desc.pColorAttachments[SubIndex].layout));
				}
				if (Desc.pDepthStencilAttachment)
				{
					DebugLog += FString::Printf(TEXT("%s\t\t\tDSAttach: Attach=%d, Layout=%s\n"), Tabs, Desc.pDepthStencilAttachment->attachment, *GetImageLayoutString(Desc.pDepthStencilAttachment->layout));
				}
				/*
				typedef struct VkAttachmentReference {
					uint32_t         attachment;
					VkImageLayout    layout;
				} VkAttachmentReference;

				typedef struct VkSubpassDescription {
					const VkAttachmentReference*    pResolveAttachments;
					uint32_t                        preserveAttachmentCount;
					const uint32_t*                 pPreserveAttachments;
				} VkSubpassDescription;*/
			}

/*
			typedef struct VkRenderPassCreateInfo {
				uint32_t                          attachmentCount;
				const VkAttachmentDescription*    pAttachments;
				uint32_t                          subpassCount;
				const VkSubpassDescription*       pSubpasses;
				uint32_t                          dependencyCount;
				const VkSubpassDependency*        pDependencies;
			} VkRenderPassCreateInfo;
*/

		}
	}

	void DumpQueueSubmit(VkQueue Queue, uint32 SubmitCount, const VkSubmitInfo* Submits, VkFence Fence)
	{
		if (CVarVulkanDumpLayer.GetValueOnAnyThread())
		{
			PrintfBeginResult(FString::Printf(TEXT("vkQueueSubmit(Queue=%p, Count=%d, Submits=%p, Fence=%p)"), Queue, SubmitCount, Submits, Fence));
			for (uint32 Index = 0; Index < SubmitCount; ++Index)
			{
				DebugLog += FString::Printf(TEXT("\n%sSubmit[%d]:"), Tabs, Index);
				if (Submits[Index].waitSemaphoreCount > 0)
				{
					DebugLog += FString::Printf(TEXT("\n%s\tWaitSemaphores(Mask): "), Tabs, Index);
					for (uint32 SubIndex = 0; SubIndex < Submits[Index].waitSemaphoreCount; ++SubIndex)
					{
						DebugLog += FString::Printf(TEXT("%p(%d) "), Submits[Index].pWaitSemaphores[SubIndex], (int32)Submits[Index].pWaitDstStageMask[SubIndex]);
					}
				}
				if (Submits[Index].commandBufferCount > 0)
				{
					DebugLog += FString::Printf(TEXT("\n%s\tCommandBuffers: "), Tabs, Index);
					for (uint32 SubIndex = 0; SubIndex < Submits[Index].commandBufferCount; ++SubIndex)
					{
						DebugLog += FString::Printf(TEXT("%p "), Submits[Index].pCommandBuffers[SubIndex]);
					}
				}
				if (Submits[Index].signalSemaphoreCount > 0)
				{
					DebugLog += FString::Printf(TEXT("\n%s\tSignalSemaphore: "), Tabs, Index);
					for (uint32 SubIndex = 0; SubIndex < Submits[Index].signalSemaphoreCount; ++SubIndex)
					{
						DebugLog += FString::Printf(TEXT("%p "), Submits[Index].pSignalSemaphores[SubIndex]);
					}
				}
			}

			FlushDebugWrapperLog();
		}
	}

	void DumpCreateShaderModule(VkDevice Device, const VkShaderModuleCreateInfo* CreateInfo, VkShaderModule* ShaderModule)
	{
		DevicePrintfBeginResult(Device, FString::Printf(TEXT("vkCreateShaderModule(CreateInfo=%p, OutShaderModule=%p)[...]"), CreateInfo, ShaderModule));
	}

	void DumpCreatePipelineCache(VkDevice Device, const VkPipelineCacheCreateInfo* CreateInfo, VkPipelineCache* PipelineCache)
	{
		DevicePrintfBeginResult(Device, FString::Printf(TEXT("vkCreatePipelineCache(CreateInfo=%p, OutPipelineCache=%p)[...]"), CreateInfo, PipelineCache));
	}

	void DumpCreateCommandPool(VkDevice Device, const VkCommandPoolCreateInfo* CreateInfo, VkCommandPool* CommandPool)
	{
		FlushDebugWrapperLog();
		DevicePrintfBeginResult(Device, FString::Printf(TEXT("vkCreateCommandPool(CreateInfo=%p, OutCommandPool=%p)[...]"), CreateInfo, CommandPool));
	}

	void DumpCreateQueryPool(VkDevice Device, const VkQueryPoolCreateInfo* CreateInfo, VkQueryPool* QueryPool)
	{
		DevicePrintfBeginResult(Device, FString::Printf(TEXT("vkCreateQueryPool(CreateInfo=%p, OutQueryPool=%p)[...]"), CreateInfo, QueryPool));
	}

	void DumpCreatePipelineLayout(VkDevice Device, const VkPipelineLayoutCreateInfo* CreateInfo, VkPipelineLayout* PipelineLayout)
	{
		DevicePrintfBeginResult(Device, FString::Printf(TEXT("vkCreatePipelineLayout(CreateInfo=%p, OutPipelineLayout=%p)[...]"), CreateInfo, PipelineLayout));
	}

	void DumpCreateDescriptorPool(VkDevice Device, const VkDescriptorPoolCreateInfo* CreateInfo, VkDescriptorPool* DescriptorPool)
	{
		DevicePrintfBeginResult(Device, FString::Printf(TEXT("vkCreateDescriptorPool(CreateInfo=%p, OutDescriptorPool=%p)[...]"), CreateInfo, DescriptorPool));
	}

	void DumpCreateSampler(VkDevice Device, const VkSamplerCreateInfo* CreateInfo, VkSampler* Sampler)
	{
		DevicePrintfBeginResult(Device, FString::Printf(TEXT("vkCreateSampler(CreateInfo=%p, OutSampler=%p)[...]"), CreateInfo, Sampler));
	}

	void DumpCreateDevice(VkPhysicalDevice PhysicalDevice, const VkDeviceCreateInfo* CreateInfo, VkDevice* Device)
	{
		FlushDebugWrapperLog();
		PrintfBeginResult(FString::Printf(TEXT("vkCreateDevice(PhysicalDevice=%p, CreateInfo=%p, OutDevice=%p)[...]"), PhysicalDevice, CreateInfo, Device));
	}

	void DumpGetPhysicalDeviceFeatures(VkPhysicalDevice PhysicalDevice, VkPhysicalDeviceFeatures* Features)
	{
		PrintfBeginResult(FString::Printf(TEXT("GetPhysicalDeviceFeatures(PhysicalDevice=%p, Features=%p)[...]"), PhysicalDevice, Features));
	}

	void DumpPhysicalDeviceFeatures(VkPhysicalDeviceFeatures* Features)
	{
		if (CVarVulkanDumpLayer.GetValueOnAnyThread())
		{
			DebugLog += FString::Printf(TEXT("VkPhysicalDeviceFeatures [...]\n"));
		}
	}

	void DumpBeginCommandBuffer(VkCommandBuffer CommandBuffer, const VkCommandBufferBeginInfo* BeginInfo)
	{
		FlushDebugWrapperLog();

		PrintfBeginResult(FString::Printf(TEXT("vkBeginCommandBuffer(CmdBuffer=%p, Info=%p)[...]"), CommandBuffer, BeginInfo));
	}

	void DumpCmdBeginRenderPass(VkCommandBuffer CommandBuffer, const VkRenderPassBeginInfo* RenderPassBegin, VkSubpassContents Contents)
	{
		if (CVarVulkanDumpLayer.GetValueOnAnyThread())
		{
			auto GetSubpassContents = [](VkSubpassContents Contents) -> FString
				{
					switch (Contents)
					{
					case VK_SUBPASS_CONTENTS_INLINE: return TEXT("INLINE");
					case VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS: return TEXT("SECONDARY_CMD_BUFS");
					default: return FString::Printf(TEXT("%d"), (int32)Contents);
					}					
				};
			CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdBeginRenderPass(BeginInfo=%p, Contents=%s)"), RenderPassBegin, *GetSubpassContents(Contents)));
			DebugLog += FString::Printf(TEXT("%sBeginInfo: RenderPass=%p, Framebuffer=%p, renderArea=(x:%d, y:%d, %s), clearValues=%d\n"),
				Tabs, RenderPassBegin->renderPass, RenderPassBegin->framebuffer, 
				RenderPassBegin->renderArea.offset.x, RenderPassBegin->renderArea.offset.y, 
				*GetExtentString(RenderPassBegin->renderArea.extent),
				RenderPassBegin->clearValueCount);
			for (uint32 Index = 0; Index < RenderPassBegin->clearValueCount; ++Index)
			{
				DebugLog += FString::Printf(TEXT("%s\tclearValue[%d]=(%d(%f), %d(%f), %d(%f), %d(%f))\n"),
					Tabs, Index,
					RenderPassBegin->pClearValues[Index].color.uint32[0], RenderPassBegin->pClearValues[Index].color.float32[0],
					RenderPassBegin->pClearValues[Index].color.uint32[1], RenderPassBegin->pClearValues[Index].color.float32[1],
					RenderPassBegin->pClearValues[Index].color.uint32[2], RenderPassBegin->pClearValues[Index].color.float32[2],
					RenderPassBegin->pClearValues[Index].color.uint32[3], RenderPassBegin->pClearValues[Index].color.float32[3]);
			}
		}
	}

	void DumpCmdBindVertexBuffers(VkCommandBuffer CommandBuffer, uint32 FirstBinding, uint32 BindingCount, const VkBuffer* Buffers, const VkDeviceSize* Offsets)
	{
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdBindVertexBuffers(FirstBinding=%d, NumBindings=%d, Buffers=%p, Offsets=%p)[...]"), FirstBinding, BindingCount, Buffers, Offsets));
	}

	void DumpCmdCopyBufferToImage(VkCommandBuffer CommandBuffer, VkBuffer SrcBuffer, VkImage DstImage, VkImageLayout DstImageLayout, uint32 RegionCount, const VkBufferImageCopy* Regions)
	{
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdCopyBufferToImage(SrcBuffer=%p, DstImage=%p, DstImageLayout=%s, NumRegions=%d, Regions=%p)[...]"), 
			SrcBuffer, DstImage, *GetImageLayoutString(DstImageLayout), RegionCount, Regions));
	}

	void DumpCmdCopyBuffer(VkCommandBuffer CommandBuffer, VkBuffer SrcBuffer, VkBuffer DstBuffer, uint32 RegionCount, const VkBufferCopy* Regions)
	{
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdCopyBuffer(SrcBuffer=%p, DstBuffer=%p, NumRegions=%d, Regions=%p)[...]"), SrcBuffer, DstBuffer, RegionCount, Regions));
	}

	void DumpGetImageSubresourceLayout(VkDevice Device, VkImage Image, const VkImageSubresource* Subresource, VkSubresourceLayout* Layout)
	{
		DevicePrintfBeginResult(Device, FString::Printf(TEXT("vkGetImageSubresourceLayout(Image=%p, Subresource=%p, OutLayout=%p)"), Image, Subresource, Layout));
	}

	void DumpImageSubresourceLayout(VkSubresourceLayout* Layout)
	{
		if (CVarVulkanDumpLayer.GetValueOnAnyThread())
		{
			DebugLog += FString::Printf(TEXT("VkSubresourceLayout: [...]\n"));
		}
	}

	void DumpSwapChainImages(VkResult Result, uint32* SwapchainImageCount, VkImage* SwapchainImages)
	{
		if (CVarVulkanDumpLayer.GetValueOnAnyThread())
		{
			PrintResult(Result);
			if (SwapchainImages)
			{
				for (uint32 Index = 0; Index < *SwapchainImageCount; ++Index)
				{
					DebugLog += FString::Printf(TEXT("%sImage[%d]=%p\n"), Tabs, Index, SwapchainImages[Index]);
				}
			}
			else
			{
				DebugLog += FString::Printf(TEXT("%sNumImages=%d\n"), Tabs, *SwapchainImageCount);
			}
		}
	}

	static struct FGlobalDumpLog
	{
		~FGlobalDumpLog()
		{
			FlushDebugWrapperLog();
		}
	} GGlobalDumpLogInstance;
}
#endif	// VULKAN_ENABLE_DUMP_LAYER
#endif // VULKAN_HAS_DEBUGGING_ENABLED
