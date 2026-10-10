#pragma once

namespace dawn::core::path {

// stale siblings are named final.process.thread.sequence.tmp; a crashed writer leaves one
void remove_stale_siblings(const wchar_t* finalPath) noexcept;

} // namespace dawn::core::path
