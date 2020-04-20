#include "pch.h"
#include "ProfileGroup.h"
#include "JsonUtils.h"

using namespace TerminalApp;

static constexpr std::string_view NameKey{ "name" };
static constexpr std::string_view HiddenKey{ "hidden" };

static constexpr std::string_view IconKey{ "icon" };

ProfileGroup::ProfileGroup(std::vector<std::variant<Profile, ProfileGroup>> profilesAndGroups) :
    _profilesAndGroups(profilesAndGroups),

    _name{ L"Group" },
    _hidden{ false },

    _icon{}
{
}

ProfileGroup::~ProfileGroup()
{
}

std::wstring_view ProfileGroup::GetName() const noexcept
{
    return _name;
}

bool ProfileGroup::IsHidden() const noexcept
{
    return _hidden;
}

bool ProfileGroup::HasIcon() const noexcept
{
    return _icon.has_value() && !_icon.value().empty();
}

winrt::hstring ProfileGroup::GetExpandedIconPath() const
{
    if (!HasIcon())
    {
        return { L"" };
    }
    winrt::hstring envExpandedPath{ wil::ExpandEnvironmentStringsW<std::wstring>(_icon.value().data()) };
    return envExpandedPath;
}

void ProfileGroup::LayerJson(const Json::Value& json)
{
    JsonUtils::GetWstring(json, NameKey, _name);

    JsonUtils::GetBool(json, HiddenKey, _hidden);

    JsonUtils::GetOptionalString(json, IconKey, _icon);
}

std::basic_string_view<std::variant<Profile, ProfileGroup>> ProfileGroup::GetProfilesAndGroups() const noexcept
{
    return GetProfilesAndGroups(_profilesAndGroups);
}

std::basic_string_view<std::variant<Profile, ProfileGroup>> ProfileGroup::GetProfilesAndGroups(const std::vector<std::variant<Profile, ProfileGroup>>& profilesAndGroups) noexcept
{
    return { &profilesAndGroups[0], profilesAndGroups.size() };
}

void ProfileGroup::ExtractProfiles(std::vector<Profile>& profiles)
{
    ExtractProfiles(_profilesAndGroups, profiles);
}

void ProfileGroup::ExtractProfiles(const std::vector<std::variant<Profile, ProfileGroup>>& profilesAndGroups, std::vector<Profile>& profiles)
{
    for (auto& profileOrGroup : profilesAndGroups)
    {
        if (std::holds_alternative<Profile>(profileOrGroup))
        {
            profiles.emplace_back(std::get<Profile>(profileOrGroup));
        }
        else if (std::holds_alternative<ProfileGroup>(profileOrGroup))
        {
            auto group = std::get<ProfileGroup>(profileOrGroup);
            group.ExtractProfiles(profiles);
        }
    }
}

const std::optional<Profile> ProfileGroup::FindProfile(GUID profileGuid) const noexcept
{
    return FindProfile(_profilesAndGroups, profileGuid);
}

const std::optional<Profile> ProfileGroup::FindProfile(const std::vector<std::variant<Profile, ProfileGroup>>& profilesAndGroups, GUID profileGuid) noexcept
{
    for (auto& profileOrGroup : profilesAndGroups)
    {
        if (std::holds_alternative<Profile>(profileOrGroup))
        {
            auto profile = std::get<Profile>(profileOrGroup);
            try
            {
                if (profile.GetGuid() == profileGuid)
                {
                    return profile;
                }
            }
            CATCH_LOG();
        }
        else if (std::holds_alternative<ProfileGroup>(profileOrGroup))
        {
            auto group = std::get<ProfileGroup>(profileOrGroup);
            return group.FindProfile(profileGuid);
        }
    }

    return std::nullopt;
}

void ProfileGroup::RemoveHiddenProfiles()
{
    RemoveHiddenProfiles(_profilesAndGroups);
}

void ProfileGroup::RemoveHiddenProfiles(std::vector<std::variant<Profile, ProfileGroup>>& profilesAndGroups)
{
    // remove_if will move all the profiles where the lambda is true to the end
    // of the list, then return a iterator to the point in the list where those
    // profiles start. The erase call will then remove all of those profiles
    // from the list. This is the [erase-remove
    // idiom](https://en.wikipedia.org/wiki/Erase%E2%80%93remove_idiom)
    profilesAndGroups.erase(std::remove_if(profilesAndGroups.begin(),
                                           profilesAndGroups.end(),
                                           [](auto&& profileOrGroup) {
                                               if (std::holds_alternative<Profile>(profileOrGroup))
                                               {
                                                   auto profile = std::get<Profile>(profileOrGroup);
                                                   return profile.IsHidden();
                                               }
                                               else if (std::holds_alternative<ProfileGroup>(profileOrGroup))
                                               {
                                                   auto group = std::get<ProfileGroup>(profileOrGroup);
                                                   group.RemoveHiddenProfiles();
                                                   return group.IsHidden();
                                               }
                                               return false;
                                           }),
                            profilesAndGroups.end());
}

bool ProfileGroup::RemoveDuplicateProfiles(std::set<GUID>& uniqueGuids)
{
    return RemoveDuplicateProfiles(_profilesAndGroups, uniqueGuids);
}

bool ProfileGroup::RemoveDuplicateProfiles(std::vector<std::variant<Profile, ProfileGroup>>& profilesAndGroups, std::set<GUID>& uniqueGuids)
{
    bool foundDupe = false;
    bool foundDupeOverall = false;
    std::vector<size_t> indicesToDelete;

    // Try collecting all the unique guids. If we ever encounter a guid that's
    // already in the set, then we need to delete that profile.
    for (size_t i = 0; i < profilesAndGroups.size(); i++)
    {
        auto& profileOrGroup = profilesAndGroups.at(i);
        if (std::holds_alternative<Profile>(profileOrGroup))
        {
            auto profile = std::get<Profile>(profileOrGroup);

            if (!uniqueGuids.insert(profile.GetGuid()).second)
            {
                foundDupe = true;
                foundDupeOverall = true;
                indicesToDelete.push_back(i);
            }
        }
        else if (std::holds_alternative<ProfileGroup>(profileOrGroup))
        {
            auto group = std::get<ProfileGroup>(profileOrGroup);
            if (group.RemoveDuplicateProfiles(uniqueGuids))
            {
                foundDupeOverall = true;
            }
        }
    }

    // Remove all the duplicates we've marked
    // Walk backwards, so we don't accidentally shift any of the elements
    for (auto iter = indicesToDelete.rbegin(); iter != indicesToDelete.rend(); iter++)
    {
        profilesAndGroups.erase(profilesAndGroups.begin() + *iter);
    }

    return foundDupeOverall;
}
