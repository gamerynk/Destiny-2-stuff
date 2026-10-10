#include <Windows.h>

#include <algorithm>
#include <array>
#include <vector>

#include "parallel.h"

namespace dawn::middleware::content::packages::reader::parallel {
namespace {

constexpr std::size_t kReservedCores = 2;

struct Share {
    const Source* source{};
    std::span<const std::uint32_t> tags;
    Visitor visitor{};
    void* context{};
    std::size_t worker{};
    Scratch* scratch{};
    std::vector<std::byte>* bytes{};
};

// megabytes each, so allocated rather than declared; only the reader owning the index
// touches one
std::vector<Scratch> g_scratch{};
std::vector<std::vector<std::byte>> g_bytes{};

struct KeptRow {
    std::uint32_t tag{};
    std::size_t offset{};
    std::size_t size{};
};

struct Keep {
    std::vector<std::byte> store;
    std::vector<KeptRow> rows;
};

std::vector<Keep> g_keep{};

void run_share(Share& share) noexcept {
    for (const std::uint32_t tag : share.tags) {
        if (read_tag(*share.source, *share.scratch, tag, *share.bytes)) {
            share.visitor(share.context, share.worker, tag, *share.bytes);
        }
    }
}

DWORD WINAPI thread_main(LPVOID parameter) noexcept {
    run_share(*static_cast<Share*>(parameter));
    return 0;
}

void keep_blob(void*,
               std::size_t worker,
               std::uint32_t tag,
               std::span<const std::byte> blob) noexcept {
    if (worker >= g_keep.size()) {
        return;
    }
    Keep& keep = g_keep[worker];
    const std::size_t offset = keep.store.size();
    keep.store.insert(keep.store.end(), blob.begin(), blob.end());
    keep.rows.push_back(KeptRow{tag, offset, blob.size()});
}

[[nodiscard]] bool prepare(std::size_t workers) noexcept {
    if (g_scratch.size() >= workers) {
        return true;
    }
    g_scratch.resize(workers);
    g_bytes.resize(workers);
    return g_scratch.size() >= workers && g_bytes.size() >= workers;
}

} // namespace

std::size_t worker_count() noexcept {
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const auto cores = static_cast<std::size_t>(info.dwNumberOfProcessors);
    const std::size_t spare = cores > kReservedCores ? cores - kReservedCores : 1;
    return (std::min)(spare, kMaxWorkers);
}

bool read_tags(const Source& source,
               std::span<const std::uint32_t> tags,
               Visitor visitor,
               void* context) noexcept {
    if (visitor == nullptr) {
        return false;
    }
    if (tags.empty()) {
        return true;
    }
    const std::size_t workers = (std::min)(worker_count(), tags.size());
    if (!prepare(workers)) {
        return false;
    }

    // each reader takes one run of tags, not every nth: neighbours share a block, so its own
    // block cache hits more
    const std::size_t share = (tags.size() + workers - 1U) / workers;
    std::array<Share, kMaxWorkers> shares{};
    std::array<HANDLE, kMaxWorkers> threads{};
    std::size_t spawned = 0;
    for (std::size_t worker = 0; worker < workers; ++worker) {
        const std::size_t begin = (std::min)(worker * share, tags.size());
        const std::size_t end = (std::min)(begin + share, tags.size());
        shares[worker] = Share{&source,
                               tags.subspan(begin, end - begin),
                               visitor,
                               context,
                               worker,
                               &g_scratch[worker],
                               &g_bytes[worker]};
    }
    std::array<bool, kMaxWorkers> started{};
    for (std::size_t worker = 1; worker < workers; ++worker) {
        const HANDLE thread = CreateThread(nullptr, 0, &thread_main, &shares[worker], 0, nullptr);
        if (thread == nullptr) {
            continue;
        }
        threads[spawned++] = thread;
        started[worker] = true;
    }
    run_share(shares[0]);
    // a reader that didn't start is read here on this thread with its own storage, so the
    // batch is never left partly read; a started share is untouched until its thread joins
    for (std::size_t worker = 1; worker < workers; ++worker) {
        if (!started[worker]) {
            shares[worker].worker = 0;
            shares[worker].scratch = &g_scratch[0];
            shares[worker].bytes = &g_bytes[0];
            run_share(shares[worker]);
        }
    }
    if (spawned != 0) {
        (void)WaitForMultipleObjects(static_cast<DWORD>(spawned), threads.data(), TRUE, INFINITE);
        for (std::size_t index = 0; index < spawned; ++index) {
            (void)CloseHandle(threads[index]);
        }
    }
    return true;
}

bool read_kept(const Source& source,
               std::span<const std::uint32_t> tags,
               std::vector<Held>& kept) noexcept {
    kept.clear();
    const std::size_t workers = worker_count();
    if (g_keep.size() < workers) {
        g_keep.resize(workers);
    }
    if (g_keep.size() < workers) {
        return false;
    }
    for (Keep& keep : g_keep) {
        keep.store.clear();
        keep.rows.clear();
    }
    if (!read_tags(source, tags, &keep_blob, nullptr)) {
        return false;
    }
    for (const Keep& keep : g_keep) {
        for (const KeptRow& row : keep.rows) {
            kept.push_back(Held{row.tag, {keep.store.data() + row.offset, row.size}});
        }
    }
    return true;
}

void release() noexcept {
    for (Scratch& scratch : g_scratch) {
        close_files(scratch);
    }
    g_scratch.clear();
    g_scratch.shrink_to_fit();
    g_bytes.clear();
    g_bytes.shrink_to_fit();
    g_keep.clear();
    g_keep.shrink_to_fit();
}

} // namespace dawn::middleware::content::packages::reader::parallel
