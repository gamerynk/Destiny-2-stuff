#include "image.h"

#include <Windows.h>

namespace dawn::client::executable {
namespace {

[[nodiscard]] bool add_fits(std::size_t left, std::size_t right, std::size_t limit) noexcept {
    return left <= limit && right <= limit - left;
}

[[nodiscard]] bool read_memory(void* output, const void* input, std::size_t size) noexcept {
    SIZE_T copied = 0;
    return ReadProcessMemory(GetCurrentProcess(), input, output, size, &copied) != FALSE
           && copied == size;
}

} // namespace

bool inspect(std::byte* base, ExecutableImage& output) noexcept {
    output = {};
    if (base == nullptr) {
        return false;
    }

    IMAGE_DOS_HEADER dos{};
    if (!read_memory(&dos, base, sizeof dos) || dos.e_magic != IMAGE_DOS_SIGNATURE
        || dos.e_lfanew <= 0) {
        return false;
    }

    IMAGE_NT_HEADERS64 nt{};
    if (!read_memory(&nt, base + dos.e_lfanew, sizeof nt) || nt.Signature != IMAGE_NT_SIGNATURE
        || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC
        || nt.FileHeader.NumberOfSections == 0
        || nt.FileHeader.NumberOfSections > kPeSectionLimit) {
        return false;
    }

    const std::size_t imageSize = nt.OptionalHeader.SizeOfImage;
    const std::size_t sectionOffset = static_cast<std::size_t>(dos.e_lfanew) + sizeof(DWORD)
                                      + sizeof(IMAGE_FILE_HEADER)
                                      + nt.FileHeader.SizeOfOptionalHeader;
    const std::size_t sectionBytes =
        static_cast<std::size_t>(nt.FileHeader.NumberOfSections) * sizeof(IMAGE_SECTION_HEADER);
    if (imageSize == 0 || !add_fits(sectionOffset, sectionBytes, imageSize)) {
        return false;
    }

    for (std::size_t index = 0; index < nt.FileHeader.NumberOfSections; ++index) {
        IMAGE_SECTION_HEADER section{};
        const std::size_t offset = sectionOffset + index * sizeof(IMAGE_SECTION_HEADER);
        if (!read_memory(&section, base + offset, sizeof section)) {
            return false;
        }
        if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0
            || section.Misc.VirtualSize == 0) {
            continue;
        }

        const std::size_t virtualAddress = section.VirtualAddress;
        const std::size_t virtualSize = section.Misc.VirtualSize;
        if (!add_fits(virtualAddress, virtualSize, imageSize)) {
            return false;
        }
        output.sections[output.count++] = std::span(base + virtualAddress, virtualSize);
    }
    return output.count != 0;
}

bool inspect_main_module(ExecutableImage& output) noexcept {
    return inspect(reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr)), output);
}

} // namespace dawn::client::executable
