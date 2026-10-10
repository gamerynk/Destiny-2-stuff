#include "script_lua.h"
#include "script_lua_dsl.h"
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <algorithm>
#include <cstdlib>
#include <memory>

namespace dawn::state::activity::coo::script::lua {
namespace {
using value::Value;
constexpr std::size_t kMemoryLimit=8*1024*1024,kSourceLimit=1024*1024,kNodeLimit=30000;
constexpr unsigned kInstructionLimit=1000000,kHookInterval=1000;
constexpr std::string_view kOperations[]{"scene","population","objective","dialogue","device","cinematic","traversal","mechanic","observation","eventAfter","complete"};
constexpr std::string_view kWaits[]{"requested","nativeReady","completed","observed"};
char kMetadata;
struct Context final {
    std::size_t allocated{};
    unsigned instructions{};
    std::string_view source;
    std::string sourceName;
    Value native;
};
void* allocate(void* user,void* pointer,std::size_t oldSize,std::size_t newSize) noexcept {
    auto& context=*static_cast<Context*>(user);
    if(!pointer) { oldSize=0; }
    if(!newSize) { context.allocated-=oldSize;std::free(pointer);return nullptr; }
    const auto remaining=context.allocated-oldSize;
    if(newSize>kMemoryLimit || remaining>kMemoryLimit-newSize) { return nullptr; }
    auto* result=std::realloc(pointer,newSize);
    if(result) { context.allocated=remaining+newSize; }return result;
}
Context& context(lua_State* state) {
    void* user{};static_cast<void>(lua_getallocf(state,&user));return *static_cast<Context*>(user);
}
void instruction_hook(lua_State* state,lua_Debug*) {
    auto& value=context(state);value.instructions+=kHookInterval;
    if(value.instructions>=kInstructionLimit) { luaL_error(state,"mission authoring instruction limit exceeded"); }
}
Value object() { Value value;value.kind=Value::Kind::object;return value; }
Value array() { Value value;value.kind=Value::Kind::array;return value; }
Value string(std::string_view text) { Value value;value.kind=Value::Kind::string;value.text=text;return value; }
Value integer(std::uint32_t number) { Value value;value.kind=Value::Kind::number;value.number=number;return value; }
Value flag(bool boolean) { Value value;value.kind=Value::Kind::boolean;value.boolean=boolean;return value; }
void field(Value& target,std::string_view key,Value value) { target.members.emplace_back(std::string(key),std::move(value)); }
Value hex(std::uint32_t number) {
    std::string text="0x00000000";constexpr char digits[]="0123456789ABCDEF";
    for(unsigned i=0;i<8;++i) { text[9-i]=digits[number&15U];number>>=4; }return string(text);
}
Value asset(const Asset& native) {
    auto value=object();field(value,"registry",hex(native.registry));field(value,"definition",hex(native.definition));
    field(value,"type",integer(native.type));field(value,"slot",integer(native.slot));return value;
}
Value native_document(const Profile& profile) {
    if(profile.capabilities.empty() || profile.capabilities.size()>4096 || profile.dialogue.rows.size()>64
        || profile.dialogue.objectiveCues.size()>64 || profile.id.size()>256 || profile.schemaName.size()>256) {
        throw std::runtime_error("invalid native Lua profile limits");
    }
    auto root=object(),assets=object(),bindings=object();
    for(const auto& cap:profile.capabilities) {
        const auto op=static_cast<std::size_t>(cap.spec.operation),wait=static_cast<std::size_t>(cap.spec.wait);
        if(op>=std::size(kOperations) || wait>=std::size(kWaits) || cap.id.empty() || cap.id.size()>256) {
            throw std::runtime_error("invalid native Lua command capability");
        }
        for(const auto& prior:bindings.members) { if(prior.first==cap.id) { throw std::runtime_error("duplicate native Lua command capability"); } }
        field(assets,cap.id,asset(cap.spec.asset));auto binding=object();
        field(binding,"capability",string(cap.id));field(binding,"operation",string(kOperations[op]));
        field(binding,"asset",string(cap.id));field(binding,"argument",integer(cap.spec.argument));
        field(binding,"wait",string(kWaits[wait]));field(bindings,cap.id,std::move(binding));
    }
    auto presentation=object(),dialogue=object(),rows=array(),cues=array();
    for(std::size_t i=0;i<profile.dialogue.rows.size();++i) {
        const auto& native=profile.dialogue.rows[i];auto row=object();field(row,"row",integer(static_cast<std::uint32_t>(i)));
        field(row,"selector",hex(native.selector));field(row,"duration_ms",integer(native.durationMs));
        field(row,"native_delay_ms",integer(native.delayMs));field(row,"scene_owned",flag(native.sceneOwned));rows.items.push_back(std::move(row));
    }
    for(const auto& native:profile.dialogue.objectiveCues) {
        auto cue=object();field(cue,"row",integer(native.row));field(cue,"objective",hex(native.objective));cues.items.push_back(std::move(cue));
    }
    field(dialogue,"bank",hex(profile.dialogue.bank));field(dialogue,"rows",std::move(rows));field(dialogue,"objective_cues",std::move(cues));
    field(dialogue,"dispatch_timeout_ms",integer(profile.dialogue.dispatchTimeoutMs));field(dialogue,"spacing_ms",integer(profile.dialogue.spacingMs));
    field(presentation,"dialogue",std::move(dialogue));field(presentation,"cue_sets",object());field(presentation,"action_sets",object());
    field(presentation,"binding_tables",object());field(presentation,"markers",array());
    field(root,"profile",string(profile.id));field(root,"authority_schema",string(profile.schemaName));
    field(root,"assets",std::move(assets));field(root,"bindings",std::move(bindings));field(root,"presentation",std::move(presentation));return root;
}
void metadata(lua_State* state,int index,bool isArray,std::size_t line) {
    index=lua_absindex(state,index);lua_rawgetp(state,LUA_REGISTRYINDEX,&kMetadata);
    lua_pushvalue(state,index);lua_createtable(state,2,0);
    lua_pushinteger(state,static_cast<lua_Integer>(line));lua_rawseti(state,-2,1);
    lua_pushboolean(state,isArray);lua_rawseti(state,-2,2);lua_rawset(state,-3);lua_pop(state,1);
}
int mark(lua_State* state) {
    luaL_checktype(state,1,LUA_TTABLE);std::size_t line{};lua_Debug frame{};
    for(int depth=1;lua_getstack(state,depth,&frame);++depth) {
        if(lua_getinfo(state,"Sl",&frame) && frame.source && frame.source[0]=='@' && frame.currentline>0) {
            line=static_cast<std::size_t>(frame.currentline);break;
        }
    }
    metadata(state,1,lua_toboolean(state,2)!=0,line);lua_settop(state,1);return 1;
}
void push(lua_State* state,const Value& value) {
    if(!lua_checkstack(state,8)) { luaL_error(state,"native profile exceeds Lua stack limit"); }
    switch(value.kind) {
    case Value::Kind::string:lua_pushlstring(state,value.text.data(),value.text.size());break;
    case Value::Kind::number:lua_pushinteger(state,value.number);break;
    case Value::Kind::boolean:lua_pushboolean(state,value.boolean);break;
    case Value::Kind::array:
        lua_createtable(state,static_cast<int>(value.items.size()),0);metadata(state,-1,true,0);
        for(std::size_t i=0;i<value.items.size();++i) { push(state,value.items[i]);lua_rawseti(state,-2,static_cast<lua_Integer>(i+1)); }break;
    case Value::Kind::object:
        lua_createtable(state,0,static_cast<int>(value.members.size()));metadata(state,-1,false,0);
        for(const auto& member:value.members) { push(state,member.second);lua_setfield(state,-2,member.first.c_str()); }break;
    }
}
int execute(lua_State* state) {
    auto& value=context(state);
    lua_sethook(state,instruction_hook,LUA_MASKCOUNT,static_cast<int>(kHookInterval));
    luaopen_base(state);lua_pop(state,1);
    lua_newtable(state);
    constexpr const char* allowed[]{"assert","error","ipairs","pairs","next","select","tonumber","tostring","type"};
    for(const auto* name:allowed) { lua_getglobal(state,name);lua_setfield(state,-2,name); }
    lua_rawseti(state,LUA_REGISTRYINDEX,LUA_RIDX_GLOBALS);
    lua_newtable(state);lua_rawsetp(state,LUA_REGISTRYINDEX,&kMetadata);
    push(state,value.native);lua_setglobal(state,"__native");lua_pushcfunction(state,mark);lua_setglobal(state,"__mark");
    if(luaL_loadbufferx(state,kBuilder.data(),kBuilder.size(),"=mission DSL","t")!=LUA_OK) { return lua_error(state); }
    lua_call(state,0,0);
    if(luaL_loadbufferx(state,value.source.data(),value.source.size(),value.sourceName.c_str(),"t")!=LUA_OK) { return lua_error(state); }
    lua_call(state,0,1);return 1;
}
int traceback(lua_State* state) {
    const auto* message=lua_type(state,1)==LUA_TSTRING?lua_tostring(state,1):"Lua mission evaluation failed";
    luaL_traceback(state,state,message,1);return 1;
}
class Converter final {
public:
    explicit Converter(lua_State* state):state_(state) {}
    Value read(int index=1,unsigned depth=0,std::size_t inheritedLine=1,std::string path="mission") {
        Value value;value.line=inheritedLine;
        if(depth>16 || ++nodes_>kNodeLimit) { value.fail(path+": Lua nesting or node limit exceeded"); }
        if(!lua_checkstack(state_,8)) { value.fail(path+": Lua conversion stack limit exceeded"); }
        index=lua_absindex(state_,index);
        switch(lua_type(state_,index)) {
        case LUA_TSTRING:value.kind=Value::Kind::string;value.text=read_string(index,value,path);return value;
        case LUA_TBOOLEAN:value.kind=Value::Kind::boolean;value.boolean=lua_toboolean(state_,index)!=0;return value;
        case LUA_TNUMBER: {
            if(!lua_isinteger(state_,index)) { value.fail(path+": expected uint32 integer, not a floating-point value"); }
            const auto number=lua_tointeger(state_,index);
            if(number<0 || static_cast<lua_Unsigned>(number)>UINT32_MAX) { value.fail(path+": integer outside uint32 range"); }
            value.kind=Value::Kind::number;value.number=static_cast<std::uint32_t>(number);return value;
        }
        case LUA_TTABLE:break;
        default:value.fail(path+": unsupported Lua value (expected a definition table, string, integer, or boolean)");
        }
        if(lua_getmetatable(state_,index)) { lua_pop(state_,1);value.fail(path+": metatables are not supported"); }
        const auto* identity=lua_topointer(state_,index);
        if(std::find(ancestors_.begin(),ancestors_.end(),identity)!=ancestors_.end()) { value.fail(path+": cyclic Lua table"); }
        ancestors_.push_back(identity);bool markedArray{};
        lua_rawgetp(state_,LUA_REGISTRYINDEX,&kMetadata);lua_pushvalue(state_,index);lua_rawget(state_,-2);
        if(lua_istable(state_,-1)) {
            lua_rawgeti(state_,-1,1);const auto line=lua_tointeger(state_,-1);lua_pop(state_,1);
            if(line>0) { value.line=static_cast<std::size_t>(line); }
            lua_rawgeti(state_,-1,2);markedArray=lua_toboolean(state_,-1)!=0;lua_pop(state_,1);
        }
        lua_pop(state_,2);
        bool numeric{},textual{};std::size_t count{},maximum{};
        lua_pushnil(state_);
        while(lua_next(state_,index)!=0) {
            if(++count>kNodeLimit) { value.fail(path+": table node limit exceeded"); }
            if(lua_type(state_,-2)==LUA_TSTRING) { textual=true; }
            else if(lua_isinteger(state_,-2)) {
                const auto key=lua_tointeger(state_,-2);
                if(key<1 || static_cast<lua_Unsigned>(key)>kNodeLimit) { value.fail(path+": invalid array index"); }
                numeric=true;maximum=std::max(maximum,static_cast<std::size_t>(key));
            } else { value.fail(path+": table keys must be strings or positive integers"); }
            lua_pop(state_,1);
        }
        if(count>kNodeLimit-slots_) { value.fail(path+": Lua aggregate table limit exceeded"); }
        slots_+=count;
        if((numeric&&textual) || (markedArray&&textual)) { value.fail(path+": mixed array/object table"); }
        if(numeric || markedArray) {
            if(maximum!=count) { value.fail(path+": sparse array is not supported"); }
            value.kind=Value::Kind::array;value.items.reserve(count);
            for(std::size_t i=1;i<=count;++i) {
                lua_rawgeti(state_,index,static_cast<lua_Integer>(i));value.items.push_back(read(-1,depth+1,value.line,path+"["+std::to_string(i)+"]"));lua_pop(state_,1);
            }
        } else {
            value.kind=Value::Kind::object;value.members.reserve(count);lua_pushnil(state_);
            while(lua_next(state_,index)!=0) {
                auto key=read_string(-2,value,path);auto child=read(-1,depth+1,value.line,path+"."+key);
                value.members.emplace_back(std::move(key),std::move(child));lua_pop(state_,1);
            }
            std::sort(value.members.begin(),value.members.end(),[](const auto& a,const auto& b){return a.first<b.first;});
        }
        ancestors_.pop_back();return value;
    }
private:
    std::string read_string(int index,const Value& source,const std::string& path) {
        std::size_t size{};const auto* text=lua_tolstring(state_,index,&size);
        if(size>256) { source.fail(path+": string exceeds 256 bytes"); }
        for(std::size_t i=0;i<size;++i) { if(static_cast<unsigned char>(text[i])>127) { source.fail(path+": only ASCII strings are supported"); } }
        return std::string(text,size);
    }
    lua_State* state_;
    std::size_t nodes_{},slots_{};
    std::vector<const void*> ancestors_;
};
}
Value evaluate(std::string_view source,const Profile& profile,std::string_view sourceName) {
    if(source.size()>kSourceLimit) { throw std::runtime_error("Lua script exceeds 1 MiB"); }
    if(source.starts_with("\xEF\xBB\xBF")) { source.remove_prefix(3); }
    Context value;value.source=source;value.sourceName="@"+std::string(sourceName);value.native=native_document(profile);
    std::unique_ptr<lua_State,decltype(&lua_close)> state(lua_newstate(allocate,&value),lua_close);
    if(!state) { throw std::runtime_error("cannot allocate bounded Lua state"); }
    lua_pushcfunction(state.get(),traceback);lua_pushcfunction(state.get(),execute);
    if(lua_pcall(state.get(),0,1,1)!=LUA_OK) {
        const auto* message=lua_tostring(state.get(),-1);
        throw std::runtime_error(message?message:"Lua mission evaluation failed");
    }
    lua_remove(state.get(),1);
    return Converter(state.get()).read();
}
}
