// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "BlueprintNativeCodeGenPCH.h"
#include "NativeCodeGenerationTool.h"
#include "Editor.h"
#include "Dialogs.h"
#include "EditorStyle.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/UserDefinedEnum.h"
#include "Engine/UserDefinedStruct.h"
#include "SourceCodeNavigation.h"
#include "DesktopPlatformModule.h" // for InvalidateMakefiles()
#include "BlueprintNativeCodeGenUtils.h"
//#include "Editor/KismetCompiler/Public/BlueprintCompilerCppBackendInterface.h"
#include "Developer/BlueprintCompilerCppBackend/Public/BlueprintCompilerCppBackendGatherDependencies.h"
#include "IBlueprintCompilerCppBackendModule.h" // for GetBaseFilename()
#include "KismetCompilerModule.h"

#define LOCTEXT_NAMESPACE "NativeCodeGenerationTool"

//
//	THE CODE SHOULD BE MOVED TO GAMEPROJECTGENERATION
//


struct FGeneratedCodeData
{
	FGeneratedCodeData(UBlueprint& InBlueprint) 
		: Blueprint(&InBlueprint)
	{
		FName GeneratedClassName, SkeletonClassName;
		InBlueprint.GetBlueprintClassNames(GeneratedClassName, SkeletonClassName);
		ClassName = GeneratedClassName.ToString();
		BaseFilename = IBlueprintCompilerCppBackendModule::GetBaseFilename(&InBlueprint);

		GatherUserDefinedDependencies(InBlueprint);
	}

	FString TypeDependencies;
	FString ErrorString;
	FString ClassName;
	FString BaseFilename;
	TWeakObjectPtr<UBlueprint> Blueprint;
	TSet<UField*> DependentObjects;
	TSet<UBlueprintGeneratedClass*> UnconvertedNeededClasses;

	void GatherUserDefinedDependencies(UBlueprint& InBlueprint)
	{
		FGatherConvertedClassDependencies ClassDependencies(InBlueprint.GeneratedClass);
		for (auto Iter : ClassDependencies.ConvertedClasses)
		{
			DependentObjects.Add(Iter);
		}
		for (auto Iter : ClassDependencies.ConvertedStructs)
		{
			DependentObjects.Add(Iter);
		}
		for (auto Iter : ClassDependencies.ConvertedEnum)
		{
			DependentObjects.Add(Iter);
		}

		if (DependentObjects.Num())
		{
			TypeDependencies = LOCTEXT("ConvertedDependencies", "Converted Dependencies:\n").ToString();
		}
		else
		{
			TypeDependencies = LOCTEXT("NoConvertedAssets", "No Converted Dependencies was found.\n").ToString();
		}

		for (auto Obj : DependentObjects)
		{
			TypeDependencies += FString::Printf(TEXT("%s \t%s\n"), *Obj->GetClass()->GetName(), *Obj->GetPathName());
		}
		DependentObjects.Add(InBlueprint.GeneratedClass);

		bool bUnconvertedHeader = false;
		for (auto Asset : ClassDependencies.Assets)
		{
			if (auto BPGC = Cast<UBlueprintGeneratedClass>(Asset))
			{
				UnconvertedNeededClasses.Add(BPGC);
				if (!bUnconvertedHeader)
				{
					bUnconvertedHeader = true;
					TypeDependencies += LOCTEXT("NoConvertedAssets", "\nUnconverted Dependencies, that require a warpper struct:\n").ToString();
				}
				TypeDependencies += FString::Printf(TEXT("%s \t%s\n"), *BPGC->GetClass()->GetName(), *BPGC->GetPathName());
			}
		}
	}

	static FString DefaultHeaderDir()
	{
		auto DefaultSourceDir = FPaths::ConvertRelativePathToFull(FPaths::GameSourceDir());
		return FPaths::Combine(*DefaultSourceDir, FApp::GetGameName(), TEXT("Public"));
	}

	static FString DefaultSourceDir()
	{
		auto DefaultSourceDir = FPaths::ConvertRelativePathToFull(FPaths::GameSourceDir());
		return FPaths::Combine(*DefaultSourceDir, FApp::GetGameName(), TEXT("Private"));
	}

	FString HeaderFileName() const
	{
		return BaseFilename + TEXT(".h");
	}

	FString SourceFileName() const
	{
		return BaseFilename + TEXT(".cpp");
	}

	bool Save(const FString& HeaderDirPath, const FString& CppDirPath)
	{
		if (!Blueprint.IsValid())
		{
			ErrorString += LOCTEXT("InvalidBlueprint", "Invalid Blueprint\n").ToString();
			return false;
		}

		const int WorkParts = 3 + (4 * DependentObjects.Num()) + (2 * UnconvertedNeededClasses.Num());
		FScopedSlowTask SlowTask(WorkParts, LOCTEXT("GeneratingCppFiles", "Generating C++ files.."));
		SlowTask.MakeDialog();

		TArray<FString> CreatedFiles;
		for(auto Obj : DependentObjects)
		{
			SlowTask.EnterProgressFrame();

			TSharedPtr<FString> HeaderSource(new FString());
			TSharedPtr<FString> CppSource(new FString());
			FBlueprintNativeCodeGenUtils::GenerateCppCode(Obj, HeaderSource, CppSource);
			SlowTask.EnterProgressFrame();

			const FString BackendBaseFilename = IBlueprintCompilerCppBackendModule::GetBaseFilename(Obj);

			const FString FullHeaderFilename = FPaths::Combine(*HeaderDirPath, *(BackendBaseFilename + TEXT(".h")));
			const bool bHeaderSaved = FFileHelper::SaveStringToFile(*HeaderSource, *FullHeaderFilename);
			if (!bHeaderSaved)
			{
				ErrorString += FString::Printf(*LOCTEXT("HeaderNotSaved", "Header file wasn't saved. Check log for details. %s\n").ToString(), *Obj->GetPathName());
			}
			else
			{
				CreatedFiles.Add(FullHeaderFilename);
			}

			SlowTask.EnterProgressFrame();
			if (!CppSource->IsEmpty())
			{
				const FString NewCppFilename = FPaths::Combine(*CppDirPath, *(BackendBaseFilename + TEXT(".cpp")));
				const bool bCppSaved = FFileHelper::SaveStringToFile(*CppSource, *NewCppFilename);
				if (!bCppSaved)
				{
					ErrorString += FString::Printf(*LOCTEXT("CppNotSaved", "Cpp file wasn't saved. Check log for details. %s\n").ToString(), *Obj->GetPathName());
				}
				else
				{
					CreatedFiles.Add(NewCppFilename);
				}
			}
		}

		for (auto BPGC : UnconvertedNeededClasses)
		{
			SlowTask.EnterProgressFrame();

			IKismetCompilerInterface& Compiler = FModuleManager::LoadModuleChecked<IKismetCompilerInterface>(KISMET_COMPILER_MODULENAME);
			const FString HeaderSource = Compiler.GenerateCppWrapper(BPGC);

			SlowTask.EnterProgressFrame();

			const FString BackendBaseFilename = IBlueprintCompilerCppBackendModule::GetBaseFilename(BPGC);

			const FString FullHeaderFilename = FPaths::Combine(*HeaderDirPath, *(BackendBaseFilename + TEXT(".h")));
			const bool bHeaderSaved = FFileHelper::SaveStringToFile(HeaderSource, *FullHeaderFilename);
			if (!bHeaderSaved)
			{
				ErrorString += FString::Printf(*LOCTEXT("HeaderNotSaved", "Header file wasn't saved. Check log for details. %s\n").ToString(), *BPGC->GetPathName());
			}
			else
			{
				CreatedFiles.Add(FullHeaderFilename);
			}
		}

		SlowTask.EnterProgressFrame();

		bool bGenerateProjectFiles = true;
		{
			bool bProjectHadCodeFiles = false;
			{
				TArray<FString> OutProjectCodeFilenames;
				IFileManager::Get().FindFilesRecursive(OutProjectCodeFilenames, *FPaths::GameSourceDir(), TEXT("*.h"), true, false, false);
				IFileManager::Get().FindFilesRecursive(OutProjectCodeFilenames, *FPaths::GameSourceDir(), TEXT("*.cpp"), true, false, false);
				bProjectHadCodeFiles = OutProjectCodeFilenames.Num() > 0;
			}

			TArray<FString> CreatedFilesForExternalAppRead;
			CreatedFilesForExternalAppRead.Reserve(CreatedFiles.Num());
			for (const FString& CreatedFile : CreatedFiles)
			{
				CreatedFilesForExternalAppRead.Add(IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*CreatedFile));
			}

			// First see if we can avoid a full generation by adding the new files to an already open project
			if (bProjectHadCodeFiles && FSourceCodeNavigation::AddSourceFiles(CreatedFilesForExternalAppRead))
			{
				// We successfully added the new files to the solution, but we still need to run UBT with -gather to update any UBT makefiles
				if (FDesktopPlatformModule::Get()->InvalidateMakefiles(FPaths::RootDir(), FPaths::GetProjectFilePath(), GWarn))
				{
					// We managed the gather, so we can skip running the full generate
					bGenerateProjectFiles = false;
				}
			}
		}

		SlowTask.EnterProgressFrame();

		bool bProjectFileUpdated = true;
		if (bGenerateProjectFiles)
		{
			// Generate project files if we happen to be using a project file.
			if (!FDesktopPlatformModule::Get()->GenerateProjectFiles(FPaths::RootDir(), FPaths::GetProjectFilePath(), GWarn))
			{
				ErrorString += LOCTEXT("FailedToGenerateProjectFiles", "Failed to generate project files.").ToString();
				bProjectFileUpdated = false;
			}
		}

		return ErrorString.IsEmpty();
	}
};

class SSimpleDirectoryPicker : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSimpleDirectoryPicker){}
		SLATE_ARGUMENT(FString, Directory)
		SLATE_ARGUMENT(FString, File)
		SLATE_ARGUMENT(FText, Message)
		SLATE_ATTRIBUTE(bool, IsEnabled)
	SLATE_END_ARGS()

	FString File;
	FString Directory;
	FText Message;

	TSharedPtr<SEditableTextBox> EditableTextBox;

	FString GetFilePath() const
	{
		return FPaths::Combine(*Directory, *File);
	}

	FText GetFilePathText() const
	{
		return FText::FromString(GetFilePath());
	}

	FReply BrowseHeaderDirectory()
	{
		PromptUserForDirectory(Directory, Message.ToString(), Directory);

		//workaround for UI problem
		EditableTextBox->SetText(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SSimpleDirectoryPicker::GetFilePathText)));

		return FReply::Handled();
	}

	void Construct(const FArguments& InArgs)
	{
		Directory = InArgs._Directory;
		File = InArgs._File;
		Message = InArgs._Message;

		TSharedPtr<SButton> OpenButton;
		ChildSlot
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SAssignNew(EditableTextBox, SEditableTextBox)
				.Text(this, &SSimpleDirectoryPicker::GetFilePathText)
				.IsReadOnly(true)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SAssignNew(OpenButton, SButton)
				.ToolTipText(Message)
				.OnClicked(this, &SSimpleDirectoryPicker::BrowseHeaderDirectory)
				[
					SNew(SImage)
					.Image(FEditorStyle::GetBrush("PropertyWindow.Button_Ellipsis"))
				]
			]
		];

		OpenButton->SetEnabled(InArgs._IsEnabled);
	}
};

class SNativeCodeGenerationDialog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNativeCodeGenerationDialog){}
		SLATE_ARGUMENT(TSharedPtr<SWindow>, ParentWindow)
		SLATE_ARGUMENT(TSharedPtr<FGeneratedCodeData>, GeneratedCodeData)
	SLATE_END_ARGS()

private:
	
	// Child widgets
	TSharedPtr<SSimpleDirectoryPicker> HeaderDirectoryBrowser;
	TSharedPtr<SSimpleDirectoryPicker> SourceDirectoryBrowser;
	TSharedPtr<SErrorText> ErrorWidget;
	// 
	TWeakPtr<SWindow> WeakParentWindow;
	TSharedPtr<FGeneratedCodeData> GeneratedCodeData;
	bool bSaved;

	void CloseParentWindow()
	{
		auto ParentWindow = WeakParentWindow.Pin();
		if (ParentWindow.IsValid())
		{
			ParentWindow->RequestDestroyWindow();
		}
	}

	bool IsEditable() const
	{
		return !bSaved && GeneratedCodeData->ErrorString.IsEmpty();
	}

	FReply OnButtonClicked()
	{
		if (IsEditable())
		{
			bSaved = GeneratedCodeData->Save(HeaderDirectoryBrowser->Directory, SourceDirectoryBrowser->Directory);
			ErrorWidget->SetError(GeneratedCodeData->ErrorString);
		}
		else
		{
			CloseParentWindow();
		}
		
		return FReply::Handled();
	}

	FText ButtonText() const
	{
		return IsEditable() ? LOCTEXT("Generate", "Generate") : LOCTEXT("Close", "Close");
	}

	FText GetClassName() const
	{
		return GeneratedCodeData.IsValid() ? FText::FromString(GeneratedCodeData->ClassName) : FText::GetEmpty();
	}

public:

	void Construct(const FArguments& InArgs)
	{
		GeneratedCodeData = InArgs._GeneratedCodeData;
		bSaved = false;
		WeakParentWindow = InArgs._ParentWindow;

		ChildSlot
		[
			SNew(SBorder)
			.Padding(4.f)
			.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.Padding(4.0f)
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ClassName", "Class Name"))
				]
				+ SVerticalBox::Slot()
				.Padding(4.0f)
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(this, &SNativeCodeGenerationDialog::GetClassName)
				]
				+ SVerticalBox::Slot()
				.Padding(4.0f)
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("HeaderPath", "Header Path"))
				]
				+ SVerticalBox::Slot()
				.Padding(4.0f)
				.AutoHeight()
				[
					SAssignNew(HeaderDirectoryBrowser, SSimpleDirectoryPicker)
					.Directory(FGeneratedCodeData::DefaultHeaderDir())
					.File(GeneratedCodeData->HeaderFileName())
					.Message(LOCTEXT("HeaderDirectory", "Header Directory"))
					.IsEnabled(this, &SNativeCodeGenerationDialog::IsEditable)
				]
				+ SVerticalBox::Slot()
				.Padding(4.0f)
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("SourcePath", "Source Path"))
				]
				+ SVerticalBox::Slot()
				.Padding(4.0f)
				.AutoHeight()
				[
					SAssignNew(SourceDirectoryBrowser, SSimpleDirectoryPicker)
					.Directory(FGeneratedCodeData::DefaultSourceDir())
					.File(GeneratedCodeData->SourceFileName())
					.Message(LOCTEXT("SourceDirectory", "Source Directory"))
					.IsEnabled(this, &SNativeCodeGenerationDialog::IsEditable)
				]
				+ SVerticalBox::Slot()
				.Padding(4.0f)
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Dependencies", "Dependencies"))
				]
				+ SVerticalBox::Slot()
				.Padding(4.0f)
				.AutoHeight()
				[
					SNew(SBox)
					.WidthOverride(360.0f)
					.HeightOverride(200.0f)
					[
						SNew(SMultiLineEditableTextBox)
						.IsReadOnly(true)
						.Text(FText::FromString(GeneratedCodeData->TypeDependencies))
					]
				]
				+ SVerticalBox::Slot()
				.Padding(4.0f)
				.AutoHeight()
				[
					SAssignNew(ErrorWidget, SErrorText)
				]
				+ SVerticalBox::Slot()
				.Padding(4.0f)
				.AutoHeight()
				.HAlign(HAlign_Right)
				[
					SNew(SButton)
					.Text(this, &SNativeCodeGenerationDialog::ButtonText)
					.OnClicked(this, &SNativeCodeGenerationDialog::OnButtonClicked)
				]
			]
		];

		ErrorWidget->SetError(GeneratedCodeData->ErrorString);
	}
};

void FNativeCodeGenerationTool::Open(UBlueprint& Blueprint, TSharedRef< class FBlueprintEditor> Editor)
{
	TSharedRef<FGeneratedCodeData> GeneratedCodeData(new FGeneratedCodeData(Blueprint));

	TSharedRef<SWindow> PickerWindow = SNew(SWindow)
		.Title(LOCTEXT("GenerateNativeCode", "Generate Native Code"))
		.SizingRule(ESizingRule::Autosized)
		.ClientSize(FVector2D(0.f, 300.f))
		.SupportsMaximize(false)
		.SupportsMinimize(false);

	TSharedRef<SNativeCodeGenerationDialog> CodeGenerationDialog = SNew(SNativeCodeGenerationDialog)
		.ParentWindow(PickerWindow)
		.GeneratedCodeData(GeneratedCodeData);

	PickerWindow->SetContent(CodeGenerationDialog);
	GEditor->EditorAddModalWindow(PickerWindow);
}

bool FNativeCodeGenerationTool::CanGenerate(const UBlueprint& Blueprint)
{
	return (Blueprint.Status == EBlueprintStatus::BS_UpToDate || Blueprint.Status == EBlueprintStatus::BS_UpToDateWithWarnings)
		&& (Blueprint.BlueprintType == EBlueprintType::BPTYPE_Normal || Blueprint.BlueprintType == EBlueprintType::BPTYPE_FunctionLibrary)
		&& Cast<UBlueprintGeneratedClass>(Blueprint.GeneratedClass);
}

#undef LOCTEXT_NAMESPACE