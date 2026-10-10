#pragma once
#include "lifecycle_service.h"
#include "scan_playback.h"
#include "executor.h"
namespace dawn::state::activity::coo {
struct CampaignScan {
    bool armed{},started{},complete{};
    std::uint32_t controller{UINT32_MAX},serial{UINT32_MAX};
    bool observe(std::uint32_t generation,std::uint32_t handle,std::uint32_t salt,
                 ScanPlayback playback,bool participant) noexcept {
        if(!armed || complete || handle==UINT32_MAX || salt==UINT32_MAX || !playback.valid(generation)) return false;
        if(controller!=UINT32_MAX && (controller!=handle || serial!=salt)) return false;
        if(!started) {
            if(!playback.started(generation,participant)) return false;
            controller=handle;serial=salt;started=true;return true;
        }
        if(!playback.finished(generation,started)) return false;
        complete=true;return true;
    }
};
struct CampaignScanRequest {
    Generation owner{};Asset link{};std::uint32_t generation{};CampaignScan state{};
    bool enabled() const noexcept {return owner.valid() && generation && state.armed && !state.complete;}
};
}
