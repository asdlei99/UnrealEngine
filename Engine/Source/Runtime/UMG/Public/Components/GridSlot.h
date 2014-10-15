// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Components/PanelSlot.h"
#include "SlateWrapperTypes.h"

#include "GridSlot.generated.h"

/**
 * A slot for UGridPanel, these slots all share the same size as the largest slot
 * in the grid.
 */
UCLASS()
class UMG_API UGridSlot : public UPanelSlot
{
	GENERATED_UCLASS_BODY()

public:

	/** The alignment of the object horizontally. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Layout (Grid Slot)")
	TEnumAsByte<EHorizontalAlignment> HorizontalAlignment;

	/** The alignment of the object vertically. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Layout (Grid Slot)")
	TEnumAsByte<EVerticalAlignment> VerticalAlignment;
	
	/** The row index of the cell this slot is in */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=( UIMin = "0" ), Category="Layout (Grid Slot)")
	int32 Row;
	
	/**  */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Layout (Grid Slot)")
	int32 RowSpan;

	/** The column index of the cell this slot is in */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=( UIMin = "0" ), Category="Layout (Grid Slot)")
	int32 Column;

	/**  */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Layout (Grid Slot)")
	int32 ColumnSpan;

	/** Positive values offset this cell to be hit-tested and drawn on top of others. Default is 0; i.e. no offset. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Layout (Grid Slot)")
	int32 Layer;

	/** Offset this slot's content by some amount; positive values offset to lower right*/
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Layout (Grid Slot)")
	FVector2D Nudge;

	/** Sets the row index of the slot, this determines what cell the slot is in the panel */
	UFUNCTION(BlueprintCallable, Category="Layout (Grid Slot)")
	void SetRow(int32 InRow);

	/** How many rows this this slot spans over */
	UFUNCTION(BlueprintCallable, Category="Layout (Grid Slot)")
	void SetRowSpan(int32 InRowSpan);

	/** Sets the column index of the slot, this determines what cell the slot is in the panel */
	UFUNCTION(BlueprintCallable, Category="Layout (Grid Slot)")
	void SetColumn(int32 InColumn);

	/** How many columns this slot spans over */
	UFUNCTION(BlueprintCallable, Category="Layout (Grid Slot)")
	void SetColumnSpan(int32 InColumnSpan);

	/**  */
	UFUNCTION(BlueprintCallable, Category="Layout (Grid Slot)")
	void SetHorizontalAlignment(EHorizontalAlignment InHorizontalAlignment);

	/**  */
	UFUNCTION(BlueprintCallable, Category="Layout (Grid Slot)")
	void SetVerticalAlignment(EVerticalAlignment InVerticalAlignment);

	// UPanelSlot interface
	virtual void SynchronizeProperties() override;
	// End of UPanelSlot interface

	/** Builds the underlying FSlot for the Slate layout panel. */
	void BuildSlot(TSharedRef<SGridPanel> GridPanel);

	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	//TODO UMG Slots should hold weak or shared refs to slots.

	/** A raw pointer to the slot to allow us to adjust the size, padding...etc at runtime. */
	SGridPanel::FSlot* Slot;
};