// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

//
//	FExpressionInput
//

//@warning: FExpressionInput is mirrored in MaterialExpression.h and manually "subclassed" in Material.h (FMaterialInput)
struct FExpressionInput
{
#if WITH_EDITORONLY_DATA
	/** Material expression that this input is connected to, or NULL if not connected. */
	class UMaterialExpression*	Expression;
#endif

	/** Index into Expression's outputs array that this input is connected to. */
	int32						OutputIndex;

	/** 
	 * Optional name of the input.  
	 * Note that this is the only member which is not derived from the output currently connected. 
	 */
	FString						InputName;

	int32						Mask,
								MaskR,
								MaskG,
								MaskB,
								MaskA;

	/** Material expression name that this input is connected to, or None if not connected. Used only in cooked builds */
	FName				ExpressionName;

	FExpressionInput()
		: OutputIndex(0)
		, Mask(0)
		, MaskR(0)
		, MaskG(0)
		, MaskB(0)
		, MaskA(0)
	{
#if WITH_EDITORONLY_DATA
		Expression = nullptr;
#endif
	}

#if WITH_EDITOR
	ENGINE_API int32 Compile(class FMaterialCompiler* Compiler, int32 MultiplexIndex=INDEX_NONE);
#endif // WITH_EDITOR

	/**
	 * Tests if the input has a material expression connected to it
	 *
	 * @return	true if an expression is connected, otherwise false
	 */
	bool IsConnected() const 
	{ 
#if WITH_EDITORONLY_DATA
		return (nullptr != Expression);
#else
		return ExpressionName != NAME_None;
#endif // WITH_EDITORONLY_DATA
	}

#if WITH_EDITOR
	/** Connects output of InExpression to this input */
	ENGINE_API void Connect( int32 InOutputIndex, class UMaterialExpression* InExpression );
#endif // WITH_EDITOR

	/** ICPPStructOps interface */
	ENGINE_API bool Serialize(FArchive& Ar);
};

template<>
struct TStructOpsTypeTraits<FExpressionInput>
	: public TStructOpsTypeTraitsBase
{
	enum
	{
		WithSerializer = true,
	};
};

//
//	FExpressionOutput
//

struct FExpressionOutput
{
	FString	OutputName;
	int32	Mask,
		MaskR,
		MaskG,
		MaskB,
		MaskA;

	FExpressionOutput(int32 InMask = 0, int32 InMaskR = 0, int32 InMaskG = 0, int32 InMaskB = 0, int32 InMaskA = 0) :
		Mask(InMask),
		MaskR(InMaskR),
		MaskG(InMaskG),
		MaskB(InMaskB),
		MaskA(InMaskA)
	{}

	FExpressionOutput(FString InOutputName, int32 InMask = 0, int32 InMaskR = 0, int32 InMaskG = 0, int32 InMaskB = 0, int32 InMaskA = 0) :
		OutputName(InOutputName),
		Mask(InMask),
		MaskR(InMaskR),
		MaskG(InMaskG),
		MaskB(InMaskB),
		MaskA(InMaskA)
	{}
};

//
//	FMaterialInput
//

template<class InputType> struct FMaterialInput : FExpressionInput
{
	uint32	UseConstant : 1;
	InputType	Constant;
};

struct FColorMaterialInput : FMaterialInput<FColor>
{
#if WITH_EDITOR
	ENGINE_API int32 CompileWithDefault(class FMaterialCompiler* Compiler, EMaterialProperty Property);
#endif  // WITH_EDITOR
	/** ICPPStructOps interface */
	ENGINE_API bool Serialize(FArchive& Ar);
};

template<>
struct TStructOpsTypeTraits<FColorMaterialInput>
	: public TStructOpsTypeTraitsBase
{
	enum
	{
		WithSerializer = true,
	};
};

struct FScalarMaterialInput : FMaterialInput<float>
{
#if WITH_EDITOR
	ENGINE_API int32 CompileWithDefault(class FMaterialCompiler* Compiler, EMaterialProperty Property);
#endif  // WITH_EDITOR
	/** ICPPStructOps interface */
	ENGINE_API bool Serialize(FArchive& Ar);
};

template<>
struct TStructOpsTypeTraits<FScalarMaterialInput>
	: public TStructOpsTypeTraitsBase
{
	enum
	{
		WithSerializer = true,
	};
};

struct FVectorMaterialInput : FMaterialInput<FVector>
{
#if WITH_EDITOR
	ENGINE_API int32 CompileWithDefault(class FMaterialCompiler* Compiler, EMaterialProperty Property);
#endif  // WITH_EDITOR
	/** ICPPStructOps interface */
	ENGINE_API bool Serialize(FArchive& Ar);
};

template<>
struct TStructOpsTypeTraits<FVectorMaterialInput>
	: public TStructOpsTypeTraitsBase
{
	enum
	{
		WithSerializer = true,
	};
};

struct FVector2MaterialInput : FMaterialInput<FVector2D>
{
#if WITH_EDITOR
	ENGINE_API int32 CompileWithDefault(class FMaterialCompiler* Compiler, EMaterialProperty Property);
#endif  // WITH_EDITOR
	/** ICPPStructOps interface */
	ENGINE_API bool Serialize(FArchive& Ar);
};

template<>
struct TStructOpsTypeTraits<FVector2MaterialInput>
	: public TStructOpsTypeTraitsBase
{
	enum
	{
		WithSerializer = true,
	};
};

struct FMaterialAttributesInput : FExpressionInput
{
	FMaterialAttributesInput() 
	: PropertyConnectedBitmask(0)
	{ 
		// ensure PropertyConnectedBitmask can contain all properties.
		static_assert((uint32)(MP_MAX)-1 <= (8 * sizeof(PropertyConnectedBitmask)), "PropertyConnectedBitmask cannot contain entire EMaterialProperty enumeration.");
	}

#if WITH_EDITOR
	ENGINE_API int32 CompileWithDefault(class FMaterialCompiler* Compiler, EMaterialProperty Property);
#endif  // WITH_EDITOR
	ENGINE_API bool IsConnected(EMaterialProperty Property) { return ((PropertyConnectedBitmask >> (uint32)Property) & 0x1) != 0; }
	ENGINE_API bool IsConnected() const { return FExpressionInput::IsConnected(); }
	ENGINE_API void SetConnectedProperty(EMaterialProperty Property, bool bIsConnected) 
	{
		PropertyConnectedBitmask = bIsConnected ? PropertyConnectedBitmask | (1 << (uint32)Property) : PropertyConnectedBitmask & ~(1 << (uint32)Property);
	}

	/** ICPPStructOps interface */
	ENGINE_API bool Serialize(FArchive& Ar);

	// each bit corresponds to EMaterialProperty connection status.
	uint32 PropertyConnectedBitmask;
};

template<>
struct TStructOpsTypeTraits<FMaterialAttributesInput>
	: public TStructOpsTypeTraitsBase
{
	enum
	{
		WithSerializer = true,
	};
};