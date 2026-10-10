#pragma once
#include "script_views.h"
#include <filesystem>
#include <memory>
#include <string>

namespace dawn::state::activity::coo::script {
struct Capability final { std::string_view id,domain; CommandSpec spec{}; std::uint32_t argumentMaximum{}; };
struct ModuleCapability final { std::string_view id; ModuleBinding binding{}; };
struct FactCapability final { std::string_view id; std::uint8_t fact{}; };
struct EventCapability final { std::string_view set,id; std::uint32_t event{}; std::uint8_t allowedCycles{}; };
struct MarkerCapability final { std::string_view id;MarkerTarget target; };
struct ParameterCapability final {
    std::string_view id;
    std::uint32_t minimum{}, maximum{}, defaultValue{};
    bool liveEditable{};
};
struct Profile final {
    std::string_view id,schemaName;
    Schema schema{};
    std::span<const Capability> capabilities;
    std::span<const ModuleCapability> modules;
    std::span<const FactCapability> facts;
    DialogueDefinition dialogue{};
    std::span<const std::uint32_t> objectives;
    std::span<const EventCapability> events;
    std::span<const PresentationTable> tables;
    std::span<const MarkerCapability> markers{};
    std::span<const ParameterCapability> parameters{};
};
[[nodiscard]] bool authorized(const Views& views,const Profile& profile) noexcept;
class MissionDocument final {
public:
    ~MissionDocument();
    MissionDocument(const MissionDocument&)=delete;
    MissionDocument& operator=(const MissionDocument&)=delete;
    [[nodiscard]] static std::unique_ptr<MissionDocument> parse(std::string_view text,const Profile& profile,std::string& error) noexcept;
    [[nodiscard]] static std::unique_ptr<MissionDocument> read_native_policy(const std::filesystem::path& path,const Profile& profile,std::string& error) noexcept;
    [[nodiscard]] static std::unique_ptr<MissionDocument> parse_lua(std::string_view text,const Profile& profile,std::string& error,std::string_view sourceName="mission.lua") noexcept;
    [[nodiscard]] static std::unique_ptr<MissionDocument> read(const std::filesystem::path& path,const Profile& profile,std::string& error) noexcept;
    [[nodiscard]] const Views& views() const noexcept;
    [[nodiscard]] std::uint64_t fingerprint() const noexcept;
    [[nodiscard]] bool same_structure(const MissionDocument& other) const noexcept;
private:
    MissionDocument();
    struct Storage;
    std::unique_ptr<Storage> storage_;
};
} // namespace dawn::state::activity::coo::script
