#pragma once

#include "combatant_state.h"
#include "executor.h"
#include "native_atom_authority.h"

namespace dawn::state::activity::coo {
struct ActorPublication final {
    native_atom::Program program{};
    std::array<native_atom::Ref,8> cargo{};
    std::uint32_t deliveryRevision{}, retirementGeneration{};
    std::uint8_t cargoCount{};
    [[nodiscard]] std::size_t bits() const noexcept {
        if(retirementGeneration) { return 77; }
        if(program.count==0) { return 0; }
        std::size_t result=(program.spawn?native_atom::kSpawnRootBits:native_atom::kControlRootBits)
            +native_atom::kProgramBlockBits;
        for(std::uint8_t index=0;index<program.count;++index) {
            result+=1U+native_atom::lane_bits(program.atoms[index]);
        }
        return result+(cargoCount?35U+55U*cargoCount:0U);
    }
    template<class Writer> [[nodiscard]] bool write(Writer& writer) const noexcept {
        if(retirementGeneration) { return native_atom::write_retirement(writer,retirementGeneration); }
        return native_atom::write_program(writer,program,
            std::span(cargo).first(cargoCount),deliveryRevision);
    }
};

class ActorProgramService final {
public:
    [[nodiscard]] const ActorPublication& publication() const noexcept { return publication_; }
    [[nodiscard]] const CombatantState& state() const noexcept { return state_; }
    [[nodiscard]] bool program(Token owner,native_atom::Program program) noexcept {
        if(owner.run==0 || owner.incarnation==0 || program.count==0
            || program.count>native_atom::kAtomCapacity || program.generation==0
            || program.generation>=native_atom::kMaximumCounter || program.revision==0
            || program.revision>native_atom::kMaximumCounter) { return false; }
        if(publication_.program.count) {
            if(!same_run(owner) || publication_.retirementGeneration
                || program.generation!=publication_.program.generation
                || program.revision<=publication_.program.revision) { return false; }
            program.spawn=publication_.program.spawn;
            program.binding=publication_.program.binding;
        } else if(!program.spawn) { return false; }
        for(std::uint8_t i=0;i<program.count;++i) {
            if(!native_atom::valid(program.atoms[i])) { return false; }
        }
        publication_.program=program; owner_=owner; finalAtomDetached_=false; return true;
    }
    [[nodiscard]] bool deliver(Token owner,std::span<const native_atom::Ref> cargo,
                                std::uint32_t revision) noexcept {
        if(!same_run(owner) || publication_.retirementGeneration || cargo.empty()
            || cargo.size()>publication_.cargo.size() || revision==0
            || revision>native_atom::kMaximumCounter || revision<=publication_.deliveryRevision) {
            return false;
        }
        for(std::size_t i=0;i<cargo.size();++i) {
            if(cargo[i].registry==0 || cargo[i].registry==native_atom::kAbsent
                || cargo[i].type!=1 || cargo[i].index<0) { return false; }
            for(std::size_t j=0;j<i;++j) {
                if(cargo[i].registry==cargo[j].registry && cargo[i].index==cargo[j].index) {
                    return false;
                }
            }
        }
        for(std::size_t i=0;i<cargo.size();++i) { publication_.cargo[i]=cargo[i]; }
        publication_.cargoCount=static_cast<std::uint8_t>(cargo.size());
        publication_.deliveryRevision=revision; owner_=owner; finalAtomDetached_=false; return true;
    }
    [[nodiscard]] bool retire(Token owner) noexcept {
        if(!same_run(owner) || publication_.retirementGeneration) { return false; }
        publication_.retirementGeneration=publication_.program.generation+1U;
        owner_=owner; finalAtomDetached_=false; return true;
    }
    template<class Delta> void observe(std::uint64_t run,const Delta& delta) noexcept {
        if(run==owner_.run && publication_.program.count && !publication_.retirementGeneration) {
            if(delta.snapshotValid && delta.detached && state_.identified) {
                auto finalState=state_;
                auto levels=delta;
                levels.detached=false;
                finalState.merge(levels);
                const auto& program=publication_.program;
                const auto last=static_cast<std::uint8_t>(program.count-1U);
                finalAtomDetached_=finalAtomDetached_
                    || finalState.program(program.generation,program.revision,last)
                    || finalState.program(program.generation,program.revision,program.count);
            }
            state_.merge(delta);
        }
    }
    [[nodiscard]] bool owns(Token owner) const noexcept { return owner==owner_; }
    [[nodiscard]] bool detached_after_last_atom(Token owner) const noexcept {
        return owns(owner) && finalAtomDetached_;
    }
private:
    [[nodiscard]] bool same_run(Token token) const noexcept {
        return token.run!=0 && token.run==owner_.run && token.incarnation==owner_.incarnation;
    }
    Token owner_{};
    ActorPublication publication_{};
    CombatantState state_{};
    bool finalAtomDetached_{};
};
} // namespace dawn::state::activity::coo
