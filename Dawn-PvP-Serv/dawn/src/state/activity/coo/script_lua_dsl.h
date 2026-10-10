#pragma once
#include <string_view>

namespace dawn::state::activity::coo::script::lua {
inline constexpr std::string_view kBuilder = R"lua(
local native, mark = __native, __mark
__native, __mark = nil, nil
local function fields(value, allowed)
    assert(type(value) == 'table', 'builder options must be a table')
    for key in pairs(value) do
        assert(allowed[key], 'unknown builder field: ' .. tostring(key))
    end
end
local function list(value)
    assert(type(value) == 'table', 'expected a list')
    local count = 0
    for key in pairs(value) do
        assert(type(key) == 'number' and key % 1 == 0 and key >= 1,
               'list keys must be positive integers')
        count = count + 1
    end
    assert(count == #value, 'list must be dense')
    return mark(value, true)
end
function array(value) return list(value or {}) end
local conditionBindings = {}
function any_of(...) return mark({any_of=list{...}}) end
function all_of(...) return mark({all_of=list{...}}) end
function condition(id, expression)
    assert(type(id)=='string' and not native.bindings[id] and not conditionBindings[id], 'duplicate or invalid condition id: '..tostring(id))
    local nodes = array{}
    local function flatten(value, depth)
        assert(depth<=8 and #nodes<64, 'condition depth or node limit exceeded')
        local index = #nodes + 1
        local node = mark({})
        nodes[index] = node
        if type(value)=='string' then node.reference=value
        else
            fields(value, {any_of=true, all_of=true})
            assert((value.any_of~=nil) ~= (value.all_of~=nil), 'condition needs any_of or all_of')
            local children=array{}
            for _, child in ipairs(list(value.any_of or value.all_of)) do children[#children+1]=flatten(child,depth+1) end
            assert(#children>0, 'condition group cannot be empty')
            if value.any_of then node.any_of=children else node.all_of=children end
        end
        return index
    end
    flatten(expression,1)
    conditionBindings[id]={wait='observed'}
    return mark({id=id,nodes=nodes})
end
function command(capability, options)
    options = options or {}
    fields(options, {id=true, argument=true})
    local binding = native.bindings[capability] or conditionBindings[capability]
    assert(binding, 'unknown command capability: ' .. tostring(capability))
    local id = options.id or capability
    assert(type(id) == 'string', 'command id must be a string')
    if options.argument ~= nil then
        assert(not conditionBindings[capability], 'condition commands cannot override arguments')
        assert(not native.bindings[id] or id == capability,
               'duplicate command binding: ' .. id)
        -- A variant needs its own binding so it cannot alter another command.
        assert(id ~= capability, 'argument override requires a distinct command id')
        native.bindings[id] = mark({capability=capability,
            operation=binding.operation, asset=binding.asset,
            argument=options.argument, wait=binding.wait})
        return mark({id=id, binding=id})
    end
    return mark({id=id, binding=capability})
end
local function as_command(value)
    if type(value) == 'string' then return command(value) end
    fields(value, {id=true, binding=true})
    assert(native.bindings[value.binding] or conditionBindings[value.binding],
           'unknown command binding: ' .. tostring(value.binding))
    return value
end
function parallel(...)
    local result = array{...}
    for i, value in ipairs(result) do result[i] = as_command(value) end
    return result
end
function step(id, commands, options)
    options = options or {}
    fields(options, {after=true})
    if type(commands) == 'string' or (type(commands) == 'table' and commands.binding) then
        commands = parallel(commands)
    else
        commands = list(commands)
        for i, value in ipairs(commands) do commands[i] = as_command(value) end
    end
    return mark({id=id, after=list(options.after or {}), commands=commands})
end
function sequence(...)
    local steps = array{...}
    for i, value in ipairs(steps) do
        fields(value, {id=true, after=true, commands=true})
        assert(#value.after == 0, 'sequence steps cannot specify dependencies; use a graph step list')
        if i > 1 then value.after = array{steps[i-1].id} end
    end
    return steps
end
function graph(id, title, steps, options)
    options = options or {}
    fields(options, {domain=true, receipts=true})
    steps = list(steps)
    local receipts = mark({})
    for _, value in ipairs(steps) do
        fields(value, {id=true, after=true, commands=true})
        for _, cmd in ipairs(value.commands) do
            local binding = native.bindings[cmd.binding] or conditionBindings[cmd.binding]
            assert(binding, 'unknown command binding: ' .. tostring(cmd.binding))
            if binding.wait ~= 'requested' then receipts[cmd.id] = cmd.id end
        end
    end
    return {id=id, definition=mark({name=title, domain=options.domain or id,
        steps=steps, receipts=options.receipts or receipts})}
end
function presentation(options)
    fields(options, {dialogue=true, cue_sets=true, action_sets=true,
        binding_tables=true, markers=true})
    local result = native.presentation
    for key, value in pairs(options) do
        if key == 'dialogue' then
            fields(value, {bank=true, rows=true, objective_cues=true,
                dispatch_timeout_ms=true, spacing_ms=true})
            for name, setting in pairs(value) do result.dialogue[name] = setting end
        else
            result[key] = value
        end
    end
    return mark(result)
end
local built = false
function mission(options)
    assert(not built, 'a script must define exactly one mission')
    built = true
    fields(options, {id=true, graphs=true, roles=true, entry=true, modules=true,
        observations=true, presentation=true, phases=true, conditions=true, observation_start=true, parameters=true})
    local graphs = mark({})
    for _, value in ipairs(list(options.graphs)) do
        fields(value, {id=true, definition=true})
        assert(not graphs[value.id], 'duplicate graph: ' .. tostring(value.id))
        graphs[value.id] = value.definition
    end
    local conditions=mark({})
    for _, value in ipairs(list(options.conditions or {})) do
        fields(value, {id=true,nodes=true})
        assert(not conditions[value.id], 'duplicate condition: '..tostring(value.id))
        conditions[value.id]=value.nodes
    end
    local observations = list(options.observations or {})
    for i, value in ipairs(observations) do
        if type(value) == 'string' then
            observations[i] = mark({fact=value, receipt=value})
        else
            fields(value, {fact=true, receipt=true})
        end
    end
    return mark({format_version=2, mission=options.id, profile=native.profile,
        authority_schema=native.authority_schema, assets=native.assets,
        bindings=native.bindings, graphs=graphs, roles=options.roles or {},
        entry=options.entry, modules=list(options.modules or {}),
        phases=list(options.phases or {}), conditions=conditions, observation_start=options.observation_start,
        observations=observations, presentation=options.presentation or native.presentation,
        parameters=options.parameters})
end
)lua";
}
