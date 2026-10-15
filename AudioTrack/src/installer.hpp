#pragma once

// Installs / removes the Audiotrack audio driver by starting the bundled
// PowerShell scripts (resources/driver) elevated through a UAC prompt.

#include <filesystem>
#include <string>

namespace at {

// Folder with install.ps1, uninstall.ps1 and the driver package.
std::filesystem::path driverDir();
// True when AudioTrackMic.inf, audiotrackmic.sys and audiotrackmic.cat are bundled with the mod.
bool driverPackageBundled();

// Starts the script elevated. Returns false (with `error`) when it could not
// be started, e.g. the user declined the UAC prompt. The result of the
// installation is reported later through a notification.
bool runDriverScript(bool install, std::string& error);

} // namespace at
