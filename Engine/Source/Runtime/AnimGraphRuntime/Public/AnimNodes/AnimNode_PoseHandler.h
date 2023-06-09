// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Animation/AnimNodeBase.h"
#include "Animation/PoseAsset.h"
#include "Animation/AnimNode_AssetPlayerBase.h"
#include "AnimNode_PoseHandler.generated.h"

// Evaluates a point in an anim sequence, using a specific time input rather than advancing time internally.
// Typically the playback position of the animation for this node will represent something other than time, like jump height.
// This node will not trigger any notifies present in the associated sequence.
USTRUCT()
struct ANIMGRAPHRUNTIME_API FAnimNode_PoseHandler : public FAnimNode_AssetPlayerBase
{
	GENERATED_USTRUCT_BODY()
public:
	// The animation sequence asset to evaluate
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (PinHiddenByDefault))
	UPoseAsset* PoseAsset;

public:	
	FAnimNode_PoseHandler()
	{
	}

	// FAnimNode_AssetPlayerBase interface
	virtual float GetCurrentAssetTime() { return 0.f; }
	virtual float GetCurrentAssetLength() { return 0.f; }
	// End of FAnimNode_AssetPlayerBase interface

	// FAnimNode_Base interface
	virtual void Initialize(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones(const FAnimationCacheBonesContext& Context) override;
	virtual void UpdateAssetPlayer(const FAnimationUpdateContext& Context) override;
	virtual void OverrideAsset(UAnimationAsset* NewAsset) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	// End of FAnimNode_Base interface

	// FAnimNode_AssetPlayerBase Interface
	virtual float GetAccumulatedTime() {return 0.f;}
	virtual void SetAccumulatedTime(const float& NewTime) {}
	virtual UAnimationAsset* GetAnimAsset() {return PoseAsset;}
	// End of FAnimNode_AssetPlayerBase Interface

protected:
	TWeakObjectPtr<UPoseAsset> CurrentPoseAsset;
	FAnimExtractContext PoseExtractContext;
	// weight to blend pose per joint - has to be cached whenever cache bones for LOD
	// note this is for mesh bone
	TArray<float> BoneBlendWeights;
	TArray<FSmartNameMapping::UID> PoseUIDList;

private:
	void UpdatePoseAssetProperty(struct FAnimInstanceProxy* InstanceProxy);
};

