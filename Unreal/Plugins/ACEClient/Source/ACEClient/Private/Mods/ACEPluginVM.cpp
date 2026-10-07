#include "ACEPluginVM.h"
#include "ACEVTRegex.h"
#include "ACEVTProfile.h"
#include <string_view>
extern "C" {
#include "Lua/lua.h"
#include "Lua/lauxlib.h"
#include "Lua/lualib.h"
}

namespace
{
    int PlainContains(lua_State* L)
    {
        size_t HayLength=0,NeedleLength=0;
        const char* Hay=luaL_checklstring(L,1,&HayLength);const char* Needle=luaL_checklstring(L,2,&NeedleLength);
        if(HayLength>4096||NeedleLength>256)return luaL_error(L,"contains text exceeds 4096/256 bytes");
        lua_pushboolean(L,std::string_view(Hay,HayLength).find(std::string_view(Needle,NeedleLength))!=std::string_view::npos);
        return 1;
    }
    FString LuaError(lua_State* L)
    { return lua_type(L,-1)==LUA_TSTRING ? FString(UTF8_TO_TCHAR(lua_tostring(L,-1))) : TEXT("Plugin raised a non-text error"); }
    int StoreTick(lua_State* L)
    { const int Ref=luaL_ref(L,LUA_REGISTRYINDEX); lua_pushinteger(L,Ref); return 1; }
    thread_local int Instructions=0;
    thread_local double RegexWork=0;
    void Budget(lua_State* L, lua_Debug*)
    {
        Instructions+=1000;
        if(Instructions>=250000)luaL_error(L,"plugin instruction budget exceeded");
    }
    // Cooperative checkpoint, not an extension of the hard sandbox quota.
    // Leave room to finish one bounded condition and return an intent.
    int WorkAvailable(lua_State* L)
    {
        lua_pushboolean(L,Instructions<200000&&RegexWork<.005);
        return 1;
    }
    thread_local int SpellIndexWork=0;
    double NumberField(lua_State* L,int Index,const char* Key,double Default=0)
    {lua_getfield(L,Index,Key);const double Value=lua_isnumber(L,-1)?lua_tonumber(L,-1):Default;lua_pop(L,1);return Value;}
    bool NotFalse(lua_State* L,int Index,const char* Key)
    {lua_getfield(L,Index,Key);const bool Value=lua_isnil(L,-1)||lua_toboolean(L,-1);lua_pop(L,1);return Value;}
    // Index snapshot tables without copying spell records or spending the Lua
    // instruction quota repeatedly walking the full book. Native work is also
    // bounded per decision; arbitrary plugins cannot bypass the work limit.
    int SpellIndex(lua_State* L)
    {
        luaL_checktype(L,1,LUA_TTABLE);luaL_checktype(L,2,LUA_TTABLE);luaL_checktype(L,5,LUA_TTABLE);
        const double Now=luaL_checknumber(L,3),Margin=luaL_checknumber(L,4);
        // Only an explicit server exemption bypasses component validation.
        const bool ComponentsRequired=lua_type(L,6)!=LUA_TBOOLEAN||lua_toboolean(L,6);
        const size_t Count=lua_rawlen(L,1),Inventory=lua_rawlen(L,2);
        if(Count>16384||Inventory>4096)return luaL_error(L,"spell index exceeds snapshot limit");
        lua_newtable(L);const int Supplies=lua_gettop(L);
        for(size_t I=1;I<=Inventory;++I)
        {
            if(++SpellIndexWork>131072)return luaL_error(L,"spell index work limit exceeded");
            lua_rawgeti(L,2,I);const int Item=lua_gettop(L);luaL_checktype(L,Item,LUA_TTABLE);
            lua_getfield(L,Item,"name");
            if(lua_type(L,-1)==LUA_TSTRING)
            {
                lua_pushvalue(L,-1);lua_rawget(L,Supplies);const double Old=lua_tonumber(L,-1);lua_pop(L,1);
                lua_pushnumber(L,Old+NumberField(L,Item,"count",1));lua_rawset(L,Supplies);
            }
            else lua_pop(L,1);
            lua_pop(L,1);
        }
        lua_newtable(L);const int ById=lua_gettop(L);
        lua_newtable(L);const int Supplied=lua_gettop(L);
        lua_newtable(L);const int Usable=lua_gettop(L);int UsableCount=0;
        for(size_t I=1;I<=Count;++I)
        {
            if(++SpellIndexWork>131072)return luaL_error(L,"spell index work limit exceeded");
            lua_rawgeti(L,1,I);const int Spell=lua_gettop(L);luaL_checktype(L,Spell,LUA_TTABLE);
            lua_getfield(L,Spell,"id");const int Id=lua_gettop(L);luaL_checktype(L,Id,LUA_TNUMBER);
            lua_pushvalue(L,Id);lua_pushvalue(L,Spell);lua_rawset(L,ById);
            bool HasSupply=!ComponentsRequired||NotFalse(L,Spell,"components_known");
            lua_getfield(L,Spell,"scarabs");
            if(ComponentsRequired&&HasSupply&&lua_istable(L,-1))
            {
                lua_pushnil(L);
                while(lua_next(L,-2))
                {
                    if(++SpellIndexWork>131072)return luaL_error(L,"spell index work limit exceeded");
                    const double Required=luaL_checknumber(L,-1);lua_pushvalue(L,-2);lua_rawget(L,Supplies);
                    HasSupply&=lua_tonumber(L,-1)>=Required;lua_pop(L,2);
                }
            }
            lua_pop(L,1);lua_pushvalue(L,Id);lua_pushboolean(L,HasSupply);lua_rawset(L,Supplied);
            lua_pushvalue(L,Id);lua_rawget(L,5);const double Unavailable=lua_tonumber(L,-1);lua_pop(L,1);
            if(HasSupply&&NotFalse(L,Spell,"known")&&Now>=Unavailable&&NumberField(L,Spell,"skill")>=NumberField(L,Spell,"power")+Margin)
            {lua_pushvalue(L,Spell);lua_rawseti(L,Usable,++UsableCount);}
            lua_pop(L,2);
        }
        return 3;
    }
    void PushJson(lua_State* L, const FJsonValue* V, int Depth);
    void PushObject(lua_State* L, const FJsonObject* O, int Depth)
    {
        if(Depth>64){luaL_error(L,"Plugin data nesting exceeds 64 levels");return;}
        lua_createtable(L,0,O?O->Values.Num():0);
        if(!O)return;
        for (const auto& Pair : O->Values)
        {
            PushJson(L, Pair.Value.Get(), Depth + 1);
            lua_setfield(L, -2, TCHAR_TO_UTF8(*Pair.Key));
        }
    }
    void PushJson(lua_State* L, const FJsonValue* V, int Depth)
    {
        if(Depth>64){luaL_error(L,"Plugin data nesting exceeds 64 levels");return;}
        if (!V) { lua_pushnil(L); return; }
        switch (V->Type)
        {
        case EJson::Boolean: lua_pushboolean(L, V->AsBool()); break;
        case EJson::Number: lua_pushnumber(L, V->AsNumber()); break;
        case EJson::String: lua_pushstring(L, TCHAR_TO_UTF8(*V->AsString())); break;
        case EJson::Object: PushObject(L, V->AsObject().Get(), Depth); break;
        case EJson::Array:
            lua_createtable(L,V->AsArray().Num(),0);
            for (int I = 0; I < V->AsArray().Num(); ++I) { PushJson(L, V->AsArray()[I].Get(), Depth + 1); lua_rawseti(L, -2, I + 1); }
            break;
        default: lua_pushnil(L);
        }
    }
    int PushArgument(lua_State* L);
    int RegexTest(lua_State* L)
    {
        size_t TextBytes=0,PatternBytes=0;const char* Text=luaL_checklstring(L,1,&TextBytes);const char* Pattern=luaL_checklstring(L,2,&PatternBytes);
        if(TextBytes>16384||PatternBytes>8192)return luaL_error(L,"Regex input exceeds limit");
        bool Failed=false,Found=false;
        {
            const double Start=FPlatformTime::Seconds();FString Error;
            Found=ACEVTRegex::Test(UTF8_TO_TCHAR(Pattern),UTF8_TO_TCHAR(Text),Error);
            RegexWork+=FPlatformTime::Seconds()-Start;Failed=!Error.IsEmpty()||RegexWork>.02;
        }
        if(Failed)return luaL_error(L,"Regex failed or exceeded per-tick work limit");
        lua_pushboolean(L,Found);return 1;
    }
    int RegexMatch(lua_State* L)
    {
        size_t TextBytes=0,PatternBytes=0;const char* Text=luaL_checklstring(L,1,&TextBytes);const char* Pattern=luaL_checklstring(L,2,&PatternBytes);
        if(TextBytes>16384||PatternBytes>8192)return luaL_error(L,"Regex input exceeds limit");
        // C++ temporaries are destroyed before raising the Lua longjmp exception.
        bool Failed=false;int Result=0;
        {
            const double Start=FPlatformTime::Seconds();TMap<FString,FString> Groups;FString Error;
            const bool Found=ACEVTRegex::Match(UTF8_TO_TCHAR(Pattern),UTF8_TO_TCHAR(Text),Groups,Error);
            RegexWork+=FPlatformTime::Seconds()-Start;Failed=!Error.IsEmpty()||RegexWork>.02;
            if(!Failed)
            {
                if(!Found)lua_pushnil(L);
                else
                {
                    auto Object=MakeShared<FJsonObject>();for(const auto& Pair:Groups)Object->SetStringField(Pair.Key,Pair.Value);
                    lua_pushcfunction(L,PushArgument);lua_pushlightuserdata(L,&Object.Get());
                    Failed=lua_pcall(L,1,1,0)!=LUA_OK;
                }
                Result=1;
            }
        }
        if(Failed)return luaL_error(L,"Regex failed or exceeded per-tick work limit");return Result;
    }
    int CompileCommand(lua_State* L)
    {
        size_t Bytes=0;const char* Text=luaL_checklstring(L,1,&Bytes);if(Bytes>4096)return luaL_error(L,"Command exceeds limit");
        bool Failed=false;
        {TArray<FString> Issues;auto Command=ACEVTProfile::CompileCommand(UTF8_TO_TCHAR(Text),Issues);Failed=Issues.Num()!=0;
         if(!Failed){lua_pushcfunction(L,PushArgument);lua_pushlightuserdata(L,Command.Get());Failed=lua_pcall(L,1,1,0)!=LUA_OK;}}
        if(Failed)return luaL_error(L,"Generated command has no supported native adapter");return 1;
    }
    int Open(lua_State* L)
    {
        luaL_requiref(L, "_G", luaopen_base, 1); lua_pop(L, 1);
        luaL_requiref(L, "table", luaopen_table, 1); lua_pop(L, 1);
        luaL_requiref(L, "string", luaopen_string, 1); lua_pop(L, 1);
        luaL_requiref(L, "math", luaopen_math, 1); lua_pop(L, 1);
        luaL_requiref(L, "utf8", luaopen_utf8, 1); lua_pop(L, 1);
        // pcall/xpcall/coroutines could catch a quota exception and continue forever.
        for (const char* Key : {"dofile", "loadfile", "load", "collectgarbage", "print", "pcall", "xpcall", "getmetatable", "setmetatable"})
        { lua_pushnil(L); lua_setglobal(L, Key); }
        // Pattern matching can consume unbounded time entirely inside C, outside
        // the instruction hook. Keep the string library's bounded basic operations.
        lua_getglobal(L, "string");
        for (const char* Key : {"find", "match", "gmatch", "gsub", "dump"})
        { lua_pushnil(L); lua_setfield(L, -2, Key); }
        lua_pushcfunction(L,PlainContains);lua_setfield(L,-2,"contains");
        lua_pop(L, 1);
        lua_pushcfunction(L,RegexMatch);lua_setglobal(L,"regexmatch");
        lua_pushcfunction(L,RegexTest);lua_setglobal(L,"regextest");
        lua_pushcfunction(L,CompileCommand);lua_setglobal(L,"vtcommand"); // Legacy plugin API alias.
        lua_pushcfunction(L,CompileCommand);lua_setglobal(L,"ucmcommand");
        lua_pushcfunction(L,SpellIndex);lua_setglobal(L,"spellindex");
        lua_pushcfunction(L,WorkAvailable);lua_setglobal(L,"workavailable");
        return 0;
    }
    int PushArgument(lua_State* L)
    { PushObject(L, static_cast<const FJsonObject*>(lua_touserdata(L, 1)), 0); return 1; }
}

FACEPluginVM::FACEPluginVM()
{
    State = lua_newstate([](void* Ud, void* Ptr, size_t Old, size_t New) -> void*
    {
        auto* Self = static_cast<FACEPluginVM*>(Ud);
        if (!Ptr) Old = 0; // Lua uses osize as a type tag for new allocations.
        if (!New) { FMemory::Free(Ptr); Self->Bytes -= Old; return nullptr; }
        if (New > 8 * 1024 * 1024 || Self->Bytes - Old > 8 * 1024 * 1024 - New) return nullptr;
        void* Result = FMemory::Realloc(Ptr, New);
        if (Result) {Self->Bytes = Self->Bytes - Old + New;Self->PeakBytes=FMath::Max(Self->PeakBytes,Self->Bytes);}
        return Result;
    }, this);
}
FACEPluginVM::~FACEPluginVM() { if (State) lua_close(State); }
bool FACEPluginVM::Load(const FString& Source, FString& Error)
{
    SpellIndexWork=0;Instructions=0;RegexWork=0;
    if (!State || Source.Len() > 256 * 1024) { Error = TEXT("Plugin exceeds source/memory limit"); return false; }
    lua_pushcfunction(State, Open);
    if (lua_pcall(State, 0, 0, 0) != LUA_OK) { Error = LuaError(State); return false; }
    lua_sethook(State, Budget, LUA_MASKCOUNT, 1000);
    FTCHARToUTF8 Utf8(*Source);
    if (luaL_loadbufferx(State, Utf8.Get(), Utf8.Length(), "plugin", "t") != LUA_OK || lua_pcall(State, 0, 1, 0) != LUA_OK)
    { Error = LuaError(State); lua_settop(State, 0); return false; }
    if (!lua_isfunction(State, -1)) { Error = TEXT("Plugin must return a tick(snapshot, profile) function"); return false; }
    lua_pushcfunction(State,StoreTick); lua_insert(State,-2);
    if(lua_pcall(State,1,1,0)!=LUA_OK){Error=LuaError(State);return false;}
    Function=int(lua_tointeger(State,-1));lua_pop(State,1);
    return true;
}
bool FACEPluginVM::Step(const TSharedPtr<FJsonObject>& Snapshot, const TSharedPtr<FJsonObject>& Profile,
    TSharedPtr<FJsonObject>& Intent, FString& Error)
{
    Intent.Reset();RegexWork=0;SpellIndexWork=0;Instructions=0;
    lua_settop(State, 0);
    lua_sethook(State, Budget, LUA_MASKCOUNT, 1000);
    lua_rawgeti(State, LUA_REGISTRYINDEX, Function);
    for (const auto* Object : {Snapshot.Get(), Profile.Get()})
    {
        lua_pushcfunction(State, PushArgument); lua_pushlightuserdata(State, const_cast<FJsonObject*>(Object));
        if (lua_pcall(State, 1, 1, 0) != LUA_OK) { Error = LuaError(State); return false; }
    }
    if (lua_pcall(State, 2, 1, 0) != LUA_OK) { Error = LuaError(State); return false; }
    if (lua_isnil(State, -1)) return true;
    if (!lua_istable(State, -1)) { Error = TEXT("Tick must return nil or an intent table"); return false; }
    Intent = MakeShared<FJsonObject>();
    lua_pushnil(State);
    int Count = 0;
    while (lua_next(State, -2))
    {
        if (++Count > 32 || lua_type(State, -2) != LUA_TSTRING) { Error = TEXT("Invalid intent fields"); return false; }
        size_t KeyLength = 0; const char* KeyText = lua_tolstring(State, -2, &KeyLength);
        if (KeyLength > 128) { Error = TEXT("Intent key exceeds limit"); return false; }
        const FString Key = UTF8_TO_TCHAR(KeyText);
        if (lua_type(State, -1) == LUA_TSTRING)
        {
            size_t Len = 0; const char* Value = lua_tolstring(State, -1, &Len);
            if (Len > 1024) { Error = TEXT("Intent text exceeds limit"); return false; }
            Intent->SetStringField(Key, UTF8_TO_TCHAR(Value));
        }
        else if (lua_type(State, -1) == LUA_TNUMBER)
        {
            const double Value = lua_tonumber(State, -1);
            if (!FMath::IsFinite(Value)) { Error = TEXT("Non-finite intent value"); return false; }
            Intent->SetNumberField(Key, Value);
        }
        else if (lua_isboolean(State, -1)) Intent->SetBoolField(Key, lua_toboolean(State, -1) != 0);
        else if(Key==TEXT("runtime_route")&&lua_istable(State,-1))
        {
            // Narrow overlay output: no arbitrary nested Lua objects or cycles.
            const size_t Num=lua_rawlen(State,-1);if(Num>2048){Error=TEXT("Overlay route exceeds 2048 points");return false;}
            TArray<TSharedPtr<FJsonValue>> Route;
            for(size_t I=1;I<=Num;++I)
            {
                lua_rawgeti(State,-1,I);if(!lua_istable(State,-1)){Error=TEXT("Invalid overlay waypoint");return false;}
                auto Point=MakeShared<FJsonObject>();
                for(const char* Field:{"cell","x","y","z"})
                {
                    lua_getfield(State,-1,Field);
                    if(lua_type(State,-1)!=LUA_TNUMBER||!FMath::IsFinite(lua_tonumber(State,-1))){Error=TEXT("Invalid overlay coordinate");return false;}
                    Point->SetNumberField(UTF8_TO_TCHAR(Field),lua_tonumber(State,-1));lua_pop(State,1);
                }
                lua_getfield(State,-1,"kind");size_t Len=0;const char* Kind=lua_tolstring(State,-1,&Len);
                if(Kind&&Len<=32)Point->SetStringField(TEXT("kind"),UTF8_TO_TCHAR(Kind));lua_pop(State,1);
                Route.Add(MakeShared<FJsonValueObject>(Point));lua_pop(State,1);
            }
            Intent->SetArrayField(Key,Route);
        }
        lua_pop(State, 1);
    }
    lua_settop(State, 0);
    return true;
}
