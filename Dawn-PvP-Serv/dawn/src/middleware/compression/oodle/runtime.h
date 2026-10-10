#pragma once

#include <Windows.h>

#include <cstddef>
#include <span>

namespace dawn::middleware::compression::oodle {

[[nodiscard]] bool
required_capacity(HMODULE module, std::size_t inputSize, std::size_t& capacity) noexcept;

[[nodiscard]] bool compress(HMODULE module,
                            std::span<const std::byte> input,
                            std::span<std::byte> output,
                            std::size_t& written) noexcept;

// Oodle rejects any size that isn't a multiple of the decode step, so step down until
// one is accepted
[[nodiscard]] bool decompress_installed_block(std::span<const std::byte> input,
                                              std::span<std::byte> output,
                                              std::size_t& written) noexcept;

[[nodiscard]] bool compress_installed(std::span<const std::byte> input,
                                      std::span<std::byte> output,
                                      std::size_t& written) noexcept;

[[nodiscard]] bool
decompress(HMODULE module, std::span<const std::byte> input, std::span<std::byte> output) noexcept;

} // namespace dawn::middleware::compression::oodle
