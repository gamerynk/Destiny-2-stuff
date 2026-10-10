#pragma once
#include "controller.h"

namespace dawn::state::activity::titan {
struct RuntimeBinding final {
    std::uint64_t session{},incarnation{};bool creator{};
};
class Runtime final {
public:
    [[nodiscard]] Frame update(RuntimeBinding binding,std::uint32_t heldRegion,bool arrived,
        std::uint32_t prewarmRegion=UINT32_MAX) noexcept {
        if(!binding.creator || !binding.session || !binding.incarnation)return {};
        return controller_.update(binding.session,binding.incarnation,heldRegion,arrived,prewarmRegion);
    }
    [[nodiscard]] Frame snapshot(RuntimeBinding binding) const noexcept {
        const auto frame=controller_.snapshot();
        return binding.creator && binding.session==frame.session && binding.incarnation==frame.incarnation?frame:Frame{};
    }
    [[nodiscard]] bool observe_tasks(RuntimeBinding binding,std::uint32_t registry,std::uint16_t source,
        std::uint32_t revision,const coo::TaskCosts& report) noexcept {
        return binding.creator && controller_.observe_tasks(binding.session,binding.incarnation,registry,source,revision,report);
    }
private:Controller controller_{};
};
}
