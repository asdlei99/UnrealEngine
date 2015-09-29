// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "BlueprintCompilerCppBackendModulePrivatePCH.h"
#include "BlueprintCompilerCppBackendBase.h"
#include "BlueprintCompilerCppBackendUtils.h"
#include "IBlueprintCompilerCppBackendModule.h" // for GetBaseFilename()

void FBlueprintCompilerCppBackendBase::EmitStructProperties(FEmitterLocalContext EmitterContext, UStruct* SourceClass)
{
	// Emit class variables
	for (TFieldIterator<UProperty> It(SourceClass, EFieldIteratorFlags::ExcludeSuper); It; ++It)
	{
		UProperty* Property = *It;
		check(Property);

		Emit(Header, TEXT("\n\tUPROPERTY("));
		{
			TArray<FString> Tags = FEmitHelper::ProperyFlagsToTags(Property->PropertyFlags, nullptr != Cast<UClass>(SourceClass));
			Tags.Emplace(FEmitHelper::HandleRepNotifyFunc(Property));
			Tags.Emplace(FEmitHelper::HandleMetaData(Property, false));
			Tags.Remove(FString());

			FString AllTags;
			FEmitHelper::ArrayToString(Tags, AllTags, TEXT(", "));
			Emit(Header, *AllTags);
		}
		Emit(Header, TEXT(")\n"));
		Emit(Header, TEXT("\t"));
		const FString CppDeclaration = EmitterContext.ExportCppDeclaration(Property, EExportedDeclaration::Member, EPropertyExportCPPFlags::CPPF_CustomTypeName | EPropertyExportCPPFlags::CPPF_BlueprintCppBackend);
		Emit(Header, *CppDeclaration);
		Emit(Header, TEXT(";\n"));
	}
}

void FBlueprintCompilerCppBackendBase::DeclareDelegates(FEmitterLocalContext EmitterContext, UClass* SourceClass, TIndirectArray<FKismetFunctionContext>& Functions)
{
	// MC DELEGATE DECLARATION
	{
		Emit(Header, TEXT("\n"));
		auto DelegateDeclarations = FEmitHelper::EmitMulticastDelegateDeclarations(EmitterContext, SourceClass);
		FString AllDeclarations;
		FEmitHelper::ArrayToString(DelegateDeclarations, AllDeclarations, TEXT(";\n"));
		if (DelegateDeclarations.Num())
		{
			Emit(Header, *AllDeclarations);
			Emit(Header, TEXT(";\n"));
		}
	}

	// GATHER ALL SC DELEGATES
	{
		TArray<UDelegateProperty*> Delegates;
		for (TFieldIterator<UDelegateProperty> It(SourceClass, EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			Delegates.Add(*It);
		}

		for (auto& FuncContext : Functions)
		{
			for (TFieldIterator<UDelegateProperty> It(FuncContext.Function, EFieldIteratorFlags::ExcludeSuper); It; ++It)
			{
				Delegates.Add(*It);
			}
		}

		// remove duplicates, n^2, but n is small:
		for (int32 I = 0; I < Delegates.Num(); ++I)
		{
			UFunction* TargetFn = Delegates[I]->SignatureFunction;
			for (int32 J = I + 1; J < Delegates.Num(); ++J)
			{
				if (TargetFn == Delegates[J]->SignatureFunction)
				{
					// swap erase:
					Delegates[J] = Delegates[Delegates.Num() - 1];
					Delegates.RemoveAt(Delegates.Num() - 1);
				}
			}
		}

		Emit(Header, TEXT("\n"));
		auto DelegateDeclarations = FEmitHelper::EmitSinglecastDelegateDeclarations(EmitterContext, Delegates);
		FString AllDeclarations;
		FEmitHelper::ArrayToString(DelegateDeclarations, AllDeclarations, TEXT(";\n"));
		if (DelegateDeclarations.Num())
		{
			Emit(Header, *AllDeclarations);
			Emit(Header, TEXT(";\n"));
		}
	}
}

void FBlueprintCompilerCppBackendBase::GenerateCodeFromClass(UClass* SourceClass, TIndirectArray<FKismetFunctionContext>& Functions, bool bGenerateStubsOnly)
{
	// use GetBaseFilename() so that we can coordinate #includes and filenames
	auto CleanCppClassName = FEmitHelper::GetBaseFilename(SourceClass);
	CppClassName = FEmitHelper::GetCppName(SourceClass);
	
	FGatherConvertedClassDependencies Dependencies(SourceClass);
	FEmitterLocalContext EmitterContext(Dependencies);
	EmitFileBeginning(CleanCppClassName, &Dependencies);

	// Class declaration
	const bool bIsInterface = SourceClass->IsChildOf<UInterface>();
	if (bIsInterface)
	{
		Emit(Header, TEXT("UINTERFACE(Blueprintable"));
		EmitReplaceConvertedMetaData(SourceClass);
		Emit(Header, *FString::Printf(TEXT(")\nclass %s : public UInterface\n{\n\tGENERATED_BODY()\n};\n"), *FEmitHelper::GetCppName(SourceClass, true)));
		Emit(Header, *FString::Printf(TEXT("\nclass %s"), *CppClassName));
	}
	else
	{
		Emit(Header, TEXT("UCLASS("));
		if (!SourceClass->IsChildOf<UBlueprintFunctionLibrary>())
		{
			Emit(Header, TEXT("Blueprintable, BlueprintType"));
		}
		EmitReplaceConvertedMetaData(SourceClass);
		Emit(Header, TEXT(")\n"));

		UClass* SuperClass = SourceClass->GetSuperClass();
		Emit(Header, *FString::Printf(TEXT("class %s : public %s"), *CppClassName, *FEmitHelper::GetCppName(SuperClass)));

		for (auto& ImplementedInterface : SourceClass->Interfaces)
		{
			if (ImplementedInterface.Class)
			{
				Emit(Header, *FString::Printf(TEXT(", public %s"), *FEmitHelper::GetCppName(ImplementedInterface.Class)));
			}
		}
	}

	// Begin scope
	Emit(Header,TEXT("\n{\npublic:\n\tGENERATED_BODY()\n"));

	DeclareDelegates(EmitterContext, SourceClass, Functions);

	EmitStructProperties(EmitterContext, SourceClass);

	// Create the state map
	for (int32 i = 0; i < Functions.Num(); ++i)
	{
		StateMapPerFunction.Add(FFunctionLabelInfo());
		FunctionIndexMap.Add(&Functions[i], i);
	}

	// Emit function declarations and definitions (writes to header and body simultaneously)
	if (Functions.Num() > 0)
	{
		Emit(Header, TEXT("\n"));
	}

	if (!bIsInterface)
	{
		Emit(Header, *FString::Printf(TEXT("\t%s(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());\n\n"), *CppClassName));
		Emit(Body, *FEmitDefaultValueHelper::GenerateConstructor(EmitterContext));

		Emit(Header, TEXT("\n\tvirtual void PostLoadSubobjects(FObjectInstancingGraph* OuterInstanceGraph) override;\n"));
		Emit(Header, TEXT("\n\tstatic void __StaticDependenciesAssets(TArray<FName>& OutPackagePaths);\n"));
	}

	for (int32 i = 0; i < Functions.Num(); ++i)
	{
		if (Functions[i].IsValid())
		{
			ConstructFunction(Functions[i], EmitterContext, bGenerateStubsOnly);
		}
	}

	Emit(Header, *FBackendHelperUMG::WidgetFunctionsInHeader(SourceClass));

	Emit(Header, TEXT("};\n\n"));

	Emit(Body, *FEmitHelper::EmitLifetimeReplicatedPropsImpl(SourceClass, CppClassName, TEXT("")));
}

void FBlueprintCompilerCppBackendBase::DeclareLocalVariables(FKismetFunctionContext& FunctionContext, FEmitterLocalContext& EmitterContext, TArray<UProperty*>& LocalVariables)
{
	for (int32 i = 0; i < LocalVariables.Num(); ++i)
	{
		UProperty* LocalVariable = LocalVariables[i];

		Emit(Body, TEXT("\t"));
		const FString CppDeclaration = EmitterContext.ExportCppDeclaration(LocalVariable, EExportedDeclaration::Local, EPropertyExportCPPFlags::CPPF_CustomTypeName | EPropertyExportCPPFlags::CPPF_BlueprintCppBackend);
		Emit(Body, *CppDeclaration);
		Emit(Body, TEXT("{};\n"));
	}

	if (LocalVariables.Num() > 0)
	{
		Emit(Body, TEXT("\n"));
	}
}

void FBlueprintCompilerCppBackendBase::ConstructFunction(FKismetFunctionContext& FunctionContext, FEmitterLocalContext& EmitterContext, bool bGenerateStubOnly)
{
	if (FunctionContext.IsDelegateSignature())
	{
		return;
	}

	UFunction* Function = FunctionContext.Function;

	UProperty* ReturnValue = NULL;
	TArray<UProperty*> LocalVariables;

	{
		FString FunctionName = FEmitHelper::GetCppName(Function);

		TArray<UProperty*> ArgumentList;

		// Split the function property list into arguments, a return value (if any), and local variable declarations
		for (TFieldIterator<UProperty> It(Function); It; ++It)
		{
			UProperty* Property = *It;
			if (Property->HasAnyPropertyFlags(CPF_Parm))
			{
				if (Property->HasAnyPropertyFlags(CPF_ReturnParm))
				{
					if (ReturnValue == NULL)
					{
						ReturnValue = Property;
						LocalVariables.Add(Property);
					}
					else
					{
						UE_LOG(LogK2Compiler, Error, TEXT("Function %s from graph @@ has more than one return value (named %s and %s)"),
							*FunctionName, *GetPathNameSafe(FunctionContext.SourceGraph), *ReturnValue->GetName(), *Property->GetName());
					}
				}
				else
				{
					ArgumentList.Add(Property);
				}
			}
			else
			{
				LocalVariables.Add(Property);
			}
		}

		bool bAddCppFromBpEventMD = false;
		bool bGenerateAsNativeEventImplementation = false;
		// Get original function declaration
		if (FEmitHelper::ShouldHandleAsNativeEvent(Function)) // BlueprintNativeEvent
		{
			bGenerateAsNativeEventImplementation = true;
			FunctionName += TEXT("_Implementation");
		}
		else if (FEmitHelper::ShouldHandleAsImplementableEvent(Function)) // BlueprintImplementableEvent
		{
			bAddCppFromBpEventMD = true;
		}

		// Emit the declaration
		FString ReturnType;
		if (ReturnValue)
		{
			const uint32 LocalExportCPPFlags = EPropertyExportCPPFlags::CPPF_CustomTypeName
				| EPropertyExportCPPFlags::CPPF_NoConst 
				| EPropertyExportCPPFlags::CPPF_NoRef 
				| EPropertyExportCPPFlags::CPPF_NoStaticArray 
				| EPropertyExportCPPFlags::CPPF_BlueprintCppBackend;
			ReturnType = EmitterContext.ExportCppDeclaration(ReturnValue, EExportedDeclaration::Parameter, LocalExportCPPFlags, true);
		}
		else
		{
			ReturnType = TEXT("void");
		}

		//@TODO: Make the header+body export more uniform
		{
			const FString Start = FString::Printf(TEXT("%s %s%s%s("), *ReturnType, TEXT("%s"), TEXT("%s"), *FunctionName);

			TArray<FString> AdditionalMetaData;
			if (bAddCppFromBpEventMD)
			{
				AdditionalMetaData.Emplace(TEXT("CppFromBpEvent"));
			}

			if (!bGenerateAsNativeEventImplementation)
			{
				Emit(Header, *FString::Printf(TEXT("\t%s\n"), *FEmitHelper::EmitUFuntion(Function, &AdditionalMetaData)));
			}
			Emit(Header, TEXT("\t"));

			if (Function->HasAllFunctionFlags(FUNC_Static))
			{
				Emit(Header, TEXT("static "));
			}

			if (bGenerateAsNativeEventImplementation)
			{
				Emit(Header, TEXT("virtual "));
			}

			Emit(Header, *FString::Printf(*Start, TEXT(""), TEXT("")));
			Emit(Body, *FString::Printf(*Start, *CppClassName, TEXT("::")));

			for (int32 i = 0; i < ArgumentList.Num(); ++i)
			{
				UProperty* ArgProperty = ArgumentList[i];

				if (i > 0)
				{
					Emit(Header, TEXT(", "));
					Emit(Body, TEXT(", "));
				}

				if (ArgProperty->HasAnyPropertyFlags(CPF_OutParm))
				{
					Emit(Header, TEXT("/*out*/ "));
					Emit(Body, TEXT("/*out*/ "));
				}

				const FString TypeDeclaration = EmitterContext.ExportCppDeclaration(ArgProperty, EExportedDeclaration::Parameter, EPropertyExportCPPFlags::CPPF_CustomTypeName | EPropertyExportCPPFlags::CPPF_BlueprintCppBackend);
				Emit(Header, *TypeDeclaration);
				Emit(Body, *TypeDeclaration);
			}

			Emit(Header, TEXT(")"));
			if (bGenerateAsNativeEventImplementation)
			{
				Emit(Header, TEXT(" override"));
			}
			Emit(Header, TEXT(";\n"));
			Emit(Body, TEXT(")\n"));
		}

		// Start the body of the implementation
		Emit(Body, TEXT("{\n"));
	}

	if (!bGenerateStubOnly)
	{
		// Emit local variables
		DeclareLocalVariables(FunctionContext, EmitterContext, LocalVariables);

		const bool bUseSwitchState = FunctionContext.MustUseSwitchState(nullptr);

		// Run thru code looking only at things marked as jump targets, to make sure the jump targets are ordered in order of appearance in the linear execution list
		// Emit code in the order specified by the linear execution list (the first node is always the entry point for the function)
		for (int32 NodeIndex = 0; NodeIndex < FunctionContext.LinearExecutionList.Num(); ++NodeIndex)
		{
			UEdGraphNode* StatementNode = FunctionContext.LinearExecutionList[NodeIndex];
			TArray<FBlueprintCompiledStatement*>* StatementList = FunctionContext.StatementsPerNode.Find(StatementNode);

			if (StatementList != NULL)
			{
				for (int32 StatementIndex = 0; StatementIndex < StatementList->Num(); ++StatementIndex)
				{
					FBlueprintCompiledStatement& Statement = *((*StatementList)[StatementIndex]);

					if (Statement.bIsJumpTarget)
					{
						// Just making sure we number them in order of appearance, so jump statements don't influence the order
						const int32 StateNum = StatementToStateIndex(FunctionContext, &Statement);
					}
				}
			}
		}

		const FString FunctionImplementation = InnerFunctionImplementation(FunctionContext, EmitterContext, bUseSwitchState);
		Emit(Body, *FunctionImplementation);
	}

	const FString ReturnValueString = ReturnValue ? (FString(TEXT(" ")) + FEmitHelper::GetCppName(ReturnValue)) : TEXT("");
	Emit(Body, *FString::Printf(TEXT("\treturn%s;\n"), *ReturnValueString));
	Emit(Body, TEXT("}\n\n"));
}

void FBlueprintCompilerCppBackendBase::GenerateCodeFromEnum(UUserDefinedEnum* SourceEnum)
{
	check(SourceEnum);
	// use GetBaseFilename() so that we can coordinate #includes and filenames
	EmitFileBeginning(FEmitHelper::GetBaseFilename(SourceEnum), nullptr);

	Emit(Header, TEXT("UENUM(BlueprintType"));
	EmitReplaceConvertedMetaData(SourceEnum);
	Emit(Header, TEXT(")\nenum class "));
	Emit(Header, *FEmitHelper::GetCppName(SourceEnum));
	Emit(Header, TEXT(" : uint8\n{"));

	for (int32 Index = 0; Index < SourceEnum->NumEnums(); ++Index)
	{
		const FString ElemName = SourceEnum->GetEnumName(Index);
		const int32 ElemValue = Index;

		const FString& DisplayNameMD = SourceEnum->GetMetaData(TEXT("DisplayName"), ElemValue);// TODO: value or index?
		const FString Meta = DisplayNameMD.IsEmpty() ? FString() : FString::Printf(TEXT("UMETA(DisplayName = \"%s\")"), *DisplayNameMD);
		Emit(Header, *FString::Printf(TEXT("\n\t%s = %d %s,"), *ElemName, ElemValue, *Meta));
	}
	Emit(Header, TEXT("\n};\n"));
}

void FBlueprintCompilerCppBackendBase::EmitReplaceConvertedMetaData(UObject* Obj)
{
	const FString ReplaceConvertedMD = FEmitHelper::GenerateReplaceConvertedMD(Obj);
	if (!ReplaceConvertedMD.IsEmpty())
	{
		TArray<FString> AdditionalMD;
		AdditionalMD.Add(ReplaceConvertedMD);
		Emit(Header, TEXT(","));
		Emit(Header, *FEmitHelper::HandleMetaData(nullptr, false, &AdditionalMD));
	}
}

void FBlueprintCompilerCppBackendBase::GenerateCodeFromStruct(UUserDefinedStruct* SourceStruct)
{
	check(SourceStruct);

	FGatherConvertedClassDependencies Dependencies(SourceStruct);
	FEmitterLocalContext EmitterContext(Dependencies);
	// use GetBaseFilename() so that we can coordinate #includes and filenames
	EmitFileBeginning(FEmitHelper::GetBaseFilename(SourceStruct), &Dependencies);

	const FString NewName = FEmitHelper::GetCppName(SourceStruct);
	Emit(Header, TEXT("USTRUCT(BlueprintType"));
	EmitReplaceConvertedMetaData(SourceStruct);
	Emit(Header, TEXT(")\n"));

	Emit(Header, *FString::Printf(TEXT("struct %s\n{\npublic:\n\tGENERATED_BODY()\n"), *NewName));
	EmitStructProperties(EmitterContext, SourceStruct);
	Emit(Header, *FEmitDefaultValueHelper::GenerateGetDefaultValue(SourceStruct, Dependencies));
	Emit(Header, TEXT("};\n"));
}

void FBlueprintCompilerCppBackendBase::GenerateWrapperForClass(UClass* SourceClass)
{
	FGatherConvertedClassDependencies Dependencies(SourceClass);
	FEmitterLocalContext EmitterContext(Dependencies);

	TArray<UFunction*> FunctionsToGenerate;
	for (auto Func : TFieldRange<UFunction>(SourceClass, EFieldIteratorFlags::ExcludeSuper))
	{
		// Exclude native events.. Unexpected.
		// Exclude delegate signatures.
		static const FName __UCSName(TEXT("UserConstructionScript"));
		if (__UCSName == Func->GetFName())
		{
			continue;
		}

		FunctionsToGenerate.Add(Func);
	}

	TArray<UMulticastDelegateProperty*> MCDelegateProperties;
	for (auto MCDelegateProp : TFieldRange<UMulticastDelegateProperty>(SourceClass, EFieldIteratorFlags::ExcludeSuper))
	{
		MCDelegateProperties.Add(MCDelegateProp);
	}

	const bool bGenerateAnyFunction = (0 != FunctionsToGenerate.Num()) || (0 != MCDelegateProperties.Num());

	// Include standard stuff
	EmitFileBeginning(FEmitHelper::GetBaseFilename(SourceClass), &Dependencies, bGenerateAnyFunction, true);

	// DELEGATES
	const FString DelegatesClassName = FString::Printf(TEXT("U__Delegates__%s"), *FEmitHelper::GetCppName(SourceClass));
	auto GenerateDelegateTypeName = [](UFunction* Func) -> FString
	{
		return FString::Printf(TEXT("F__Delegate__%s"), *FEmitHelper::GetCppName(Func));
	};
	auto GenerateMulticastDelegateTypeName = [](UMulticastDelegateProperty* MCDelegateProp) -> FString
	{
		return FString::Printf(TEXT("F__MulticastDelegate__%s"), *FEmitHelper::GetCppName(MCDelegateProp));
	};
	if (bGenerateAnyFunction)
	{
		Emit(Header, *FString::Printf(TEXT("\nUCLASS()\nclass %s : public UObject\n{\npublic:\n\tGENERATED_BODY()\n"), *DelegatesClassName));
		for (auto Func : FunctionsToGenerate)
		{
			FString ParamNumberStr, Parameters;
			FEmitHelper::ParseDelegateDetails(EmitterContext, Func, Parameters, ParamNumberStr);
			Emit(Header, *FString::Printf(TEXT("\tDECLARE_DYNAMIC_DELEGATE%s(%s%s);\n"), *ParamNumberStr, *GenerateDelegateTypeName(Func), *Parameters));
		}
		for (auto MCDelegateProp : MCDelegateProperties)
		{
			FString ParamNumberStr, Parameters;
			FEmitHelper::ParseDelegateDetails(EmitterContext, MCDelegateProp->SignatureFunction, Parameters, ParamNumberStr);
			Emit(Header, *FString::Printf(TEXT("\tDECLARE_DYNAMIC_MULTICAST_DELEGATE%s(%s%s);\n"), *ParamNumberStr, *GenerateMulticastDelegateTypeName(MCDelegateProp), *Parameters));
		}
		Emit(Header, TEXT("\n};\n"));
	}

	// Begin the struct
	const FString WrapperName = FString::Printf(TEXT("FUnconvertedWrapper__%s"), *FEmitHelper::GetCppName(SourceClass));

	FString ParentStruct;
	auto SuperBPGC = Cast<UBlueprintGeneratedClass>(SourceClass->GetSuperClass());
	if (SuperBPGC && !Dependencies.WillClassBeConverted(SuperBPGC))
	{
		//TODO: include header for super-class wrapper
		ParentStruct = FString::Printf(TEXT("FUnconvertedWrapper__%s"), *FEmitHelper::GetCppName(SourceClass->GetSuperClass()));
	}
	else
	{
		ParentStruct = FString::Printf(TEXT("FUnconvertedWrapper<%s>"), *FEmitHelper::GetCppName(SourceClass->GetSuperClass()));
	}
	
	Emit(Header, *FString::Printf(TEXT("struct %s : public %s\n{\n"), *WrapperName, *ParentStruct));
	// Constructor
	Emit(Header, *FString::Printf(TEXT("\n\t%s(const %s* __InObject) : %s(__InObject){}\n"), *WrapperName, *FEmitHelper::GetCppName(EmitterContext.GetNativeOrConvertedClass(SourceClass)), *ParentStruct));

	// PROPERTIES:
	for (auto Property : TFieldRange<UProperty>(SourceClass, EFieldIteratorFlags::ExcludeSuper))
	{
		//TODO: check if the property is really used?
		const FString TypeDeclaration = Property->IsA<UMulticastDelegateProperty>()
			? FString::Printf(TEXT("%s::%s"), *DelegatesClassName, *GenerateMulticastDelegateTypeName(CastChecked<UMulticastDelegateProperty>(Property)))
			: EmitterContext.ExportCppDeclaration(Property, EExportedDeclaration::Parameter, EPropertyExportCPPFlags::CPPF_CustomTypeName | EPropertyExportCPPFlags::CPPF_BlueprintCppBackend | EPropertyExportCPPFlags::CPPF_NoRef, true);
		Emit(Header, *FString::Printf(TEXT("\t%s& GetRef__%s()"), *TypeDeclaration, *UnicodeToCPPIdentifier(Property->GetName(), false, nullptr)));
		Emit(Header, TEXT("\n\t{\n\t\t"));
		Emit(Header, *FString::Printf(TEXT("static const FName __PropertyName(TEXT(\"%s\"));"), *Property->GetName()));
		Emit(Header, TEXT("\n\t\t"));
		Emit(Header, *FString::Printf(TEXT("return *(GetClass()->FindPropertyByName(__PropertyName)->ContainerPtrToValuePtr<%s>(__Object));"),*TypeDeclaration));
		Emit(Header, TEXT("\n\t}\n\n"));
	}

	// FUNCTIONS:
	for (auto Func : FunctionsToGenerate)
	{
		Emit(Header, *FString::Printf(TEXT("\tvoid %s("), *FEmitHelper::GetCppName(Func)));
		bool bFirst = true;
		for (TFieldIterator<UProperty> It(Func); It; ++It)
		{
			UProperty* Property = *It;
			if (!Property || !Property->HasAnyPropertyFlags(CPF_Parm))
			{
				continue;
			}

			if (!bFirst)
			{
				Emit(Header, TEXT(", "));
			}
			else
			{
				bFirst = false;
			}

			if (Property->HasAnyPropertyFlags(CPF_OutParm))
			{
				Emit(Header, TEXT("/*out*/ "));
			}

			const FString TypeDeclaration = EmitterContext.ExportCppDeclaration(Property, EExportedDeclaration::Parameter, EPropertyExportCPPFlags::CPPF_CustomTypeName | EPropertyExportCPPFlags::CPPF_BlueprintCppBackend);
			Emit(Header, *TypeDeclaration);
		}
		Emit(Header, TEXT(")\n\t{\n"));
		Emit(Header, *FString::Printf(TEXT("\t\t%s::%s __Delegate;\n"), *DelegatesClassName, *GenerateDelegateTypeName(Func)));
		Emit(Header, *FString::Printf(TEXT("\t\tstatic const FName __FunctionName(TEXT(\"%s\"));\n"), *Func->GetName()));
		Emit(Header, TEXT("\t\t__Delegate.BindUFunction(__Object, __FunctionName);\n"));
		Emit(Header, TEXT("\t\tcheck(__Delegate.IsBound());\n"));
		Emit(Header, TEXT("\t\t__Delegate.Execute("));
		bFirst = true;
		for (TFieldIterator<UProperty> It(Func); It; ++It)
		{
			UProperty* Property = *It;
			if (!Property || !Property->HasAnyPropertyFlags(CPF_Parm))
			{
				continue;
			}

			if (!bFirst)
			{
				Emit(Header, TEXT(", "));
			}
			else
			{
				bFirst = false;
			}

			Emit(Header, *UnicodeToCPPIdentifier(Property->GetName(), Property->HasAnyPropertyFlags(CPF_Deprecated), TEXT("bpv__")));
		}
		Emit(Header, TEXT(");\n"));
		Emit(Header, TEXT("\t}\n"));
	}

	// close struct
	Emit(Header, TEXT("};\n"));
}

void FBlueprintCompilerCppBackendBase::EmitFileBeginning(const FString& CleanName, FGatherConvertedClassDependencies* Dependencies, bool bIncludeGeneratedH, bool bIncludeCodeHelpersInHeader)
{
	Emit(Header, TEXT("#pragma once\n\n"));

	auto EmitIncludeHeader = [&](FStringOutputDevice& Dst, const TCHAR* Message, bool bAddDotH)
	{
		Emit(Dst, *FString::Printf(TEXT("#include \"%s%s\"\n"), Message, bAddDotH ? TEXT(".h") : TEXT("")));
	};
	EmitIncludeHeader(Body, FApp::GetGameName(), true);
	EmitIncludeHeader(Body, *CleanName, true);
	if (bIncludeCodeHelpersInHeader)
	{
		EmitIncludeHeader(Header, TEXT("GeneratedCodeHelpers"), true);
	}
	else
	{
		EmitIncludeHeader(Body, TEXT("GeneratedCodeHelpers"), true);
	}

	if (Dependencies)
	{
		Emit(Header, *FBackendHelperUMG::AdditionalHeaderIncludeForWidget(Cast<UClass>(Dependencies->GetOriginalStruct())));

		TSet<FString> AlreadyIncluded;
		AlreadyIncluded.Add(CleanName);
		auto EmitInner = [&](FStringOutputDevice& Dst, const TSet<UField*>& Src, const TSet<UField*>& Declarations)
		{
			auto EngineSourceDir = FPaths::EngineSourceDir();
			auto GameSourceDir = FPaths::GameSourceDir();

			for (UField* Field : Src)
			{
				check(Field);
				const bool bWantedType = Field->IsA<UBlueprintGeneratedClass>() || Field->IsA<UUserDefinedEnum>() || Field->IsA<UUserDefinedStruct>();

				// Wanted no-native type, that will be converted
				if (bWantedType)
				{
					// @TODO: Need to query if this asset will actually be converted

					const FString Name = Field->GetName();
					bool bAlreadyIncluded = false;
					AlreadyIncluded.Add(Name, &bAlreadyIncluded);
					if (!bAlreadyIncluded)
					{
						const FString GeneratedFilename = IBlueprintCompilerCppBackendModule::GetBaseFilename(Field);
						EmitIncludeHeader(Dst, *GeneratedFilename, true);
					}
				}
				// headers for native items
				else
				{
					FString PackPath;
					if (FSourceCodeNavigation::FindClassHeaderPath(Field, PackPath))
					{
						if (!PackPath.RemoveFromStart(EngineSourceDir))
						{
							if (!PackPath.RemoveFromStart(GameSourceDir))
							{
								PackPath = FPaths::GetCleanFilename(PackPath);
							}
						}
						bool bAlreadyIncluded = false;
						AlreadyIncluded.Add(PackPath, &bAlreadyIncluded);
						if (!bAlreadyIncluded)
						{
							EmitIncludeHeader(Dst, *PackPath, false);
						}
					}
				}
			}

			Emit(Dst, TEXT("\n"));

			for (auto Type : Declarations)
			{
				if (auto ForwardDeclaredType = Cast<UClass>(Type))
				{
					Emit(Dst, *FString::Printf(TEXT("class %s;\n"), *FEmitHelper::GetCppName(ForwardDeclaredType)));
				}
			}

			Emit(Dst, TEXT("\n"));
		};

		EmitInner(Header, Dependencies->IncludeInHeader, Dependencies->DeclareInHeader);
		EmitInner(Body, Dependencies->IncludeInBody, TSet<UField*>());
	}

	if (bIncludeGeneratedH)
	{
		Emit(Header, *FString::Printf(TEXT("#include \"%s.generated.h\"\n\n"), *CleanName));
	}
}
