#pragma once
#include "match_host.h"

namespace dawn::state::activity::crucible {
// statdefs 80BA0015: indices 6..13 place, 14..21 tied place. The type16 player field is a
// headline selector, independent of combat faction
inline std::int8_t native_score_headline(const host::View& view, std::size_t player) noexcept {
    if (view.mode != match_rules::Mode::rumble || view.phase != host::Phase::playing
        || view.count > view.rows.size() || player >= view.count) return -1;
    std::size_t ahead=0, equal=0;
    for (std::size_t i=0; i<view.count; ++i) {
        if (!view.rows[i].player) return -1;
        for (std::size_t j=0; j<i; ++j)
            if (view.rows[j].player==view.rows[i].player) return -1;
        ahead += view.rows[i].score > view.rows[player].score;
        equal += view.rows[i].score == view.rows[player].score;
    }
    return ahead<8 ? static_cast<std::int8_t>(6+ahead+(equal>1?8:0)) : -1;
}
}
