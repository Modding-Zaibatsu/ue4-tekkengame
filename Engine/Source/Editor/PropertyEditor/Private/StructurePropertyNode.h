// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

//-----------------------------------------------------------------------------
//	FStructPropertyNode - Used for the root and various sub-nodes
//-----------------------------------------------------------------------------

class FStructurePropertyNode : public FComplexPropertyNode
{
public:
	FStructurePropertyNode() : FComplexPropertyNode() {}
	virtual ~FStructurePropertyNode() {}

	/** FPropertyNode Interface */
	virtual uint8* GetValueBaseAddress(uint8* Base) override
	{
		ensure(true);
		return IsValid() ? StructData->GetStructMemory() : NULL;
	}

	void SetStructure(TSharedPtr<FStructOnScope> InStructData)
	{
		ClearCachedReadAddresses(true);
		DestroyTree();
		StructData = InStructData;
	}

	bool IsValid() const
	{
		return StructData.IsValid() && StructData->IsValid();
	}

	bool GetReadAddressUncached(FPropertyNode& InPropertyNode, FReadAddressListData& OutAddresses) const override
	{
		if (!IsValid())
		{
			return false;
		}

		UProperty* InItemProperty = InPropertyNode.GetProperty();
		if (!InItemProperty)
		{
			return false;
		}

		uint8* ReadAddress = StructData->GetStructMemory();
		check(ReadAddress);
		OutAddresses.Add(NULL, InPropertyNode.GetValueBaseAddress(ReadAddress), true);
		return true;
	}

	bool GetReadAddressUncached(FPropertyNode& InPropertyNode,
		bool InRequiresSingleSelection,
		FReadAddressListData& OutAddresses,
		bool bComparePropertyContents,
		bool bObjectForceCompare,
		bool bArrayPropertiesCanDifferInSize) const override
	{
		return GetReadAddressUncached(InPropertyNode, OutAddresses);
	}

	UPackage* GetOwnerPackage() const
	{
		return IsValid() ? StructData->GetPackage() : nullptr;
	}

	/** FComplexPropertyNode Interface */
	virtual const UStruct* GetBaseStructure() const override
	{ 
		return IsValid() ? StructData->GetStruct() : NULL; 
	}
	virtual UStruct* GetBaseStructure() override
	{
		const UStruct* Struct = IsValid() ? StructData->GetStruct() : NULL;
		return const_cast<UStruct*>(Struct);
	}
	virtual int32 GetInstancesNum() const override
	{ 
		return IsValid() ? 1 : 0; 
	}
	virtual uint8* GetMemoryOfInstance(int32 Index) override
	{ 
		check(0 == Index);
		return IsValid() ? StructData->GetStructMemory() : NULL;
	}
	virtual TWeakObjectPtr<UObject> GetInstanceAsUObject(int32 Index) override
	{
		check(0 == Index);
		return NULL;
	}
	virtual EPropertyType GetPropertyType() const override
	{
		return EPT_StandaloneStructure;
	}

	virtual void Disconnect() override
	{
		SetStructure(NULL);
	}

protected:

	/** FPropertyNode interface */
	virtual void InitChildNodes() override;

	virtual bool GetQualifiedName(FString& PathPlusIndex, const bool bWithArrayIndex, const FPropertyNode* StopParent = nullptr, bool bIgnoreCategories = false) const override
	{
		PathPlusIndex += TEXT("Struct");
		return true;
	}

private:
	TSharedPtr<FStructOnScope> StructData;
};

