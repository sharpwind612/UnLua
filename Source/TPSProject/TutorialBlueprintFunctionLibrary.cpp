#include "TutorialBlueprintFunctionLibrary.h"
#include "Kismet/KismetSystemLibrary.h"

#include "UnLua.h"

static void PrintScreen(const FString& Msg)
{
    UKismetSystemLibrary::PrintString(nullptr, Msg, true, false, FLinearColor(0, 0.66, 1), 100);
}

const FString UNKNOW_KEY = "???";
const FString METATABLE_KEY = "__metatable";
const FString KEY_OF_TABLE = "!KEY!";

typedef enum RelationshipType
{
	TableValue = 1,
	NumberKeyTableValue = 2,
	KeyOfTable = 3,
	Metatable = 4,
	Upvalue = 5,
}RelationshipType;

struct RefInfo
{
	FString Key;

	bool HasNext;

	int* Parent;

	bool IsNumberKey;
};

static FString makeKey(RelationshipType type, const char* key, double d, const char* key2)
{
	switch (type)
	{
	case TableValue:
		return key == nullptr ? FString::FromInt((int)d) : key;
	case NumberKeyTableValue:
		return FString::Printf(TEXT("[%d]"), d);
	case KeyOfTable:
		return KEY_OF_TABLE;
	case Metatable:
		return METATABLE_KEY;
	case Upvalue:
		return FString::Printf(TEXT("%s:local %s"), *FString(key), *FString(key2));
	}
	return UNKNOW_KEY;
}

static TMap<void*, TArray<RefInfo>> GetRelationship()
{
	static const auto L = UnLua::GetState();
	static TMap<void*, TArray<RefInfo>> result;
	static int top = lua_gettop(L);
	static void* registryPointer = xlua_registry_pointer(L);
	static void* globalPointer = xlua_global_pointer(L);
	//const void* a = nullptr;
	//TArray<RefInfo>& infos = result.FindOrAdd(const_cast<void*>(a));
	//typedef void (*ObjectRelationshipReport) (const void* parent, const void* child, int type, const char* key, double d, const char* key2);
	xlua_report_object_relationship(L, [](const void* parent, const void* child, int type, const char* key, double d, const char* key2) {
		TArray<RefInfo>& infos = result.FindOrAdd(const_cast<void*>(child));

		FString keyOfRef = makeKey((RelationshipType)type, key, d, key2);

		bool hasNext = type != Upvalue;

		if (hasNext)
		{
			if (parent == registryPointer)
			{
				keyOfRef = "_R." + keyOfRef;
				hasNext = false;
			}
			else if (parent == globalPointer)
			{
				keyOfRef = "_G." + keyOfRef;
				hasNext = false;
			}
		}
		RefInfo info = { keyOfRef , hasNext , (int*)parent , type == NumberKeyTableValue };
		infos.Add(info);
		});
	lua_settop(L, top);
	return result;
}

void UTutorialBlueprintFunctionLibrary::CallLuaByGlobalTable()
{
    PrintScreen(TEXT("[C++]CallLuaByGlobalTable 开始"));

    UnLua::FLuaEnv Env;
    const auto bSuccess = Env.DoString("G_08_CppCallLua = require 'Tutorials.08_CppCallLua'");
    check(bSuccess);

    const auto RetValues = UnLua::CallTableFunc(Env.GetMainState(), "G_08_CppCallLua", "CallMe", 1.1f, 2.2f);
    check(RetValues.Num() == 1);

    const auto Msg = FString::Printf(TEXT("[C++]收到来自Lua的返回，结果=%f"), RetValues[0].Value<float>());
    PrintScreen(Msg);
    PrintScreen(TEXT("[C++]CallLuaByGlobalTable 结束"));
}

void UTutorialBlueprintFunctionLibrary::CallLuaByFLuaTable()
{
    PrintScreen(TEXT("[C++]CallLuaByFLuaTable 开始"));
    UnLua::FLuaEnv Env;

    const auto Require = UnLua::FLuaFunction(&Env, "_G", "require");
    const auto RetValues1 = Require.Call("Tutorials.08_CppCallLua");
    check(RetValues1.Num() == 2);

    const auto RetValue = RetValues1[0];
    const auto LuaTable = UnLua::FLuaTable(&Env, RetValue);
    const auto RetValues2 = LuaTable.Call("CallMe", 3.3f, 4.4f);
    check(RetValues2.Num() == 1);

    const auto Msg = FString::Printf(TEXT("[C++]收到来自Lua的返回，结果=%f"), RetValues2[0].Value<float>());
    PrintScreen(Msg);
    PrintScreen(TEXT("[C++]CallLuaByFLuaTable 结束"));
}

bool CustomLoader1(UnLua::FLuaEnv& Env, const FString& RelativePath, TArray<uint8>& Data, FString& FullPath)
{
    const auto SlashedRelativePath = RelativePath.Replace(TEXT("."), TEXT("/"));
    FullPath = FString::Printf(TEXT("%s%s.lua"), *GLuaSrcFullPath, *SlashedRelativePath);

    if (FFileHelper::LoadFileToArray(Data, *FullPath, FILEREAD_Silent))
        return true;

    FullPath.ReplaceInline(TEXT(".lua"), TEXT("/Index.lua"));
    if (FFileHelper::LoadFileToArray(Data, *FullPath, FILEREAD_Silent))
        return true;

    return false;
}

bool CustomLoader2(UnLua::FLuaEnv& Env, const FString& RelativePath, TArray<uint8>& Data, FString& FullPath)
{
    const auto SlashedRelativePath = RelativePath.Replace(TEXT("."), TEXT("/"));
    const auto L = Env.GetMainState();
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "path");
    const char* Path = lua_tostring(L, -1);
    lua_pop(L, 2);
    if (!Path)
        return false;

    TArray<FString> Parts;
    FString(Path).ParseIntoArray(Parts, TEXT(";"), false);
    for (auto& Part : Parts)
    {
        Part.ReplaceInline(TEXT("?"), *SlashedRelativePath);
        FPaths::CollapseRelativeDirectories(Part);
        
        if (FPaths::IsRelative(Part))
            FullPath = FPaths::ConvertRelativePathToFull(GLuaSrcFullPath, Part);
        else
            FullPath = Part;

        if (FFileHelper::LoadFileToArray(Data, *FullPath, FILEREAD_Silent))
            return true;
    }

    return false;
}

void UTutorialBlueprintFunctionLibrary::SetupCustomLoader(int Index)
{
    switch (Index)
    {
    case 0:
        FUnLuaDelegates::CustomLoadLuaFile.Unbind();
        break;
    case 1:
        FUnLuaDelegates::CustomLoadLuaFile.BindStatic(CustomLoader1);
        break;
    case 2:
        FUnLuaDelegates::CustomLoadLuaFile.BindStatic(CustomLoader2);
        break;
    }
}

//打印当前虚拟机中所有的LuaTable及其大小
void UTutorialBlueprintFunctionLibrary::PrintAllLuaTable()
{
    UE_LOG(LogTemp, Log, TEXT("UTutorialBlueprintFunctionLibrary::PrintAllLuaTable Start================="));
    const auto L = UnLua::GetState();
    //lua_pushstring(L, "ObjectMap");
    xlua_report_table_size(L, [](const void* p, int size){
            UE_LOG(LogTemp, Log, TEXT("LuaTable:%d,%d"), p, size);
        },false);
    TMap<void*, TArray<RefInfo>> infoMap = GetRelationship();
    UE_LOG(LogTemp, Log, TEXT("UTutorialBlueprintFunctionLibrary::PrintAllLuaTable End==================="));
}