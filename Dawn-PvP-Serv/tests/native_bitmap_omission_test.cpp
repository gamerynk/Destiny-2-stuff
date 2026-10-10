#include <cstdio>
#include <string_view>
#include "../dawn/src/client/hooks/bitmap/bitmap_hook_lifecycle.cpp"
namespace fixture { unsigned installs{},uninstalls{},logs{}; }
namespace dawn::client::hooks::bitmap {
bool install_bitmap_ref_guard() noexcept { ++fixture::installs; return true; }
void uninstall_bitmap_ref_guard() noexcept { ++fixture::uninstalls; }
}
namespace dawn::core::log {
void write(Channel,Level,std::string_view) noexcept { ++fixture::logs; }
}
int main() {
    namespace b=dawn::client::hooks::bitmap;
    if(!b::install() || b::is_installed() || fixture::installs!=0 || fixture::logs!=1) return 1;
    b::uninstall();
    if(b::is_installed() || fixture::installs!=0 || fixture::uninstalls!=1) return 2;
    std::puts("PASS bitmap omission lifecycle: no guard installation; attachment status remains false");
}
