// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once

class FAssetTypeActions_SoundWave : public FAssetTypeActions_SoundBase
{
public:
	// IAssetTypeActions Implementation
	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_SoundWave", "Sound Wave"); }
	virtual FColor GetTypeColor() const override { return FColor(0, 0, 255); }
	virtual UClass* GetSupportedClass() const override;
	virtual void GetActions( const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder ) override;
	virtual void OpenAssetEditor( const TArray<UObject*>& InObjects, TSharedPtr<class IToolkitHost> EditWithinLevelEditor = TSharedPtr<IToolkitHost>() ) override;
	virtual bool CanFilter() override { return true; }
	virtual bool IsImportedAsset() const override { return true; }
	virtual void GetResolvedSourceFilePaths(const TArray<UObject*>& TypeAssets, TArray<FString>& OutSourceFilePaths) const override;
	
private:

	/** Handler for when CompressionPreview is selected */
	void ExecuteCompressionPreview(TArray<TWeakObjectPtr<USoundWave>> Objects);

	/** Creates a SoundCue of the same name for the sound, if one does not already exist */
	void ExecuteCreateSoundCue(TArray<TWeakObjectPtr<USoundWave>> Objects);
};