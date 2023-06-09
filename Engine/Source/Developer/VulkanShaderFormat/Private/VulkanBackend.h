// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "hlslcc.h"
#include "LanguageSpec.h"

class FVulkanLanguageSpec : public ILanguageSpec
{
protected:
	bool bShareSamplers;

public:
	FVulkanLanguageSpec(bool bInShareSamplers)
		: bShareSamplers(bInShareSamplers)
	{}

	virtual bool SupportsDeterminantIntrinsic() const override
	{
		return true;
	}

	virtual bool SupportsTransposeIntrinsic() const override
	{
		return true;
	}

	virtual bool SupportsIntegerModulo() const override
	{
		return true;
	}

	virtual bool SupportsMatrixConversions() const override { return true; }

	virtual void SetupLanguageIntrinsics(_mesa_glsl_parse_state* State, exec_list* ir) override;

	virtual bool AllowsSharingSamplers() const override { return bShareSamplers; }
};

class ir_variable;

// Generates Vulkan compliant code from IR tokens
#ifdef __GNUC__
#pragma GCC visibility push(default)
#endif // __GNUC__

struct FVulkanBindingTable
{
	enum EBindingType : uint16
	{
		TYPE_COMBINED_IMAGE_SAMPLER,
		TYPE_SAMPLER_BUFFER,
		TYPE_UNIFORM_BUFFER,
		TYPE_PACKED_UNIFORM_BUFFER,
		TYPE_SAMPLER,
		TYPE_IMAGE,

		TYPE_MAX,
	};

	struct FBinding
	{
		FBinding();
		FBinding(const char* InName, int32 InIndex, EBindingType InType, int8 InSubType);

		char		Name[256];
		int32		Index;
		EBindingType	Type;
		int8		SubType;	// HLSL CC subtype, PACKED_TYPENAME_HIGHP and etc
	};

	FVulkanBindingTable(EHlslShaderFrequency ShaderStage) : Stage(ShaderStage){}

	int32 RegisterBinding(const char* InName, const char* BlockName, EBindingType Type);

	int32 FindBinding(const char* InName) const;

	const TArray<FBinding>& GetBindings() const { return Bindings; }

private:
	// Previouse implementation supported bindings only for textures.
	// However, layout(binding=%d) must be also used for uniform buffers.

	EHlslShaderFrequency Stage;
	TArray<FBinding> Bindings;
};

struct FVulkanCodeBackend : public FCodeBackend
{
	FVulkanCodeBackend(	unsigned int InHlslCompileFlags,
						FVulkanBindingTable& InBindingTable,
						EHlslCompileTarget InTarget) :
		FCodeBackend(InHlslCompileFlags, InTarget),
		BindingTable(InBindingTable)
	{
	}

	virtual char* GenerateCode(struct exec_list* ir, struct _mesa_glsl_parse_state* ParseState, EHlslShaderFrequency Frequency) override;

	// Return false if there were restrictions that made compilation fail
	virtual bool ApplyAndVerifyPlatformRestrictions(exec_list* Instructions, _mesa_glsl_parse_state* ParseState, EHlslShaderFrequency Frequency) override;

	/**
	* Generate a GLSL main() function that calls the entry point and handles
	* reading and writing all input and output semantics.
	* @param Frequency - The shader frequency.
	* @param EntryPoint - The name of the shader entry point.
	* @param Instructions - IR code.
	* @param ParseState - Parse state.
	*/
	virtual bool GenerateMain(EHlslShaderFrequency Frequency, const char* EntryPoint, exec_list* Instructions, _mesa_glsl_parse_state* ParseState) override;

	void FixIntrinsics(_mesa_glsl_parse_state* ParseState, exec_list* ir);

	void GenShaderPatchConstantFunctionInputs(_mesa_glsl_parse_state* ParseState, ir_variable* OutputPatchVar, exec_list &PostCallInstructions);

	void CallPatchConstantFunction(_mesa_glsl_parse_state* ParseState, ir_variable* OutputPatchVar, ir_function_signature* PatchConstantSig, exec_list& DeclInstructions, exec_list &PostCallInstructions);

	ir_function_signature* FindPatchConstantFunction(exec_list* Instructions, _mesa_glsl_parse_state* ParseState);

	FVulkanBindingTable& BindingTable;
};
#ifdef __GNUC__
#pragma GCC visibility pop
#endif // __GNUC__
