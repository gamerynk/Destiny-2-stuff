#pragma once

#include <d3d11.h>

namespace dawn::client::hooks::graphics::textures {

struct Uploaded {
    ID3D11Texture2D* texture{};
    ID3D11ShaderResourceView* view{};
};

[[nodiscard]] bool upload_logo_sheet(ID3D11Device* device, Uploaded& output) noexcept;

void release_logo_sheet(Uploaded& uploaded) noexcept;

} // namespace dawn::client::hooks::graphics::textures
