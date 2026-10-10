#pragma once
#include <Windows.h>
#include <array>
#include <span>

namespace dawn::steam::runtime::social {
template<class Packet,std::size_t Capacity,std::size_t BatchCapacity>
class BoundedBatchSender final {
    static_assert(Capacity>0 && BatchCapacity>0 && BatchCapacity<=Capacity);
public:
    using Send=void (*)(std::span<const Packet>) noexcept;
    BoundedBatchSender()=default;
    BoundedBatchSender(const BoundedBatchSender&)=delete;
    BoundedBatchSender& operator=(const BoundedBatchSender&)=delete;
    bool start(Send send) noexcept {
        if(!send)return false;
        AcquireSRWLockExclusive(&lock_);
        if(thread_) {const bool active=accepting_;ReleaseSRWLockExclusive(&lock_);return active;}
        send_=send;stopping_=false;head_=count_=0;
        thread_=CreateThread(nullptr,0,&run,this,0,nullptr);accepting_=thread_!=nullptr;
        const bool active=accepting_;ReleaseSRWLockExclusive(&lock_);return active;
    }
    bool enqueue(const Packet& packet) noexcept {
        AcquireSRWLockExclusive(&lock_);
        const bool accepted=accepting_ && count_<Capacity;
        if(accepted){packets_[(head_+count_)%Capacity]=packet;++count_;WakeConditionVariable(&ready_);}
        ReleaseSRWLockExclusive(&lock_);return accepted;
    }
    void stop() noexcept {
        AcquireSRWLockExclusive(&lock_);
        accepting_=false;stopping_=true;head_=count_=0;const HANDLE thread=thread_;
        WakeAllConditionVariable(&ready_);ReleaseSRWLockExclusive(&lock_);
        if(thread)WaitForSingleObject(thread,INFINITE);
        AcquireSRWLockExclusive(&lock_);
        if(thread_==thread)thread_=nullptr;
        ReleaseSRWLockExclusive(&lock_);if(thread)CloseHandle(thread);
    }
private:
    static DWORD WINAPI run(void* context) noexcept {
        auto& self=*static_cast<BoundedBatchSender*>(context);
        for(;;) {
            std::array<Packet,BatchCapacity> batch{};std::size_t count{};
            AcquireSRWLockExclusive(&self.lock_);
            while(!self.stopping_ && self.count_==0)
                SleepConditionVariableSRW(&self.ready_,&self.lock_,INFINITE,0);
            if(self.stopping_){ReleaseSRWLockExclusive(&self.lock_);return 0;}
            while(self.count_ && count<BatchCapacity) {
                batch[count++]=self.packets_[self.head_];
                self.head_=(self.head_+1)%Capacity;--self.count_;
            }
            ReleaseSRWLockExclusive(&self.lock_);
            self.send_(std::span(batch).first(count));
        }
    }
    SRWLOCK lock_{SRWLOCK_INIT};CONDITION_VARIABLE ready_{CONDITION_VARIABLE_INIT};
    std::array<Packet,Capacity> packets_{};
    std::size_t head_{},count_{};Send send_{};HANDLE thread_{};bool accepting_{},stopping_{};
};
} // namespace dawn::steam::runtime::social
