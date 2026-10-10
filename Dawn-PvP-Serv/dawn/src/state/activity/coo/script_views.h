#pragma once
#include "mission_runtime.h"
#include "dialogue_service.h"
#include "presentation_cues.h"
#include "presentation_services.h"
#include "objective_service.h"

namespace dawn::state::activity::coo::script {
struct CommandBinding final { std::string_view id, capability; std::uint8_t step{}, command{}; };
struct GraphView final {
    std::string_view id, domain;
    Definition definition{};
    std::span<const CommandBinding> commands;
    [[nodiscard]] const CommandBinding* command(std::string_view name) const noexcept {
        for(const auto& item:commands) { if(item.id==name) { return &item; } }return nullptr;
    }
};
struct RoleView final { std::string_view id; const GraphView* graph{}; };
struct CueSet final { std::string_view id; std::span<const PresentationCue> cues; };
struct ActionSet final { std::string_view id; std::span<const PresentationAction> actions; };
struct PresentationTable final { std::string_view id; PresentationBindings bindings{}; };
inline constexpr Asset kConditionAsset{UINT32_MAX,UINT32_MAX,UINT16_MAX,UINT16_MAX};
struct ConditionNode final {
    enum class Kind { observation, any, all } kind{};
    CommandSpec native{};
    std::span<const std::uint8_t> children;
};
struct ConditionView final {
    std::string_view id;
    CommandSpec spec{};
    std::span<const ConditionNode> nodes;
    template<class Observe> [[nodiscard]] bool evaluate(Observe&& observe) const {
        const auto visit=[&](auto&& self,std::size_t index,unsigned depth)->bool {
            if(index>=nodes.size() || depth>8) { return false; }
            const auto& node=nodes[index];
            if(node.kind==ConditionNode::Kind::observation) { return is_observation(node.native.operation) && node.native.wait==Wait::observed && observe(node.native); }
            if(node.children.empty()) { return false; }
            for(const auto child:node.children) {
                const bool result=self(self,child,depth+1);
                if(node.kind==ConditionNode::Kind::any && result) { return true; }
                if(node.kind==ConditionNode::Kind::all && !result) { return false; }
            }
            return node.kind==ConditionNode::Kind::all;
        };
        return visit(visit,0,1);
    }
};
[[nodiscard]] inline bool valid_token(const Definition& definition,const Executor& executor,const Command& command) noexcept {
    const auto& token=command.token;
    if(command.schema!=definition.schema || !token.run || !token.incarnation || token.step>=definition.steps.size()
        || token.command>=definition.steps[token.step].commands.size() || executor.token(token.step,token.command)!=token) { return false; }
    const auto& expected=definition.steps[token.step].commands[token.command];
    return expected.operation==command.spec.operation && expected.asset==command.spec.asset
        && expected.argument==command.spec.argument && expected.wait==command.spec.wait;
}
struct PolicyValue final {
    std::string_view id;
    std::uint32_t value{};
    bool liveEditable{};
};
struct Views final {
    bool valid{};
    std::string_view missionId, profileId;
    std::span<const GraphView> graphs;
    std::span<const RoleView> roles;
    MissionDefinition mission{};
    DialogueDefinition dialogue{};
    std::span<const CueSet> cueSets;
    std::span<const ActionSet> actionSets;
    std::span<const PresentationTable> tables;
    std::span<const ObjectiveMarker> markers{};
    std::span<const GraphView* const> phases{};
    const CommandSpec* observationStart{};
    std::span<const ConditionView> conditions{};
    [[nodiscard]] const ConditionView* condition(const CommandSpec& spec) const noexcept {
        if(spec.operation!=Operation::observation || spec.wait!=Wait::observed || spec.asset!=kConditionAsset) { return nullptr; }
        for(const auto& item:conditions) { if(item.spec.argument==spec.argument) { return &item; } }return nullptr;
    }
    template<class Observe> [[nodiscard]] bool evaluate(const CommandSpec& spec,Observe&& observe) const {
        if(!valid) { return false; }
        if(const auto* value=condition(spec)) { return value->evaluate(observe); }
        return spec.asset!=kConditionAsset && is_observation(spec.operation) && spec.wait==Wait::observed && observe(spec);
    }
    std::span<const PolicyValue> parameters;
    [[nodiscard]] const PolicyValue* parameter(std::string_view id) const noexcept {
        for(const auto& item:parameters) { if(item.id==id) { return &item; } }return nullptr;
    }
    [[nodiscard]] const GraphView* graph(std::string_view id) const noexcept {
        for(const auto& item:graphs) { if(item.id==id) { return &item; } }return nullptr;
    }
    [[nodiscard]] const GraphView* role(std::string_view id) const noexcept {
        for(const auto& item:roles) { if(item.id==id) { return item.graph; } }return nullptr;
    }
    [[nodiscard]] std::span<const PresentationCue> cues(std::string_view id) const noexcept {
        for(const auto& item:cueSets) { if(item.id==id) { return item.cues; } }return {};
    }
    [[nodiscard]] std::span<const PresentationAction> actions(std::string_view id) const noexcept {
        for(const auto& item:actionSets) { if(item.id==id) { return item.actions; } }return {};
    }
    [[nodiscard]] const PresentationBindings* table(std::string_view id) const noexcept {
        for(const auto& item:tables) { if(item.id==id) { return &item.bindings; } }return nullptr;
    }
};
} // namespace dawn::state::activity::coo::script
