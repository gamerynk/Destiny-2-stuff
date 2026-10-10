#pragma once

namespace dawn::client::input {

// the async key state reports a key held whichever app owns it, so movement polls are gated
// on this; the game ignores input when it loses focus
[[nodiscard]] bool game_focused() noexcept;

} // namespace dawn::client::input
