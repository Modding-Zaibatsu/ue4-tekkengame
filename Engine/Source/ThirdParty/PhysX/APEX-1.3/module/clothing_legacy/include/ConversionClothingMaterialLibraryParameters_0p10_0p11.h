/*
 * Copyright (c) 2008-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */


#ifndef CONVERSIONCLOTHINGMATERIALLIBRARYPARAMETERS_0P10_0P11H_H
#define CONVERSIONCLOTHINGMATERIALLIBRARYPARAMETERS_0P10_0P11H_H

#include "ParamConversionTemplate.h"
#include "ClothingMaterialLibraryParameters_0p10.h"
#include "ClothingMaterialLibraryParameters_0p11.h"

namespace physx
{
namespace apex
{
namespace legacy
{

typedef ParamConversionTemplate<ClothingMaterialLibraryParameters_0p10, ClothingMaterialLibraryParameters_0p11, 10, 11> ConversionClothingMaterialLibraryParameters_0p10_0p11Parent;

class ConversionClothingMaterialLibraryParameters_0p10_0p11: ConversionClothingMaterialLibraryParameters_0p10_0p11Parent
{
public:
	static NxParameterized::Conversion* Create(NxParameterized::Traits* t)
	{
		void* buf = t->alloc(sizeof(ConversionClothingMaterialLibraryParameters_0p10_0p11));
		return buf ? PX_PLACEMENT_NEW(buf, ConversionClothingMaterialLibraryParameters_0p10_0p11)(t) : 0;
	}

protected:
	ConversionClothingMaterialLibraryParameters_0p10_0p11(NxParameterized::Traits* t) : ConversionClothingMaterialLibraryParameters_0p10_0p11Parent(t) {}

	const NxParameterized::PrefVer* getPreferredVersions() const
	{
		static NxParameterized::PrefVer prefVers[] =
		{
			//TODO:
			//	Add your preferred versions for included references here.
			//	Entry format is
			//		{ (const char*)longName, (PxU32)preferredVersion }

			{ 0, 0 } // Terminator (do not remove!)
		};

		return prefVers;
	}

	bool convert()
	{
		//TODO:
		//	Write custom conversion code here using mNewData and mLegacyData members.
		//
		//	Note that
		//		- mNewData has already been initialized with default values
		//		- same-named/same-typed members have already been copied
		//			from mLegacyData to mNewData
		//		- included references were moved to mNewData
		//			(and updated to preferred versions according to getPreferredVersions)
		//
		//	For more info see the versioning wiki.

		// selfcollisionThickness is updated in ConversionClothingAssetParameters_0p9_0p10.h
		
		// init selfcollisionStiffness
		for (PxI32 i = 0; i < mNewData->materials.arraySizes[0]; ++i)
		{
			mNewData->materials.buf[i].selfcollisionStiffness = 1.0f;
		}

		return true;
	}
};

}
}
} // namespace physx::apex

#endif
