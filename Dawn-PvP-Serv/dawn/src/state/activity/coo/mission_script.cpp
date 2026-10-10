#include "mission_script.h"
#include "script_value.h"
#include "script_lua.h"
#include "script_json.h"
#include <algorithm>
#include <charconv>
#include <fstream>

namespace dawn::state::activity::coo::script {
namespace {
using value::Value;
constexpr std::string_view operations[]{"scene","population","objective","dialogue","device","cinematic","traversal","mechanic","observation","eventAfter","complete"};
constexpr std::string_view waits[]{"requested","nativeReady","completed","observed"};
const auto& object(const Value& v,std::size_t maximum) {
    if(v.kind!=Value::Kind::object || v.members.size()>maximum) { v.fail("expected bounded object"); }return v.members;
}
void name(std::string_view text,const Value& v) {
    if(text.empty() || std::any_of(text.begin(),text.end(),[](unsigned char c){return c<32 || c>126;})) { v.fail("name must be nonempty printable ASCII"); }
}
std::string_view name(const Value& v) { const auto& result=v.string();name(result,v);return result; }
std::uint32_t hex(const Value& v) {
    const auto& text=v.string();std::uint32_t result{};
    if(text.size()!=10 || text.substr(0,2)!="0x") { v.fail("expected 0x plus eight hexadecimal digits"); }
    const auto end=text.data()+text.size();const auto parsed=std::from_chars(text.data()+2,end,result,16);
    if(parsed.ec!=std::errc{} || parsed.ptr!=end) { v.fail("invalid hexadecimal identity"); }return result;
}
Asset asset(const Value& v) {
    v.fields({"registry","definition","type","slot"});
    return {hex(v.at("registry")),hex(v.at("definition")),static_cast<std::uint16_t>(v.at("type").integer(UINT16_MAX)),static_cast<std::uint16_t>(v.at("slot").integer(UINT16_MAX))};
}
const Value* optional(const Value& value,std::string_view key) {
    for(const auto& member:value.members) { if(member.first==key) { return &member.second; } }return nullptr;
}
void extended_fields(const Value& value,std::initializer_list<std::string_view> required,std::initializer_list<std::string_view> extra) {
    object(value,64);
    for(const auto& member:value.members) {
        bool found{};for(const auto key:required) { found|=member.first==key; }for(const auto key:extra) { found|=member.first==key; }
        if(!found) { member.second.fail("unknown field '"+member.first+"'"); }
    }
    for(const auto key:required) { static_cast<void>(value.at(key)); }
}
bool same(const CommandSpec& a,const CommandSpec& b) { return a.operation==b.operation && a.asset==b.asset && a.argument==b.argument && a.wait==b.wait; }
std::size_t enumeration(const Value& v,std::span<const std::string_view> values) {
    const auto& n=v.string();for(std::size_t i=0;i<values.size();++i) { if(n==values[i]) { return i; } }v.fail("unknown operation or wait");
}
template<class T> const T& lookup(std::span<const T> values,std::string_view id,const Value& source) {
    const T* result{};
    for(const auto& value:values) { if(value.id==id) { if(result) { source.fail("ambiguous native capability"); }result=&value; } }
    if(!result) { source.fail("unregistered native name '"+std::string(id)+"'"); }return *result;
}
bool objective(const Profile& profile,std::uint32_t value) {
    return std::find(profile.objectives.begin(),profile.objectives.end(),value)!=profile.objectives.end();
}
void validate_profile(const Profile& p,const Value& root) {
    name(p.id,root);name(p.schemaName,root);
    if(p.schema==Schema::unspecified || p.capabilities.empty() || p.capabilities.size()>4096 || p.modules.size()>8
        || p.facts.size()>32 || p.dialogue.rows.size()>64 || p.objectives.size()>256 || p.events.size()>1024 || p.tables.size()>32
        || p.parameters.size()>64) { root.fail("invalid native profile limits"); }
    for(std::size_t i=0;i<p.parameters.size();++i) {
        const auto& item=p.parameters[i];name(item.id,root);
        if(item.minimum>item.maximum || item.defaultValue<item.minimum || item.defaultValue>item.maximum) { root.fail("invalid native parameter bounds"); }
        for(std::size_t j=0;j<i;++j) { if(item.id==p.parameters[j].id) { root.fail("duplicate native parameter"); } }
    }
    for(std::size_t i=0;i<p.capabilities.size();++i) {
        const auto& cap=p.capabilities[i];name(cap.id,root);name(cap.domain,root);
        if(cap.spec.asset==kConditionAsset) { root.fail("native capability uses reserved condition identity"); }
        if(static_cast<unsigned>(cap.spec.operation)>=std::size(operations) || static_cast<unsigned>(cap.spec.wait)>=std::size(waits)
            || (is_observation(cap.spec.operation))!=(cap.spec.wait==Wait::observed)
            || (cap.argumentMaximum && (cap.spec.operation!=Operation::eventAfter || cap.spec.argument>cap.argumentMaximum))) { root.fail("invalid native operation contract"); }
        for(std::size_t j=0;j<i;++j) { if(cap.id==p.capabilities[j].id) { root.fail("duplicate native capability"); } }
    }
    for(std::size_t i=0;i<p.modules.size();++i) {
        name(p.modules[i].id,root);
        for(std::size_t j=0;j<i;++j) { if(p.modules[i].id==p.modules[j].id || p.modules[i].binding.id==p.modules[j].binding.id || p.modules[i].binding.asset==p.modules[j].binding.asset) { root.fail("duplicate native module"); } }
    }
    for(std::size_t i=0;i<p.facts.size();++i) {
        name(p.facts[i].id,root);if(p.facts[i].fact>=32) { root.fail("native fact outside supported range"); }
        for(std::size_t j=0;j<i;++j) { if(p.facts[i].id==p.facts[j].id || p.facts[i].fact==p.facts[j].fact) { root.fail("duplicate native fact"); } }
    }
    for(std::size_t i=0;i<p.events.size();++i) {
        name(p.events[i].set,root);name(p.events[i].id,root);
        for(std::size_t j=0;j<i;++j) { if(p.events[i].set==p.events[j].set && (p.events[i].id==p.events[j].id || p.events[i].event==p.events[j].event)) { root.fail("ambiguous native presentation event"); } }
    }
    if(p.markers.size()>256) { root.fail("too many native marker targets"); }
    for(std::size_t i=0;i<p.markers.size();++i) {
        name(p.markers[i].id,root);if(!p.markers[i].target.valid()) { root.fail("invalid native marker target"); }
        for(std::size_t j=0;j<i;++j) { if(p.markers[j].id==p.markers[i].id) { root.fail("ambiguous native marker target"); } }
    }
    for(std::size_t i=0;i<p.tables.size();++i) {
        name(p.tables[i].id,root);if(p.tables[i].bindings.schema!=p.schema) { root.fail("native presentation schema mismatch"); }
        for(std::size_t j=0;j<i;++j) { if(p.tables[i].id==p.tables[j].id) { root.fail("duplicate native presentation table"); } }
    }
}
bool equal_json(const Value& a,const Value& b,bool root=false) noexcept {
    if(a.kind!=b.kind) { return false; }
    switch(a.kind) {
    case Value::Kind::object: {
        std::size_t left{},right{};
        for(const auto& item:a.members) { if(!root || item.first!="parameters") { ++left; } }
        for(const auto& item:b.members) { if(!root || item.first!="parameters") { ++right; } }
        if(left!=right) { return false; }
        for(const auto& item:a.members) {
            if(root && item.first=="parameters") { continue; }
            const auto* other=b.find(item.first);
            if(!other || !equal_json(item.second,*other)) { return false; }
        }
        return true;
    }
    case Value::Kind::array:
        if(a.items.size()!=b.items.size()) { return false; }
        for(std::size_t i=0;i<a.items.size();++i) { if(!equal_json(a.items[i],b.items[i])) { return false; } }return true;
    case Value::Kind::string:return a.text==b.text;
    case Value::Kind::number:return a.number==b.number;
    case Value::Kind::boolean:return a.boolean==b.boolean;
    }
    return false;
}
}
struct MissionDocument::Storage final {
    Value root;
    struct AssetItem { std::string_view id;Asset value; };
    struct BindingItem { std::string_view id,capability,domain;CommandSpec spec; };
    struct Graph {
        std::vector<Step> steps;
        std::vector<std::vector<CommandSpec>> commands;
        std::vector<CommandBinding> bindings;
        std::vector<ReceiptBinding> receipts;
    };
    struct Table {
        std::vector<TraversalBinding> traversal;
        std::vector<ObjectiveBinding> objectives;
        std::vector<DialogueBinding> dialogue;
    };
    std::vector<AssetItem> assets;
    std::vector<BindingItem> bindings;
    std::vector<Graph> graphs;
    std::vector<GraphView> graphViews;
    std::vector<RoleView> roles;
    std::vector<const GraphView*> phases;
    CommandSpec observationStart{};
    struct ConditionStorage {
        std::vector<ConditionNode> nodes;
        std::vector<std::vector<std::uint8_t>> children;
    };
    std::vector<ConditionStorage> conditionStorage;
    std::vector<ConditionView> conditionViews;
    std::vector<ModuleBinding> modules;
    std::vector<ObservationBinding> observations;
    std::vector<DialogueRow> rows;
    std::vector<ObjectiveCueBinding> objectiveCues;
    std::vector<ObjectiveMarker> markers;
    std::vector<std::vector<PresentationCue>> cueLists;
    std::vector<std::vector<PresentationAction>> actionLists;
    std::vector<CueSet> cueSets;
    std::vector<ActionSet> actionSets;
    std::vector<Table> tables;
    std::vector<PresentationTable> tableViews;
    std::vector<std::string> parameterNames;
    std::vector<PolicyValue> parameterValues;
    Views views;
    std::uint64_t fingerprint{};
    Asset named_asset(const Value& value) const { return lookup(std::span<const AssetItem>(assets),name(value),value).value; }
    void load_graph(std::size_t ordinal,std::string_view id,const Value& input,const Profile& profile) {
        input.fields({"name","domain","steps","receipts"});name(id,input);const auto domain=name(input.at("domain"));
        auto& graph=graphs[ordinal];const auto& steps=input.at("steps").array(Executor::kMaxSteps);
        if(steps.empty()) { input.fail("graph requires at least one step"); }
        const auto count=steps.size();graph.steps.resize(count);graph.commands.resize(count);
        std::vector<std::uint32_t> dependencies(count);std::vector<std::size_t> order;
        std::vector<std::uint8_t> destination(count,UINT8_MAX);
        for(std::size_t i=0;i<count;++i) {
            const auto& step=steps[i];step.fields({"id","after","commands"});const auto idValue=name(step.at("id"));
            for(std::size_t j=0;j<i;++j) { if(idValue==steps[j].at("id").string()) { step.fail("duplicate step id"); } }
            for(const auto& after:step.at("after").array(Executor::kMaxSteps)) {
                const auto target=name(after);std::size_t j=0;for(;j<count && steps[j].at("id").string()!=target;++j) {}
                if(j==count || j==i || (dependencies[i]&(1U<<j))) { after.fail("missing, self, or duplicate dependency '"+std::string(target)+"'"); }dependencies[i]|=1U<<j;
            }
        }
        std::uint32_t completed{};
        while(order.size()!=count) {
            std::size_t i=0;for(;i<count;++i) { if(!(completed&(1U<<i)) && (dependencies[i]&completed)==dependencies[i]) { break; } }
            if(i==count) { input.fail("dependency cycle"); }
            destination[i]=static_cast<std::uint8_t>(order.size());order.push_back(i);completed|=1U<<i;
        }
        for(std::size_t n=0;n<count;++n) {
            const auto i=order[n];auto& output=graph.steps[n];output.name=steps[i].at("id").string();
            for(std::size_t j=0;j<count;++j) { if(dependencies[i]&(1U<<j)) { output.dependencies|=1U<<destination[j]; } }
            const auto& commands=steps[i].at("commands").array(Executor::kMaxCommands);
            if(commands.empty()) { steps[i].fail("step requires commands"); }
            for(std::size_t c=0;c<commands.size();++c) {
                const auto& cmd=commands[c];cmd.fields({"id","binding"});const auto commandId=name(cmd.at("id"));
                for(const auto& prior:graph.bindings) { if(prior.id==commandId) { cmd.fail("duplicate command id in graph"); } }
                const auto& binding=lookup(std::span<const BindingItem>(bindings),name(cmd.at("binding")),cmd);
                if(binding.domain!=domain && !(binding.domain=="*" && domain!="composition")) { cmd.fail("native operation belongs to a different service domain"); }
                graph.commands[n].push_back(binding.spec);
                graph.bindings.push_back({commandId,binding.capability,static_cast<std::uint8_t>(n),static_cast<std::uint8_t>(c)});
            }
            output.commands=graph.commands[n];
        }
        auto& view=graphViews[ordinal];view={id,domain,{name(input.at("name")),profile.schema,graph.steps},graph.bindings};
        for(const auto& [receipt,target]:object(input.at("receipts"),256)) {
            name(receipt,target);const auto* command=view.command(name(target));
            if(!command) { target.fail("receipt '"+receipt+"' refers to missing command '"+target.string()+"'"); }
            graph.receipts.push_back({receipt,command->step,command->command});
        }
        view.definition.receipts=graph.receipts;
        if(!Executor::valid(view.definition)) { input.fail("invalid executor graph or ambiguous receipt"); }
        for(const auto& binding:graph.bindings) {
            const auto& spec=graph.steps[binding.step].commands[binding.command];
            if(spec.wait==Wait::requested) { continue; }
            bool found{};for(const auto& receipt:graph.receipts) { found|=receipt.step==binding.step && receipt.command==binding.command; }
            if(!found) { input.fail("waiting command requires a named receipt"); }
        }
    }
    std::vector<PresentationAction> actions(const Value& list,const Profile& profile) {
        std::vector<PresentationAction> result;
        for(const auto& v:list.array(16)) {
            v.fields({"operation","value","delay_ms"});PresentationAction action{
                static_cast<Operation>(enumeration(v.at("operation"),operations)),v.at("value").integer(),v.at("delay_ms").integer(300000)};
            if(action.operation==Operation::objective) {
                if(!objective(profile,action.value) || action.delayMs) { v.fail("unknown objective or unsupported objective delay"); }
            } else if(action.operation==Operation::dialogue) {
                if(action.value>=rows.size() || rows[action.value].sceneOwned) { v.fail("unknown or scene-owned dialogue row"); }
            } else { v.fail("presentation action must be dialogue or objective"); }
            result.push_back(action);
        }
        return result;
    }
    void presentation(const Value& v,const Profile& profile) {
        bool hasMarkers{};for(const auto& item:v.members) { hasMarkers|=item.first=="markers"; }
        if(hasMarkers) {
            v.fields({"dialogue","cue_sets","action_sets","binding_tables","markers"});
            for(const auto& item:v.at("markers").array(256)) {
                item.fields({"objective","target"});const auto event=hex(item.at("objective"));
                if(!objective(profile,event)) { item.fail("marker objective is not registered"); }
                for(const auto& prior:markers) { if(prior.event==event) { item.fail("duplicate objective marker"); } }
                const auto& cap=lookup(profile.markers,name(item.at("target")),item);
                if(!cap.target.valid()) { item.fail("invalid native marker target"); }markers.push_back({event,cap.target});
            }
            views.markers=markers;
        } else { v.fields({"dialogue","cue_sets","action_sets","binding_tables"}); }
        const auto& dialogue=v.at("dialogue");dialogue.fields({"bank","rows","objective_cues","dispatch_timeout_ms","spacing_ms"});
        if(hex(dialogue.at("bank"))!=profile.dialogue.bank) { dialogue.fail("unregistered dialogue bank"); }
        const auto& rowList=dialogue.at("rows").array(64);rows.resize(profile.dialogue.rows.size());
        if(rowList.size()!=rows.size()) { dialogue.fail("dialogue rows do not match native bank"); }
        std::uint64_t seen{};
        for(const auto& row:rowList) {
            row.fields({"row","selector","duration_ms","native_delay_ms","scene_owned"});const auto i=row.at("row").integer(63);
            if(i>=rows.size() || (seen&(1ULL<<i))) { row.fail("unknown or duplicate dialogue row"); }seen|=1ULL<<i;
            rows[i]={hex(row.at("selector")),row.at("duration_ms").integer(300000),row.at("native_delay_ms").integer(),row.at("scene_owned").flag()};
            const auto& native=profile.dialogue.rows[i];
            if(rows[i].selector!=native.selector || rows[i].delayMs!=native.delayMs || rows[i].sceneOwned!=native.sceneOwned) { row.fail("native dialogue identity or ownership changed"); }
        }
        seen=0;
        for(const auto& cue:dialogue.at("objective_cues").array(64)) {
            cue.fields({"row","objective"});const auto row=cue.at("row").integer(63);const auto event=hex(cue.at("objective"));
            if(row>=rows.size() || rows[row].sceneOwned || (seen&(1ULL<<row)) || !objective(profile,event)) { cue.fail("unknown or duplicate objective cue"); }seen|=1ULL<<row;
            objectiveCues.push_back({static_cast<std::uint8_t>(row),event});
        }
        const auto timeout=dialogue.at("dispatch_timeout_ms").integer(300000);if(timeout<100) { dialogue.fail("dispatch timeout must be at least 100 ms"); }
        views.dialogue={profile.dialogue.bank,rows,objectiveCues,timeout,dialogue.at("spacing_ms").integer(60000)};
        const auto& sets=object(v.at("cue_sets"),32);cueLists.resize(sets.size());cueSets.resize(sets.size());
        // at most 32*64 cue actions plus 64 standalone lists; reserve so spans stay valid
        actionLists.reserve(32*64+64);
        for(std::size_t i=0;i<sets.size();++i) {
            const auto& [set,list]=sets[i];name(set,list);
            bool supported{};for(const auto& event:profile.events) { supported|=event.set==set; }
            if(!supported) { list.fail("unregistered presentation event set"); }
            for(const auto& cue:list.array(64)) {
                cue.fields({"event","cycles","actions"});const auto eventName=name(cue.at("event"));const EventCapability* event{};
                for(const auto& candidate:profile.events) { if(candidate.set==set && candidate.id==eventName) { event=&candidate; } }
                if(!event) { cue.fail("unregistered presentation event"); }
                std::uint8_t cycles{};
                for(const auto& cycle:cue.at("cycles").array(8)) {
                    const auto n=cycle.integer(8);if(!n || (cycles&(1U<<(n-1)))) { cycle.fail("invalid or duplicate cycle"); }cycles|=static_cast<std::uint8_t>(1U<<(n-1));
                }
                if((cycles & ~event->allowedCycles)!=0 || (event->allowedCycles!=0 && cycles==0)) { cue.fail("unsupported event cycle"); }
                actionLists.push_back(actions(cue.at("actions"),profile));cueLists[i].push_back({event->event,cycles,actionLists.back()});
            }
            cueSets[i]={set,cueLists[i]};
        }
        for(const auto& [set,list]:object(v.at("action_sets"),64)) { name(set,list);actionLists.push_back(actions(list,profile));actionSets.push_back({set,actionLists.back()}); }
        const auto& tableList=object(v.at("binding_tables"),32);tables.resize(tableList.size());tableViews.resize(tableList.size());
        for(std::size_t i=0;i<tableList.size();++i) {
            const auto& [id,input]=tableList[i];name(id,input);input.fields({"traversal","objectives","dialogue"});
            const auto& native=lookup(profile.tables,id,input).bindings;auto& table=tables[i];
            for(const auto& item:input.at("traversal").array(256)) {
                item.fields({"asset","stage"});TraversalBinding value{named_asset(item.at("asset")),static_cast<std::uint8_t>(item.at("stage").integer(UINT8_MAX))};
                bool found{};for(const auto& cap:native.traversal) { found|=cap.asset==value.asset && cap.stage==value.stage; }
                for(const auto& prior:table.traversal) { if(prior.asset==value.asset) { item.fail("duplicate traversal asset"); } }
                if(!found) { item.fail("unregistered traversal binding"); }table.traversal.push_back(value);
            }
            for(const auto& item:input.at("objectives").array(256)) {
                item.fields({"asset","event"});ObjectiveBinding value{named_asset(item.at("asset")),hex(item.at("event"))};
                bool found{};for(const auto& cap:native.objectives) { found|=cap.asset==value.asset && cap.event==value.event; }
                for(const auto& prior:table.objectives) { if(prior.asset==value.asset) { item.fail("duplicate objective asset"); } }
                if(!found) { item.fail("unregistered objective binding"); }table.objectives.push_back(value);
            }
            for(const auto& item:input.at("dialogue").array(64)) {
                item.fields({"asset","row","delay_ms"});DialogueBinding value{named_asset(item.at("asset")),static_cast<std::uint8_t>(item.at("row").integer(63)),item.at("delay_ms").integer(300000)};
                bool found{};for(const auto& cap:native.dialogue) { found|=cap.asset==value.asset && cap.row==value.row; }
                for(const auto& prior:table.dialogue) { if(prior.asset==value.asset) { item.fail("duplicate dialogue asset"); } }
                if(!found || value.row>=rows.size() || rows[value.row].sceneOwned) { item.fail("unregistered dialogue binding"); }table.dialogue.push_back(value);
            }
            tableViews[i]={id,{profile.schema,table.traversal,table.objectives,table.dialogue}};
        }
        views.cueSets=cueSets;views.actionSets=actionSets;views.tables=tableViews;
    }
    void load_conditions(const Value& source,const Profile& profile) {
        const auto& declarations=object(source,64);
        conditionStorage.resize(declarations.size());conditionViews.resize(declarations.size());
        for(const auto& [id,nodes]:declarations) {
            name(id,nodes);if(nodes.array(64).empty()) { nodes.fail("condition needs an expression"); }
            for(const auto& cap:profile.capabilities) { if(cap.id==id) { nodes.fail("condition shadows native capability"); } }
            for(const auto& binding:bindings) { if(binding.id==id) { nodes.fail("condition shadows command binding"); } }
        }
        for(std::size_t ordinal=0;ordinal<declarations.size();++ordinal) {
            auto& storage=conditionStorage[ordinal];storage.nodes.reserve(64);storage.children.reserve(64);
            std::vector<std::pair<std::size_t,std::size_t>> ancestors;
            const auto expand=[&](auto&& self,std::size_t declaration,std::size_t nodeIndex,unsigned depth)->std::uint8_t {
                const auto& inputs=declarations[declaration].second.array(64);
                if(nodeIndex>=inputs.size()) { source.fail("condition child reference is missing"); }
                const auto& input=inputs[nodeIndex];
                if(depth>8 || storage.nodes.size()>=64) { input.fail("condition depth or node limit exceeded"); }
                const auto key=std::pair{declaration,nodeIndex};
                if(std::find(ancestors.begin(),ancestors.end(),key)!=ancestors.end()) { input.fail("condition reference cycle"); }
                ancestors.push_back(key);
                object(input,1);if(input.members.size()!=1) { input.fail("condition node needs one expression"); }
                const auto& [kind,value]=input.members.front();
                if(kind=="reference") {
                    const auto id=name(value);
                    for(std::size_t i=0;i<declarations.size();++i) {
                        if(declarations[i].first==id) { const auto result=self(self,i,0,depth+1);ancestors.pop_back();return result; }
                    }
                    const auto& cap=lookup(profile.capabilities,id,value);
                    if(!is_observation(cap.spec.operation) || cap.spec.wait!=Wait::observed) { input.fail("condition requires readonly native observation capability"); }
                    const auto result=static_cast<std::uint8_t>(storage.nodes.size());
                    storage.nodes.push_back({ConditionNode::Kind::observation,cap.spec,{}});storage.children.emplace_back();
                    ancestors.pop_back();return result;
                }
                if(kind!="any_of" && kind!="all_of") { input.fail("unknown condition expression"); }
                const auto& children=value.array(64);if(children.empty()) { value.fail("condition group cannot be empty"); }
                const auto result=static_cast<std::uint8_t>(storage.nodes.size());
                storage.nodes.push_back({kind=="any_of"?ConditionNode::Kind::any:ConditionNode::Kind::all,{},{}});storage.children.emplace_back();
                for(const auto& child:children) {
                    const auto index=child.integer(64);if(!index) { child.fail("condition child indices start at one"); }
                    const auto resolved=self(self,declaration,index-1,depth+1);storage.children[result].push_back(resolved);
                }
                ancestors.pop_back();return result;
            };
            for(std::size_t node=1;node<declarations[ordinal].second.items.size();++node) {
                static_cast<void>(expand(expand,ordinal,node,1));storage.nodes.clear();storage.children.clear();
            }
            static_cast<void>(expand(expand,ordinal,0,1));
            for(std::size_t i=0;i<storage.nodes.size();++i) { storage.nodes[i].children=storage.children[i]; }
            const CommandSpec spec{Operation::observation,kConditionAsset,static_cast<std::uint32_t>(ordinal+1),Wait::observed};
            const auto id=std::string_view(declarations[ordinal].first);
            conditionViews[ordinal]={id,spec,storage.nodes};bindings.push_back({id,id,"*",spec});
        }
        views.conditions=conditionViews;
    }
    void load(const Profile& profile) {
        extended_fields(root,{"format_version","mission","profile","authority_schema","assets","bindings","graphs","roles","entry","modules","observations","presentation"},{"phases","conditions","observation_start","parameters"});
        if(root.at("format_version").integer()!=2) { root.fail("unsupported mission format; expected version 2"); }
        validate_profile(profile,root);views.missionId=name(root.at("mission"));views.profileId=name(root.at("profile"));
        if(views.profileId!=profile.id || name(root.at("authority_schema"))!=profile.schemaName) { root.fail("native profile or schema mismatch"); }
        const auto* supplied=root.find("parameters");
        if(supplied) {
            for(const auto& item:object(*supplied,64)) { static_cast<void>(lookup(profile.parameters,item.first,item.second)); }
        }
        parameterNames.reserve(profile.parameters.size());parameterValues.reserve(profile.parameters.size());
        for(const auto& cap:profile.parameters) {
            const auto* input=supplied?supplied->find(cap.id):nullptr;
            const auto value=input?input->integer(cap.maximum):cap.defaultValue;
            if(value<cap.minimum) { input->fail("parameter below native minimum"); }
            parameterNames.emplace_back(cap.id);
            parameterValues.push_back({parameterNames.back(),value,cap.liveEditable});
        }
        views.parameters=parameterValues;
        for(const auto& [id,value]:object(root.at("assets"),2048)) {
            name(id,value);const auto key=asset(value);bool allowed{};
            for(const auto& cap:profile.capabilities) { allowed|=cap.spec.asset==key; }
            if(!allowed) { value.fail("asset has no registered native capability"); }assets.push_back({id,key});
        }
        for(const auto& [id,value]:object(root.at("bindings"),4096)) {
            name(id,value);value.fields({"capability","operation","asset","argument","wait"});
            const auto key=name(value.at("capability"));const auto& cap=lookup(profile.capabilities,key,value);
            CommandSpec spec{static_cast<Operation>(enumeration(value.at("operation"),operations)),named_asset(value.at("asset")),value.at("argument").integer(),static_cast<Wait>(enumeration(value.at("wait"),waits))};
            if(!(same(spec,cap.spec) || (cap.argumentMaximum && spec.operation==Operation::eventAfter && spec.asset==cap.spec.asset && spec.wait==cap.spec.wait && spec.argument>0 && spec.argument<=cap.argumentMaximum))) { value.fail("operation does not match registered native capability"); }
            bindings.push_back({id,key,cap.domain,spec});
        }
        if(const auto* conditions=optional(root,"conditions")) { load_conditions(*conditions,profile); }
        if(const auto* start=optional(root,"observation_start")) {
            const auto& cap=lookup(profile.capabilities,name(*start),*start);
            if(!is_observation(cap.spec.operation) || cap.spec.wait!=Wait::observed) { start->fail("observation_start requires a readonly native observation capability"); }
            observationStart=cap.spec;views.observationStart=&observationStart;
        }
        const auto& graphList=object(root.at("graphs"),64);if(graphList.empty()) { root.fail("mission requires graphs"); }
        graphs.resize(graphList.size());graphViews.resize(graphList.size());
        for(std::size_t i=0;i<graphList.size();++i) { load_graph(i,graphList[i].first,graphList[i].second,profile); }
        views.graphs=graphViews;
        if(const auto* phaseList=optional(root,"phases")) {
            for(const auto& phase:phaseList->array(8)) {
                const auto* graph=views.graph(name(phase));
                if(!graph || graph->domain=="composition") { phase.fail("phase requires a non-composition graph"); }
                if(std::find(phases.begin(),phases.end(),graph)!=phases.end()) { phase.fail("duplicate phase graph"); }
                phases.push_back(graph);
            }
            views.phases=phases;
        }
        for(const auto& [role,value]:object(root.at("roles"),64)) {
            name(role,value);const auto* target=views.graph(name(value));if(!target) { value.fail("role refers to missing graph"); }roles.push_back({role,target});
        }
        views.roles=roles;const auto* entry=views.graph(name(root.at("entry")));if(!entry) { root.fail("entry graph is missing"); }
        for(const auto& module:root.at("modules").array(8)) { modules.push_back(lookup(profile.modules,name(module),module).binding); }
        for(const auto& observation:root.at("observations").array(32)) {
            observation.fields({"fact","receipt"});const auto fact=lookup(profile.facts,name(observation.at("fact")),observation).fact;
            const auto target=name(observation.at("receipt"));const ReceiptBinding* resolved{};
            for(const auto& receipt:entry->definition.receipts) { if(receipt.name==target) { resolved=&receipt; } }
            if(!resolved || entry->definition.steps[resolved->step].commands[resolved->command].operation!=Operation::observation
                || entry->definition.steps[resolved->step].commands[resolved->command].argument!=fact) { observation.fail("fact does not match named observation receipt"); }
            observations.push_back({fact,resolved->step,resolved->command});
        }
        views.mission={entry->definition,modules,observations};if(!MissionRuntime::valid(views.mission)) { root.fail("invalid mission modules or observation mapping"); }
        presentation(root.at("presentation"),profile);views.valid=true;
    }
};
bool authorized(const Views& views,const Profile& profile) noexcept {
    if(!views.valid || views.graphs.empty() || views.graphs.size()>64 || views.conditions.size()>64 || views.phases.size()>8) { return false; }
    const auto native=[&](const CommandSpec& spec,std::string_view domain,bool observationOnly) {
        if(spec.asset==kConditionAsset || (observationOnly && (!is_observation(spec.operation) || spec.wait!=Wait::observed))) { return false; }
        for(const auto& cap:profile.capabilities) {
            if(!domain.empty() && cap.domain!=domain && !(cap.domain=="*" && domain!="composition")) { continue; }
            if(same(spec,cap.spec) || (cap.argumentMaximum && spec.operation==Operation::eventAfter && cap.spec.operation==Operation::eventAfter
                && spec.asset==cap.spec.asset && spec.wait==cap.spec.wait && spec.argument>0 && spec.argument<=cap.argumentMaximum)) { return true; }
        }
        return false;
    };
    for(std::size_t i=0;i<views.conditions.size();++i) {
        const auto& condition=views.conditions[i];
        if(condition.id.empty() || condition.spec.operation!=Operation::observation || condition.spec.wait!=Wait::observed
            || condition.spec.asset!=kConditionAsset || !condition.spec.argument || condition.nodes.empty() || condition.nodes.size()>64) { return false; }
        for(std::size_t j=0;j<i;++j) { if(condition.id==views.conditions[j].id || condition.spec.argument==views.conditions[j].spec.argument) { return false; } }
        unsigned remaining{};
        const auto validNode=[&](auto&& self,std::size_t index,unsigned depth,std::uint64_t ancestors)->bool {
            if(!remaining) { return false; }--remaining;
            if(index>=condition.nodes.size() || depth>8 || (ancestors&(UINT64_C(1)<<index))) { return false; }
            const auto& node=condition.nodes[index];
            if(node.kind==ConditionNode::Kind::observation) { return node.children.empty() && native(node.native,{},true); }
            if((node.kind!=ConditionNode::Kind::any && node.kind!=ConditionNode::Kind::all) || node.children.empty() || node.children.size()>64) { return false; }
            for(const auto child:node.children) { if(!self(self,child,depth+1,ancestors|(UINT64_C(1)<<index))) { return false; } }
            return true;
        };
        for(std::size_t n=0;n<condition.nodes.size();++n) { remaining=64;if(!validNode(validNode,n,1,0)) { return false; } }
    }
    const auto definition=[&](const Definition& graph,std::string_view domain) {
        if(graph.schema!=profile.schema || !Executor::valid(graph)) { return false; }
        for(const auto& step:graph.steps) { for(const auto& spec:step.commands) {
            if(const auto* condition=views.condition(spec)) { if(domain=="composition" || condition->nodes.empty()) { return false; } }
            else if(!native(spec,domain,false)) { return false; }
        } }
        return true;
    };
    for(const auto& graph:views.graphs) { if(graph.domain.empty() || !definition(graph.definition,graph.domain)) { return false; } }
    if(!definition(views.mission.sequence,"composition") || !MissionRuntime::valid(views.mission)) { return false; }
    for(const auto& module:views.mission.modules) {
        bool found{};for(const auto& cap:profile.modules) { found|=module.id==cap.binding.id && module.asset==cap.binding.asset; }if(!found) { return false; }
    }
    for(std::size_t i=0;i<views.phases.size();++i) {
        const auto* phase=views.phases[i];bool found{};for(const auto& graph:views.graphs) { found|=phase==&graph; }
        if(!found || phase->domain=="composition") { return false; }
        for(std::size_t j=0;j<i;++j) { if(phase==views.phases[j]) { return false; } }
    }
    return !views.observationStart || native(*views.observationStart,{},true);
}
MissionDocument::MissionDocument():storage_(std::make_unique<Storage>()) {}
MissionDocument::~MissionDocument()=default;
const Views& MissionDocument::views() const noexcept { return storage_->views; }
std::uint64_t MissionDocument::fingerprint() const noexcept { return storage_->fingerprint; }
bool MissionDocument::same_structure(const MissionDocument& other) const noexcept {
    return equal_json(storage_->root,other.storage_->root,true);
}
std::unique_ptr<MissionDocument> MissionDocument::parse_lua(std::string_view text,const Profile& profile,std::string& error,std::string_view sourceName) noexcept {
    try {
        error.clear();auto document=std::unique_ptr<MissionDocument>(new MissionDocument);
        const Value source{};validate_profile(profile,source);
        document->storage_->root=lua::evaluate(text,profile,sourceName);document->storage_->load(profile);
        auto hash=UINT64_C(14695981039346656037);for(const unsigned char c:text) { hash^=c;hash*=UINT64_C(1099511628211); }document->storage_->fingerprint=hash;
        return document;
    } catch(const std::exception& exception) { error=std::string(sourceName)+": "+exception.what();return {}; }
}
std::unique_ptr<MissionDocument> MissionDocument::parse(std::string_view text,const Profile& profile,std::string& error) noexcept {
    try {
        error.clear();auto document=std::unique_ptr<MissionDocument>(new MissionDocument);
        document->storage_->root=json::Reader(text).parse();document->storage_->load(profile);
        auto hash=UINT64_C(14695981039346656037);for(const unsigned char c:text) { hash^=c;hash*=UINT64_C(1099511628211); }document->storage_->fingerprint=hash;
        return document;
    } catch(const std::exception& exception) { error=exception.what();return {}; }
}
std::unique_ptr<MissionDocument> MissionDocument::read(const std::filesystem::path& path,const Profile& profile,std::string& error) noexcept {
    try {
        auto extension=path.extension().wstring();
        for(auto& character:extension) { if(character>=L'A' && character<=L'Z') { character+=L'a'-L'A'; } }
        if(extension!=L".lua") { error=path.string()+": mission scripts must use the .lua extension";return {}; }
        std::ifstream stream(path,std::ios::binary);if(!stream) { error=path.string()+": cannot open mission script";return {}; }
        std::string text(1048577,'\0');stream.read(text.data(),static_cast<std::streamsize>(text.size()));text.resize(static_cast<std::size_t>(stream.gcount()));
        if(stream.bad()) { error=path.string()+": cannot read mission script";return {}; }
        return parse_lua(text,profile,error,path.string());
    } catch(const std::exception& exception) { error=exception.what();return {}; }
}
std::unique_ptr<MissionDocument> MissionDocument::read_native_policy(const std::filesystem::path& path,const Profile& profile,std::string& error) noexcept {
    try {
        auto extension=path.extension().wstring();
        for(auto& character:extension) { if(character>=L'A' && character<=L'Z') { character+=L'a'-L'A'; } }
        if(extension!=L".json") { error=path.string()+": native policies must use the .json extension";return {}; }
        std::ifstream stream(path,std::ios::binary);if(!stream) { error=path.string()+": cannot open mission script";return {}; }
        std::string text(1048577,'\0');stream.read(text.data(),static_cast<std::streamsize>(text.size()));text.resize(static_cast<std::size_t>(stream.gcount()));
        if(stream.bad()) { error=path.string()+": cannot read mission script";return {}; }
        auto result=parse(text,profile,error);
        if(!result) { error=path.string()+": "+error; }return result;
    } catch(const std::exception& exception) { error=exception.what();return {}; }
}
} // namespace dawn::state::activity::coo::script
