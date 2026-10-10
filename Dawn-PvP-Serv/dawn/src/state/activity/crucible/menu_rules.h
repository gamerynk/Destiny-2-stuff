#pragma once
#include "native_menu_options.h"
#include <span>

namespace dawn::state::activity::crucible::menu_rules {
struct Selection { std::int16_t index{-1}, choice{-1}; bool operator==(const Selection&) const=default; };
struct Rules { std::uint32_t scoreLimit{}; std::uint64_t durationMs{}; };
[[nodiscard]] inline bool resolve(std::span<const Selection> selections, Rules& output) noexcept {
    if (selections.size()>32) return false;
    Rules result{};
    for (std::size_t i=0;i<selections.size();++i) {
        const auto& selected=selections[i];
        if (selected.index<0 || selected.choice<0) return false;
        for (std::size_t j=0;j<i;++j) if (selections[j].index==selected.index) return false;
        for (const auto& definition:kDefinitions) if (definition.index==selected.index) {
            if (selected.choice>=definition.count) return false;
            const auto value=definition.values[static_cast<std::size_t>(selected.choice)];
            if (definition.seconds) {
                if (result.durationMs) return false;
                result.durationMs=std::uint64_t{value}*1000;
            } else {
                if (result.scoreLimit) return false;
                result.scoreLimit=value;
            }
        }
    }
    if (!result.scoreLimit || !result.durationMs) return false;
    output=result; return true;
}
}
