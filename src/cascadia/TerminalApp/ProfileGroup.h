#pragma once
#include <variant>
#include "Profile.h"
#include "Utils.h"

namespace TerminalApp
{
    class ProfileGroup;
}

class TerminalApp::ProfileGroup final
{
public:
    ProfileGroup(std::vector<std::variant<Profile, ProfileGroup>> profilesAndGroups);

    ~ProfileGroup();

    void LayerJson(const Json::Value& json);

    std::wstring_view GetName() const noexcept;
    bool IsHidden() const noexcept;
    bool HasIcon() const noexcept;
    winrt::hstring GetExpandedIconPath() const;

    void SetName(const std::wstring_view name) noexcept;
    void SetIconPath(std::wstring_view path);

    std::basic_string_view<std::variant<Profile, ProfileGroup>> GetProfilesAndGroups() const noexcept;
    static std::basic_string_view<std::variant<Profile, ProfileGroup>> GetProfilesAndGroups(const std::vector<std::variant<Profile, ProfileGroup>>& profilesAndGroups) noexcept;

    void ExtractProfiles(std::vector<Profile>& profiles);
    static void ExtractProfiles(const std::vector<std::variant<Profile, ProfileGroup>>& profilesAndGroups, std::vector<Profile>& profiles);

    void RemoveHiddenProfiles();
    static void RemoveHiddenProfiles(std::vector<std::variant<Profile, ProfileGroup>>& profilesAndGroups);

    const std::optional<Profile> FindProfile(GUID profileGuid) const noexcept;
    static const std::optional<Profile> FindProfile(const std::vector<std::variant<Profile, ProfileGroup>>& profilesAndGroups, GUID profileGuid) noexcept;

    bool RemoveDuplicateProfiles(std::set<GUID>& uniqueGuids);
    static bool RemoveDuplicateProfiles(std::vector<std::variant<Profile, ProfileGroup>>& profilesAndGroups, std::set<GUID>& uniqueGuids);

private:
    std::vector<std::variant<Profile, ProfileGroup>> _profilesAndGroups;
    std::wstring _name;
    bool _hidden;
    std::optional<std::wstring> _icon;
};
