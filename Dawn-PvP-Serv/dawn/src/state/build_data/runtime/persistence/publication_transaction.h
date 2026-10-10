#pragma once

namespace dawn::state::build_data::runtime::persistence {

struct Context;

using Rollback = void() noexcept;

class Transaction final {
public:
    Transaction() noexcept;

    ~Transaction();

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    Transaction(Transaction&&) = delete;
    Transaction& operator=(Transaction&&) = delete;

    [[nodiscard]] bool active() const noexcept;

    // dropping clears the target; it never restores what was published before
    [[nodiscard]] bool finish(bool published, Rollback& rollback) noexcept;

private:
    Context* state_{};
    bool active_{};
};

} // namespace dawn::state::build_data::runtime::persistence
