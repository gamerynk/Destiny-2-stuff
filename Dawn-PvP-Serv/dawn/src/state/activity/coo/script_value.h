#pragma once
#include <cstdint>
#include <cstddef>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dawn::state::activity::coo::script::value {
struct Value final {
    enum class Kind { object,array,string,number,boolean } kind{};
    std::vector<std::pair<std::string,Value>> members;
    std::vector<Value> items;
    std::string text;
    std::uint32_t number{};
    bool boolean{};
    std::size_t line{};
    std::size_t offset{};
    [[noreturn]] void fail(std::string_view reason) const {
        throw std::runtime_error((line ? "line " + std::to_string(line) : "byte " + std::to_string(offset)) + ": " + std::string(reason));
    }
    const Value& at(std::string_view key) const {
        if(kind!=Kind::object) { fail("expected object"); }
        for(const auto& item:members) { if(item.first==key) { return item.second; } }
        fail("missing field '" + std::string(key) + "'");
    }
    const Value* find(std::string_view key) const {
        if(kind!=Kind::object) { fail("expected object"); }
        for(const auto& item:members) { if(item.first==key) { return &item.second; } }return nullptr;
    }
    void fields(std::initializer_list<std::string_view> keys) const {
        if(kind!=Kind::object) { fail("expected object"); }
        for(const auto& item:members) {
            bool found=false;for(auto key:keys) { found|=key==item.first; }
            if(!found) { item.second.fail("unknown field '"+item.first+"'"); }
        }
        for(auto key:keys) { static_cast<void>(at(key)); }
    }
    const std::vector<Value>& array(std::size_t maximum) const {
        if(kind!=Kind::array || items.size()>maximum) { fail("expected bounded array"); }return items;
    }
    const std::string& string() const { if(kind!=Kind::string) { fail("expected string"); }return text; }
    std::uint32_t integer(std::uint32_t maximum=UINT32_MAX) const {
        if(kind!=Kind::number || number>maximum) { fail("integer outside allowed range"); }return number;
    }
    bool flag() const { if(kind!=Kind::boolean) { fail("expected boolean"); }return boolean; }
};
} // namespace dawn::state::activity::coo::script::value
