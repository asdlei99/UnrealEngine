// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SlateWrapperTypes.h"

#include "ScaleBoxSlot.generated.h"

/** The Slot for the UScaleBoxSlot, contains the widget displayed in a button's single slot */
UCLASS()
class UMG_API UScaleBoxSlot : public UPanelSlot
{
	GENERATED_UCLASS_BODY()

public:
	
	/** The padding area between the slot and the content it contains. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Layout (ScaleBox Slot)")
	FMargin Padding;

	/** The alignment of the object horizontally. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Layout (ScaleBox Slot)")
	TEnumAsByte<EHorizontalAlignment> HorizontalAlignment;

	/** The alignment of the object vertically. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Layout (ScaleBox Slot)" )
	TEnumAsByte<EVerticalAlignment> VerticalAlignment;

public:

	UFUNCTION(BlueprintCallable, Category="Layout (ScaleBox Slot)")
	void SetPadding(FMargin InPadding);

	UFUNCTION(BlueprintCallable, Category="Layout (ScaleBox Slot)")
	void SetHorizontalAlignment(EHorizontalAlignment InHorizontalAlignment);

	UFUNCTION(BlueprintCallable, Category="Layout (ScaleBox Slot)")
	void SetVerticalAlignment(EVerticalAlignment InVerticalAlignment);

	// UPanelSlot interface
	virtual void SynchronizeProperties() override;
	// End of UPanelSlot interface

	/** Builds the underlying slot for the slate ScaleBox. */
	void BuildSlot(TSharedRef<SScaleBox> InScaleBox);

	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:

	/** A pointer to the button to allow us to adjust the size, padding...etc at runtime. */
	TSharedPtr<SScaleBox> ScaleBox;
};
