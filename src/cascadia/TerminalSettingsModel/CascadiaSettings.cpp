// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include <argb.h>
#include <conattrs.hpp>
#include <io.h>
#include <fcntl.h>
#include "CascadiaSettings.h"
#include "../../types/inc/utils.hpp"
#include "../../inc/DefaultSettings.h"
#include "Utils.h"
#include "LibraryResources.h"

#include "PowershellCoreProfileGenerator.h"
#include "WslDistroGenerator.h"
#include "AzureCloudShellGenerator.h"

#include "CascadiaSettings.g.cpp"

using namespace ::Microsoft::Terminal::Settings::Model;
using namespace winrt::Microsoft::Terminal::TerminalControl;
using namespace winrt::Microsoft::Terminal::Settings::Model::implementation;
using namespace winrt::Windows::Foundation::Collections;
using namespace Microsoft::Console;

static constexpr std::wstring_view PACKAGED_PROFILE_ICON_PATH{ L"ms-appx:///ProfileIcons/" };

static constexpr std::wstring_view PACKAGED_PROFILE_ICON_EXTENSION{ L".png" };
static constexpr std::wstring_view DEFAULT_LINUX_ICON_GUID{ L"{9acb9455-ca41-5af7-950f-6bca1bc9722f}" };

// make sure this matches defaults.json.
static constexpr std::wstring_view DEFAULT_WINDOWS_POWERSHELL_GUID{ L"{61c54bbd-c2c6-5271-96e7-009a87ff44bf}" };

CascadiaSettings::CascadiaSettings() :
    CascadiaSettings(true)
{
}

// Constructor Description:
// - Creates a new settings object. If addDynamicProfiles is true, we'll
//   automatically add the built-in profile generators to our list of profile
//   generators. Set this to `false` for unit testing.
// Arguments:
// - addDynamicProfiles: if true, we'll add the built-in DPGs.
CascadiaSettings::CascadiaSettings(const bool addDynamicProfiles) :
    _globals{ winrt::make_self<implementation::GlobalAppSettings>() },
    _allProfiles{ winrt::single_threaded_observable_vector<Model::Profile>() },
    _activeProfiles{ winrt::single_threaded_observable_vector<Model::Profile>() },
    _warnings{ winrt::single_threaded_vector<SettingsLoadWarnings>() },
    _deserializationErrorMessage{ L"" }
{
    if (addDynamicProfiles)
    {
        _profileGenerators.emplace_back(std::make_unique<PowershellCoreProfileGenerator>());
        _profileGenerators.emplace_back(std::make_unique<WslDistroGenerator>());
        _profileGenerators.emplace_back(std::make_unique<AzureCloudShellGenerator>());
    }
}

CascadiaSettings::CascadiaSettings(winrt::hstring json) :
    CascadiaSettings(false)
{
    std::optional<GUID> profileGuid{};
    std::vector<Profile> profiles{};
    ProfileGroup::ExtractProfiles(_profilesAndGroups, profiles);

    for (const auto& profile : profiles)
    {
        if (profileName == profile.GetName())
        {
            try
            {
                profileGuid = profile.GetGuid();
            }
            CATCH_LOG();

            break;
        }
    }

// Method Description:
// - Copies the inheritance tree for profiles and hooks them up to a clone CascadiaSettings
// Arguments:
// - cloneSettings: the CascadiaSettings we're copying the inheritance tree to
// Return Value:
// - <none>
void CascadiaSettings::_CopyProfileInheritanceTree(winrt::com_ptr<CascadiaSettings>& cloneSettings) const
{
    // Our profiles inheritance graph doesn't have a formal root.
    // However, if we create a dummy Profile, and set _profiles as the parent,
    //  we now have a root. So we'll do just that, then copy the inheritance graph
    //  from the dummyRoot.
    auto dummyRootSource{ winrt::make_self<Profile>() };
    for (const auto& profile : _allProfiles)
    {
        winrt::com_ptr<Profile> profileImpl;
        profileImpl.copy_from(winrt::get_self<Profile>(profile));
        dummyRootSource->InsertParent(profileImpl);
    }

    auto dummyRootClone{ winrt::make_self<Profile>() };
    std::unordered_map<void*, winrt::com_ptr<Profile>> visited{};

    if (_userDefaultProfileSettings)
    {
        // profile.defaults must be saved to CascadiaSettings
        // So let's do that manually first, and add that to visited
        cloneSettings->_userDefaultProfileSettings = Profile::CopySettings(_userDefaultProfileSettings);
        visited[_userDefaultProfileSettings.get()] = cloneSettings->_userDefaultProfileSettings;
    }

    Profile::CloneInheritanceGraph(dummyRootSource, dummyRootClone, visited);

    // All of the parents of the dummy root clone are _profiles.
    // Get the parents and add them to the settings clone.
    const auto cloneParents{ dummyRootClone->Parents() };
    for (const auto& profile : cloneParents)
    {
        cloneSettings->_allProfiles.Append(*profile);
        if (!profile->Hidden())
        {
            cloneSettings->_activeProfiles.Append(*profile);
        }
    }
}

// Method Description:
// - Finds a profile that matches the given GUID. If there is no profile in this
//      settings object that matches, returns nullptr.
// Arguments:
// - profileGuid: the GUID of the profile to return.
// Return Value:
// - a non-ownership pointer to the profile matching the given guid, or nullptr
//      if there is no match.
const std::optional<Profile> CascadiaSettings::FindProfile(GUID profileGuid) const noexcept
{
    return ProfileGroup::FindProfile(_profilesAndGroups, profileGuid);
}

// Method Description:
// - Returns an iterable collection of all of our Profiles.
// Arguments:
// - <none>
// Return Value:
// - an iterable collection of all of our Profiles.
IObservableVector<winrt::Microsoft::Terminal::Settings::Model::Profile> CascadiaSettings::AllProfiles() const noexcept
{
    return _allProfiles;
}

// Method Description:
// - Returns an iterable collection of all of our non-hidden Profiles.
// Arguments:
// - <none>
// Return Value:
// - an iterable collection of all of our Profiles.
IObservableVector<winrt::Microsoft::Terminal::Settings::Model::Profile> CascadiaSettings::ActiveProfiles() const noexcept
{
    return _activeProfiles;
}

std::basic_string_view<std::variant<Profile, ProfileGroup>> CascadiaSettings::GetProfilesAndGroups() const noexcept
{
    return ProfileGroup::GetProfilesAndGroups(_profilesAndGroups);
}

// Method Description:
// - Returns the globally configured keybindings
// Arguments:
// - <none>
// Return Value:
// - the globally configured keybindings
winrt::Microsoft::Terminal::Settings::Model::KeyMapping CascadiaSettings::KeyMap() const noexcept
{
    return _globals->KeyMap();
}

// Method Description:
// - Get a reference to our global settings
// Arguments:
// - <none>
// Return Value:
// - a reference to our global settings
winrt::Microsoft::Terminal::Settings::Model::GlobalAppSettings CascadiaSettings::GlobalSettings() const
{
    return *_globals;
}

void CascadiaSettings::ExtractProfiles(std::vector<Profile>& profiles)
{
    ProfileGroup::ExtractProfiles(_profilesAndGroups, profiles);
}

// Method Description:
// - Get a reference to our profiles.defaults object
// Arguments:
// - <none>
// Return Value:
// - a reference to our profile.defaults object
winrt::Microsoft::Terminal::Settings::Model::Profile CascadiaSettings::ProfileDefaults() const
{
    return *_userDefaultProfileSettings;
}

// Method Description:
// - Create a new profile based off the default profile settings.
// Arguments:
// - <none>
// Return Value:
// - a reference to the new profile
winrt::Microsoft::Terminal::Settings::Model::Profile CascadiaSettings::CreateNewProfile()
{
    auto newProfile{ _userDefaultProfileSettings->CreateChild() };
    _allProfiles.Append(*newProfile);

    // Give the new profile a distinct name so a guid is properly generated
    const winrt::hstring newName{ fmt::format(L"Profile {}", _allProfiles.Size()) };
    newProfile->Name(newName);

    return *newProfile;
}

// Method Description:
// - Gets our list of warnings we found during loading. These are things that we
//   knew were bad when we called `_ValidateSettings` last.
// Return Value:
// - a reference to our list of warnings.
IVectorView<winrt::Microsoft::Terminal::Settings::Model::SettingsLoadWarnings> CascadiaSettings::Warnings()
{
    return _warnings.GetView();
}

void CascadiaSettings::ClearWarnings()
{
    _warnings.Clear();
}

void CascadiaSettings::AppendWarning(SettingsLoadWarnings warning)
{
    _warnings.Append(warning);
}

winrt::Windows::Foundation::IReference<winrt::Microsoft::Terminal::Settings::Model::SettingsLoadErrors> CascadiaSettings::GetLoadingError()
{
    return _loadError;
}

winrt::hstring CascadiaSettings::GetSerializationErrorMessage()
{
    return _deserializationErrorMessage;
}

// Method Description:
// - Attempts to validate this settings structure. If there are critical errors
//   found, they'll be thrown as a SettingsLoadError. Non-critical errors, such
//   as not finding the default profile, will only result in an error. We'll add
//   all these warnings to our list of warnings, and the application can chose
//   to display these to the user.
// Arguments:
// - <none>
// Return Value:
// - <none>
void CascadiaSettings::_ValidateSettings()
{
    // Make sure to check that profiles exists at all first and foremost:
    _ValidateProfilesExist();

    // Re-order profiles so that all profiles from the user's settings appear
    // before profiles that _weren't_ in the user profiles.
    _ReorderProfilesToMatchUserSettingsOrder();

    // Remove hidden profiles _after_ re-ordering. The re-ordering uses the raw
    // json, and will get confused if the profile isn't in the list.
    _UpdateActiveProfiles();

    // Then do some validation on the profiles. The order of these does not
    // terribly matter.
    _ValidateNoDuplicateProfiles();

    // Resolve the default profile before we validate that it exists.
    _ResolveDefaultProfile();
    _ValidateDefaultProfileExists();

    // Ensure that all the profile's color scheme names are
    // actually the names of schemes we've parsed. If the scheme doesn't exist,
    // just use the hardcoded defaults
    _ValidateAllSchemesExist();

    // Ensure all profile's with specified images resources have valid file path.
    // This validates icons and background images.
    _ValidateMediaResources();

    // TODO:GH#2548 ensure there's at least one key bound. Display a warning if
    // there's _NO_ keys bound to any actions. That's highly irregular, and
    // likely an indication of an error somehow.

    // GH#3522 - With variable args to keybindings, it's possible that a user
    // set a keybinding without all the required args for an action. Display a
    // warning if an action didn't have a required arg.
    // This will also catch other keybinding warnings, like from GH#4239
    _ValidateKeybindings();

    _ValidateColorSchemesInCommands();

    _ValidateNoGlobalsKey();
}

// Method Description:
// - Checks if the settings contain profiles at all. As we'll need to have some
//   profiles at all, we'll throw an error if there aren't any profiles.
void CascadiaSettings::_ValidateProfilesExist()
{
    std::vector<Profile> profiles{};
    ProfileGroup::ExtractProfiles(_profilesAndGroups, profiles);
    const bool hasProfiles = !profiles.empty();
    if (!hasProfiles)
    {
        // Throw an exception. This is an invalid state, and we want the app to
        // be able to gracefully use the default settings.

        // We can't add the warning to the list of warnings here, because this
        // object is not going to be returned at any point.

        throw SettingsException(Microsoft::Terminal::Settings::Model::SettingsLoadErrors::NoProfiles);
    }
}

// Method Description:
// - Walks through each profile, and ensures that they had a GUID set at some
//   point. If the profile did _not_ have a GUID ever set for it, generate a
//   temporary runtime GUID for it. This validation does not add any warnings.
void CascadiaSettings::_ValidateProfilesHaveGuid()
{
    std::vector<Profile> profiles{};
    ProfileGroup::ExtractProfiles(_profilesAndGroups, profiles);
    for (auto& profile : profiles)
    {
        auto maybeParsedDefaultProfile{ _GetProfileGuidByName(unparsedDefaultProfile) };
        auto defaultProfileGuid{ til::coalesce_value(maybeParsedDefaultProfile, winrt::guid{}) };
        GlobalSettings().DefaultProfile(defaultProfileGuid);
    }
}

// Method Description:
// - Checks if the "defaultProfile" is set to one of the profiles we
//   actually have. If the value is unset, or the value is set to something that
//   doesn't exist in the list of profiles, we'll arbitrarily pick the first
//   profile to use temporarily as the default.
// - Appends a SettingsLoadWarnings::MissingDefaultProfile to our list of
//   warnings if we failed to find the default.
void CascadiaSettings::_ValidateDefaultProfileExists()
{
    const winrt::guid defaultProfileGuid{ GlobalSettings().DefaultProfile() };
    const bool nullDefaultProfile = defaultProfileGuid == winrt::guid{};
    bool defaultProfileNotInProfiles = true;
    std::vector<Profile> profiles{};
    ProfileGroup::ExtractProfiles(_profilesAndGroups, profiles);

    for (const auto& profile : profiles)
    {
        if (profile.Guid() == defaultProfileGuid)
        {
            defaultProfileNotInProfiles = false;
            break;
        }
    }

    if (nullDefaultProfile || defaultProfileNotInProfiles)
    {
        _warnings.Append(Microsoft::Terminal::Settings::Model::SettingsLoadWarnings::MissingDefaultProfile);
        // Use the first profile as the new default

        // _temporarily_ set the default profile to the first profile. Because
        // we're adding a warning, this settings change won't be re-serialized.
        GlobalSettings().SetDefaultProfile(profiles[0].GetGuid());
    }
}

// Method Description:
// - Checks to make sure there aren't any duplicate profiles in the list of
//   profiles. If so, we'll remove the subsequent entries (temporarily), as they
//   won't be accessible anyways.
// - Appends a SettingsLoadWarnings::DuplicateProfile to our list of warnings if
//   we find any such duplicate.
void CascadiaSettings::_ValidateNoDuplicateProfiles()
{
    std::set<GUID> uniqueGuids;

    bool foundDupe = ProfileGroup::RemoveDuplicateProfiles(_profilesAndGroups, uniqueGuids);

    if (foundDupe)
    {
        _warnings.Append(Microsoft::Terminal::Settings::Model::SettingsLoadWarnings::DuplicateProfile);
    }
}

// Method Description:
// - Re-orders the list of profiles to match what the user would expect them to
//   be. Orders profiles to be in the ordering { [profiles from user settings],
//   [default profiles that weren't in the user profiles]}.
// - Does not set any warnings.
// Arguments:
// - <none>
// Return Value:
// - <none>
void CascadiaSettings::_ReorderProfilesToMatchUserSettingsOrder()
{
    std::set<GUID> uniqueGuids;
    std::deque<GUID> guidOrder;
    auto nullGuid = GUID{ 0 };

    auto collectGuids = [&](const auto& json) {
        for (auto profileJson : _GetProfilesJsonObject(json))
        {
            if (profileJson.isObject())
            {
                auto profilesList = _GetProfileGroupProfilesList(profileJson);
                if (profilesList != nullptr)
                {
                    guidOrder.push_back(nullGuid);
                }
                else
                {
                    auto guid = Profile::GetGuidOrGenerateForJson(profileJson);
                    if (uniqueGuids.insert(guid).second)
                    {
                        guidOrder.push_back(guid);
                    }
                }
            }
        }
    };

    // Push all the userSettings profiles' GUIDS into the set
    collectGuids(_userSettings);

    // Push all the defaultSettings profiles' GUIDS into the set
    collectGuids(_defaultSettings);
    std::equal_to<winrt::guid> equals;
    // Re-order the list of profiles to match that ordering
    // for (gIndex=0 -> uniqueGuids.size)
    //   pIndex = the pIndex of the profile with guid==guids[gIndex]
    //   profiles.swap(pIndex <-> gIndex)
    // This is O(N^2), which is kinda rough. I'm sure there's a better way
    for (uint32_t gIndex = 0; gIndex < guidOrder.size(); gIndex++)
    {
        const auto guid = guidOrder.at(gIndex);
        for (size_t pIndex = gIndex; pIndex < _profilesAndGroups.size(); pIndex++)
        {
            if (std::holds_alternative<Profile>(_profilesAndGroups.at(pIndex)))
            {
                auto profile = std::get<Profile>(_profilesAndGroups.at(pIndex));
                auto profileGuid = profile.GetGuid();
                if (equals(profileGuid, guid))
                {
                    std::iter_swap(_profilesAndGroups.begin() + pIndex, _profilesAndGroups.begin() + gIndex);
                    break;
                }
            }
        }
    }
}

// Method Description:
// - Updates the list of active profiles from the list of all profiles
// - If there are no active profiles (all profiles are hidden), throw a SettingsException
// - Does not set any warnings.
// Arguments:
// - <none>
// Return Value:
// - <none>
void CascadiaSettings::_UpdateActiveProfiles()
{
    ProfileGroup::RemoveHiddenProfiles(_profilesAndGroups);
    std::vector<Profile> profiles{};
    ProfileGroup::ExtractProfiles(_profilesAndGroups, profiles);

    // Ensure that we still have some profiles here. If we don't, then throw an
    // exception, so the app can use the defaults.
    const bool hasProfiles = !profiles.empty();
    if (!hasProfiles)
    {
        // Throw an exception. This is an invalid state, and we want the app to
        // be able to gracefully use the default settings.
        throw SettingsException(SettingsLoadErrors::AllProfilesHidden);
    }
}

// Method Description:
// - Ensures that every profile has a valid "color scheme" set. If any profile
//   has a colorScheme set to a value which is _not_ the name of an actual color
//   scheme, we'll set the color table of the profile to something reasonable.
// Arguments:
// - <none>
// Return Value:
// - <none>
// - Appends a SettingsLoadWarnings::UnknownColorScheme to our list of warnings if
//   we find any such duplicate.
void CascadiaSettings::_ValidateAllSchemesExist()
{
    bool foundInvalidScheme = false;

    std::vector<Profile> profiles{};
    ProfileGroup::ExtractProfiles(_profilesAndGroups, profiles);

    for (auto& profile : profiles)
    {
        auto schemeName = profile.GetSchemeName();
        if (schemeName.has_value())
        {
            const auto found = _globals.GetColorSchemes().find(schemeName.value());
            if (found == _globals.GetColorSchemes().end())
            {
                profile.SetColorScheme({ L"Campbell" });
                foundInvalidScheme = true;
            }
        }
    }

    if (foundInvalidScheme)
    {
        _warnings.Append(SettingsLoadWarnings::UnknownColorScheme);
    }
}

// Method Description:
// - Ensures that all specified images resources (icons and background images) are valid URIs.
//   This does not verify that the icon or background image files are encoded as an image.
// Arguments:
// - <none>
// Return Value:
// - <none>
// - Appends a SettingsLoadWarnings::InvalidBackgroundImage to our list of warnings if
//   we find any invalid background images.
// - Appends a SettingsLoadWarnings::InvalidIconImage to our list of warnings if
//   we find any invalid icon images.
void CascadiaSettings::_ValidateMediaResources()
{
    bool invalidBackground{ false };
    bool invalidIcon{ false };

    std::vector<Profile> profiles{};
    ProfileGroup::ExtractProfiles(_profilesAndGroups, profiles);

    for (auto& profile : profiles)
    {
        if (!profile.BackgroundImagePath().empty())
        {
            // Attempt to convert the path to a URI, the ctor will throw if it's invalid/unparseable.
            // This covers file paths on the machine, app data, URLs, and other resource paths.
            try
            {
                winrt::Windows::Foundation::Uri imagePath{ profile.ExpandedBackgroundImagePath() };
            }
            catch (...)
            {
                // reset background image path
                profile.BackgroundImagePath(L"");
                invalidBackground = true;
            }
        }

        if (!profile.Icon().empty())
        {
            const auto iconPath{ wil::ExpandEnvironmentStringsW<std::wstring>(profile.Icon().c_str()) };
            try
            {
                winrt::Windows::Foundation::Uri imagePath{ iconPath };
            }
            catch (...)
            {
                // Anything longer than 2 wchar_t's _isn't_ an emoji or symbol,
                // so treat it as an invalid path.
                if (iconPath.size() > 2)
                {
                    // reset icon path
                    profile.Icon(L"");
                    invalidIcon = true;
                }
            }
        }
    }

    if (invalidBackground)
    {
        _warnings.Append(SettingsLoadWarnings::InvalidBackgroundImage);
    }

    if (invalidIcon)
    {
        _warnings.Append(SettingsLoadWarnings::InvalidIcon);
    }
}

// Method Description:
// - Create a TerminalSettings object for the profile with a GUID matching the
//   provided GUID. If no profile matches this GUID, then this method will
//   throw.
// Arguments:
// - profileGuid: The GUID of a profile to use to create a settings object for.
// Return Value:
// - the GUID of the created profile, and a fully initialized TerminalSettings object
TerminalSettings CascadiaSettings::BuildSettings(GUID profileGuid) const
{
    const auto profile = FindProfile(profileGuid);
    //TODO: THROW_HR_IF_NULLOPT(E_INVALIDARG, profile);

    TerminalSettings result = profile.value().CreateTerminalSettings(_globals.GetColorSchemes());

    // Place our appropriate global settings into the Terminal Settings
    _globals.ApplyToSettings(result);

    return result;
}

// Method Description:
// - Helper to get the GUID of a profile, given an optional index and a possible
//   "profile" value to override that.
// - First, we'll try looking up the profile for the given index. This will
//   either get us the GUID of the Nth profile, or the GUID of the default
//   profile.
// - Then, if there was a Profile set in the NewTerminalArgs, we'll use that to
//   try and look the profile up by either GUID or name.
// Arguments:
// - index: if provided, the index in the list of profiles to get the GUID for.
//   If omitted, instead use the default profile's GUID
// - newTerminalArgs: An object that may contain a profile name or GUID to
//   actually use. If the Profile value is not a guid, we'll treat it as a name,
//   and attempt to look the profile up by name instead.
// Return Value:
// - the GUID of the profile corresponding to this combination of index and NewTerminalArgs
winrt::guid CascadiaSettings::GetProfileForArgs(const Model::NewTerminalArgs& newTerminalArgs) const
{
    GUID profileGuid = _globals.GetDefaultProfile();

        profileByName = _GetProfileGuidByName(newTerminalArgs.Profile());
    }

        // First, try and parse the "profile" argument as a GUID. If it's a
        // GUID, and the GUID of one of our profiles, then use that as the
        // profile GUID instead. If it's not, then try looking it up as a
        // name of a profile. If it's still not that, then just ignore it.
        if (!profileString.empty())
        {
            bool wasGuid = false;

            // Do a quick heuristic check - is the profile 38 chars long (the
            // length of a GUID string), and does it start with '{'? Because if
            // it doesn't, it's _definitely_ not a GUID.
            if (profileString.size() == 38 && profileString[0] == L'{')
            {
                try
                {
                    const auto newGUID = Utils::GuidFromString(profileString.c_str());
                    const auto profile = FindProfile(newGUID);
                    if (profile.has_value())
                    {
                        profileGuid = newGUID;
                        wasGuid = true;
                    }
                }
                CATCH_LOG();
            }

        // Here, we were unable to use the profile string as a GUID to
        // lookup a profile. Instead, try using the string to look the
        // Profile up by name.
        for (auto profile : _allProfiles)
        {
            if (profile.Name() == name)
            {
                return profile.Guid();
            }
        }
    }

    return std::nullopt;
}
catch (...)
{
    LOG_CAUGHT_EXCEPTION();
    return std::nullopt;
}

// Method Description:
// - If there were any warnings we generated while parsing the user's
//   keybindings, add them to the list of warnings here. If there were warnings
//   generated in this way, we'll add a AtLeastOneKeybindingWarning, which will
//   act as a header for the other warnings
// Arguments:
// - <none>
// Return Value:
// - <none>
void CascadiaSettings::_ValidateKeybindings()
{
    auto keybindingWarnings = _globals->KeybindingsWarnings();

    if (!keybindingWarnings.empty())
    {
        _warnings.Append(SettingsLoadWarnings::AtLeastOneKeybindingWarning);
        for (auto warning : keybindingWarnings)
        {
            _warnings.Append(warning);
        }
    }
}

// Method Description:
// - Ensures that every "setColorScheme" command has a valid "color scheme" set.
// Arguments:
// - <none>
// Return Value:
// - <none>
// - Appends a SettingsLoadWarnings::InvalidColorSchemeInCmd to our list of warnings if
//   we find any command with an invalid color scheme.
void CascadiaSettings::_ValidateColorSchemesInCommands()
{
    bool foundInvalidScheme{ false };
    for (const auto& nameAndCmd : _globals->Commands())
    {
        if (_HasInvalidColorScheme(nameAndCmd.Value()))
        {
            foundInvalidScheme = true;
            break;
        }
    }

    if (foundInvalidScheme)
    {
        _warnings.Append(SettingsLoadWarnings::InvalidColorSchemeInCmd);
    }
}

bool CascadiaSettings::_HasInvalidColorScheme(const Model::Command& command)
{
    bool invalid{ false };
    if (command.HasNestedCommands())
    {
        for (const auto& nested : command.NestedCommands())
        {
            if (_HasInvalidColorScheme(nested.Value()))
            {
                invalid = true;
                break;
            }
        }
    }
    else if (const auto& actionAndArgs = command.Action())
    {
        if (const auto& realArgs = actionAndArgs.Args().try_as<Model::SetColorSchemeArgs>())
        {
            auto cmdImpl{ winrt::get_self<Command>(command) };
            // no need to validate iterable commands on color schemes
            // they will be expanded to commands with a valid scheme name
            if (cmdImpl->IterateOn() != ExpandCommandType::ColorSchemes &&
                !_globals->ColorSchemes().HasKey(realArgs.SchemeName()))
            {
                invalid = true;
            }
        }
    }

    return invalid;
}

// Method Description:
// - Checks for the presence of the legacy "globals" key in the user's
//   settings.json. If this key is present, then they've probably got a pre-0.11
//   settings file that won't work as expected anymore. We should warn them
//   about that.
// Arguments:
// - <none>
// Return Value:
// - <none>
// - Appends a SettingsLoadWarnings::LegacyGlobalsProperty to our list of warnings if
//   we find any invalid background images.
void CascadiaSettings::_ValidateNoGlobalsKey()
{
    // use isMember here. If you use [], you're actually injecting "globals": null.
    if (_userSettings.isMember("globals"))
    {
        _warnings.Append(SettingsLoadWarnings::LegacyGlobalsProperty);
    }
}

// Method Description
// - Replaces known tokens DEFAULT_PROFILE, PRODUCT and VERSION in the settings template
//   with their expected values. DEFAULT_PROFILE is updated to match PowerShell Core's GUID
//   if such a profile is detected. If it isn't, it'll be set to Windows PowerShell's GUID.
// Arguments:
// - settingsTemplate: a settings template
// Return value:
// - The new settings string.
std::string CascadiaSettings::_ApplyFirstRunChangesToSettingsTemplate(std::string_view settingsTemplate) const
{
    // We're using replace_needle_in_haystack_inplace here, because it's more
    // efficient to iteratively modify a single string in-place than it is to
    // keep copying over the contents and modifying a copy (which
    // replace_needle_in_haystack would do).
    std::string finalSettings{ settingsTemplate };

    std::wstring defaultProfileGuid{ DEFAULT_WINDOWS_POWERSHELL_GUID };
    if (const auto psCoreProfileGuid{ _GetProfileGuidByName(hstring{ PowershellCoreProfileGenerator::GetPreferredPowershellProfileName() }) })
    {
        defaultProfileGuid = Utils::GuidToString(*psCoreProfileGuid);
    }

    til::replace_needle_in_haystack_inplace(finalSettings,
                                            "%DEFAULT_PROFILE%",
                                            til::u16u8(defaultProfileGuid));

    til::replace_needle_in_haystack_inplace(finalSettings,
                                            "%VERSION%",
                                            til::u16u8(ApplicationVersion()));
    til::replace_needle_in_haystack_inplace(finalSettings,
                                            "%PRODUCT%",
                                            til::u16u8(ApplicationDisplayName()));

    til::replace_needle_in_haystack_inplace(finalSettings,
                                            "%COMMAND_PROMPT_LOCALIZED_NAME%",
                                            RS_A(L"CommandPromptDisplayName"));

    return finalSettings;
}

// Method Description:
// - Lookup the color scheme for a given profile. If the profile doesn't exist,
//   or the scheme name listed in the profile doesn't correspond to a scheme,
//   this will return `nullptr`.
// Arguments:
// - profileGuid: the GUID of the profile to find the scheme for.
// Return Value:
// - a non-owning pointer to the scheme.
winrt::Microsoft::Terminal::Settings::Model::ColorScheme CascadiaSettings::GetColorSchemeForProfile(const winrt::guid profileGuid) const
{
    auto profile = FindProfile(profileGuid);
    if (!profile)
    {
        return nullptr;
    }
    const auto schemeName = profile.ColorSchemeName();
    return _globals->ColorSchemes().TryLookup(schemeName);
}

// Method Description:
// - updates all references to that color scheme with the new name
// Arguments:
// - oldName: the original name for the color scheme
// - newName: the new name for the color scheme
// Return Value:
// - <none>
void CascadiaSettings::UpdateColorSchemeReferences(const hstring oldName, const hstring newName)
{
    // update profiles.defaults, if necessary
    if (_userDefaultProfileSettings &&
        _userDefaultProfileSettings->HasColorSchemeName() &&
        _userDefaultProfileSettings->ColorSchemeName() == oldName)
    {
        _userDefaultProfileSettings->ColorSchemeName(newName);
    }

    // update all profiles referencing this color scheme
    for (const auto& profile : _allProfiles)
    {
        if (profile.HasColorSchemeName() && profile.ColorSchemeName() == oldName)
        {
            profile.ColorSchemeName(newName);
        }
    }
}

winrt::hstring CascadiaSettings::ApplicationDisplayName()
{
    try
    {
        const auto package{ winrt::Windows::ApplicationModel::Package::Current() };
        return package.DisplayName();
    }
    CATCH_LOG();

    return RS_(L"ApplicationDisplayNameUnpackaged");
}

winrt::hstring CascadiaSettings::ApplicationVersion()
{
    try
    {
        const auto package{ winrt::Windows::ApplicationModel::Package::Current() };
        const auto version{ package.Id().Version() };
        winrt::hstring formatted{ wil::str_printf<std::wstring>(L"%u.%u.%u.%u", version.Major, version.Minor, version.Build, version.Revision) };
        return formatted;
    }
    CATCH_LOG();

    // Try to get the version the old-fashioned way
    try
    {
        struct LocalizationInfo
        {
            WORD language, codepage;
        };
        // Use the current module instance handle for TerminalApp.dll, nullptr for WindowsTerminal.exe
        auto filename{ wil::GetModuleFileNameW<std::wstring>(wil::GetModuleInstanceHandle()) };
        auto size{ GetFileVersionInfoSizeExW(0, filename.c_str(), nullptr) };
        THROW_LAST_ERROR_IF(size == 0);
        auto versionBuffer{ std::make_unique<std::byte[]>(size) };
        THROW_IF_WIN32_BOOL_FALSE(GetFileVersionInfoExW(0, filename.c_str(), 0, size, versionBuffer.get()));

        // Get the list of Version localizations
        LocalizationInfo* pVarLocalization{ nullptr };
        UINT varLen{ 0 };
        THROW_IF_WIN32_BOOL_FALSE(VerQueryValueW(versionBuffer.get(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&pVarLocalization), &varLen));
        THROW_HR_IF(E_UNEXPECTED, varLen < sizeof(*pVarLocalization)); // there must be at least one translation

        // Get the product version from the localized version compartment
        // We're using String/ProductVersion here because our build pipeline puts more rich information in it (like the branch name)
        // than in the unlocalized numeric version fields.
        WCHAR* pProductVersion{ nullptr };
        UINT versionLen{ 0 };
        const auto localizedVersionName{ wil::str_printf<std::wstring>(L"\\StringFileInfo\\%04x%04x\\ProductVersion",
                                                                       pVarLocalization->language ? pVarLocalization->language : 0x0409, // well-known en-US LCID
                                                                       pVarLocalization->codepage) };
        THROW_IF_WIN32_BOOL_FALSE(VerQueryValueW(versionBuffer.get(), localizedVersionName.c_str(), reinterpret_cast<void**>(&pProductVersion), &versionLen));
        return { pProductVersion };
    }
    CATCH_LOG();

    return RS_(L"ApplicationVersionUnknown");
}
