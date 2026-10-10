#pragma once
#include "script_value.h"
#include <charconv>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dawn::state::activity::coo::script::json {
using Value = value::Value;
class Reader final {
public:
    explicit Reader(std::string_view text):text_(text) {}
    Value parse() {
        if(text_.size()>1048576) { error("script exceeds 1 MiB"); }
        if(text_.starts_with("\xEF\xBB\xBF")) { pos_=3; }
        auto root=value(0);space();if(pos_!=text_.size()) { error("trailing data"); }return root;
    }
private:
    [[noreturn]] void error(std::string_view message) const {
        Value v;v.offset=pos_;v.fail(message);
    }
    void space() { while(pos_<text_.size() && (text_[pos_]==' '||text_[pos_]=='\t'||text_[pos_]=='\r'||text_[pos_]=='\n')) { ++pos_; } }
    bool take(char c) { space();if(pos_<text_.size()&&text_[pos_]==c) { ++pos_;return true; }return false; }
    void expect(char c) { if(!take(c)) { error("unexpected character or end of input"); } }
    std::string string() {
        expect('"');std::string result;
        while(pos_<text_.size()) {
            auto c=static_cast<unsigned char>(text_[pos_++]);
            if(c=='"') { return result; }
            if(c<32 || c>126) { error("expected ASCII string"); }
            if(c=='\\') {
                if(pos_==text_.size()) { error("unfinished escape"); }
                const char escape=text_[pos_++];
                switch(escape) {
                case '"':case '\\':case '/':c=static_cast<unsigned char>(escape);break;
                case 'b':c='\b';break;case 'f':c='\f';break;case 'n':c='\n';break;case 'r':c='\r';break;case 't':c='\t';break;
                case 'u': {
                    if(pos_+4>text_.size()) { error("unfinished Unicode escape"); }
                    unsigned code{};const auto end=text_.data()+pos_+4;
                    const auto parsed=std::from_chars(text_.data()+pos_,end,code,16);
                    if(parsed.ec!=std::errc{} || parsed.ptr!=end || code>127) { error("only ASCII Unicode escapes are supported"); }
                    pos_+=4;c=static_cast<unsigned char>(code);break;
                }
                default:error("invalid escape");
                }
            }
            result.push_back(static_cast<char>(c));
            if(result.size()>256) { error("string exceeds 256 bytes"); }
        }
        error("unterminated string");
    }
    Value value(unsigned depth) {
        space();if(depth>16 || ++nodes_>30000) { error("JSON nesting or node limit exceeded"); }
        if(pos_==text_.size()) { error("missing value"); }
        Value v;v.offset=pos_;
        const char c=text_[pos_];
        if(c=='{') {
            ++pos_;v.kind=Value::Kind::object;
            if(take('}')) { return v; }
            do {
                auto key=string();for(const auto& old:v.members) { if(old.first==key) { error("duplicate key '"+key+"'"); } }
                expect(':');v.members.emplace_back(std::move(key),value(depth+1));
            } while(take(','));expect('}');
        } else if(c=='[') {
            ++pos_;v.kind=Value::Kind::array;
            if(take(']')) { return v; }
            do { v.items.push_back(value(depth+1)); } while(take(','));expect(']');
        } else if(c=='"') { v.kind=Value::Kind::string;v.text=string();
        } else if(text_.substr(pos_,4)=="true") { pos_+=4;v.kind=Value::Kind::boolean;v.boolean=true;
        } else if(text_.substr(pos_,5)=="false") { pos_+=5;v.kind=Value::Kind::boolean;
        } else if(c>='0'&&c<='9') {
            v.kind=Value::Kind::number;const auto start=pos_;
            while(pos_<text_.size()&&text_[pos_]>='0'&&text_[pos_]<='9') { ++pos_; }
            if(pos_-start>1&&c=='0') { error("leading zero"); }
            const auto converted=std::from_chars(text_.data()+start,text_.data()+pos_,v.number);
            if(converted.ec!=std::errc{}) { error("integer overflow"); }
        } else { error("unsupported JSON value"); }
        return v;
    }
    std::string_view text_;std::size_t pos_{},nodes_{};
};
} // namespace dawn::state::activity::coo::script::json
