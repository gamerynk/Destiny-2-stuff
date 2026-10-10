#include <Windows.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace {
int checks{};
void require(bool result, const char* message) {
    ++checks;
    if (!result) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
void create_queue() {
    MSG message{};
    (void)PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
}
}
int main() {
    create_queue();
    std::array<BYTE,256> initial{};
    require(GetKeyboardState(initial.data()) != FALSE, "read calling thread's initial keyboard table");
    struct Restore {
        std::array<BYTE,256>& value;
        ~Restore() { (void)SetKeyboardState(value.data()); }
    } restore{initial};
    HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    HANDLE inspect=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    require(ready && inspect,"create private test synchronization events");
    std::array<BYTE,256> otherBefore{},otherAfter{};
    bool otherReadBefore{},otherReadAfter{};
    std::thread other([&] {
        create_queue();
        otherReadBefore=GetKeyboardState(otherBefore.data()) != FALSE;
        SetEvent(ready);
        if (WaitForSingleObject(inspect,5000)==WAIT_OBJECT_0)
            otherReadAfter=GetKeyboardState(otherAfter.data()) != FALSE;
    });
    require(WaitForSingleObject(ready,5000)==WAIT_OBJECT_0,"other input thread is ready");
    auto pressed=initial;
    pressed['W']|=0x80;
    require(SetKeyboardState(pressed.data()) != FALSE,"native producer publishes held key on calling thread");
    require((GetKeyState('W')&0x8000) != 0,"ordinary unhooked GetKeyState observes produced held state");
    std::array<BYTE,256> observed{};
    require(GetKeyboardState(observed.data()) != FALSE && observed==pressed,
            "other key and toggle states are preserved in the calling thread");
    SetEvent(inspect);
    other.join();
    require(otherReadBefore && otherReadAfter && otherBefore==otherAfter,
            "native keyboard producer does not alter another thread's table");
    auto released=observed;
    released['W']&=0x7F;
    require(SetKeyboardState(released.data()) != FALSE && (GetKeyState('W')&0x8000)==0,
            "native producer releases the key without an export guard");
    require(SetKeyboardState(initial.data()) != FALSE,"restore exact initial native state");
    require(GetKeyboardState(observed.data()) != FALSE && observed==initial,
            "all256 original states are restored");
    CloseHandle(ready);CloseHandle(inspect);
    std::printf("PASS: %d real Windows keyboard producer checks; private threads only\n",checks);
}
