#include "../dawn/src/client/hooks/external_server/url_diagnostic.h"
#include <cassert>
int main() {
 using dawn::client::hooks::external_server::diagnostic::safe_url;
 assert(safe_url("https://www1.example.test/SignOn")=="https://www1.example.test/SignOn");
 assert(safe_url("https://www1.example.test/SignOn?token=sample#sample")=="https://www1.example.test/SignOn");
 assert(safe_url("https://sample:sample@example.test/SignOn?sample")=="<redacted-url>");
 assert(safe_url("http://127.0.0.1:18101/config#sample")=="http://127.0.0.1:18101/config");
 assert(safe_url("https://example.test/path\r\nInjected: sample")=="https://example.test/path");
 assert(safe_url("").empty());
 std::uint32_t rva = 7;
 assert(dawn::client::hooks::external_server::diagnostic::main_image_rva(0x100000, 0x2000, 0x100000, rva) && rva == 0);
 assert(dawn::client::hooks::external_server::diagnostic::main_image_rva(0x100000, 0x2000, 0x101fff, rva) && rva == 0x1fff);
 assert(!dawn::client::hooks::external_server::diagnostic::main_image_rva(0x100000, 0x2000, 0xfffff, rva));
 assert(!dawn::client::hooks::external_server::diagnostic::main_image_rva(0x100000, 0x2000, 0x102000, rva));
 assert(!dawn::client::hooks::external_server::diagnostic::main_image_rva(0x100000, 0, 0x100000, rva));
}
