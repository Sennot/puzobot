#include "installer.hpp"

#include "mixer.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/Notification.hpp>

#include <Windows.h>
#include <shellapi.h>

#include <thread>

using namespace geode::prelude;

namespace at {

std::filesystem::path driverDir() {
    return Mod::get()->getResourcesDir() / "driver";
}

bool driverPackageBundled() {
    auto const dir = driverDir();
    std::error_code ec;
    return std::filesystem::exists(dir / "AudioTrackMic.inf", ec) &&
           std::filesystem::exists(dir / "audiotrackmic.sys", ec) &&
           std::filesystem::exists(dir / "audiotrackmic.cat", ec);
}

bool runDriverScript(bool install, std::string& error) {
    auto const dir = driverDir();
    auto const script = dir / (install ? "install.ps1" : "uninstall.ps1");
    std::error_code ec;
    if (!std::filesystem::exists(script, ec)) {
        error = "the driver scripts are missing from the mod";
        return false;
    }
    if (install && !driverPackageBundled()) {
        error = "this build of the mod does not contain the signed driver package";
        return false;
    }

    auto const log = Mod::get()->getSaveDir() / "driver-install.log";
    std::filesystem::create_directories(log.parent_path(), ec);
    std::filesystem::remove(log, ec);

    std::wstring parameters = L"-NoProfile -ExecutionPolicy Bypass -File \"" + script.wstring() + L"\"";
    if (install) parameters += L" -DriverDir \"" + dir.wstring() + L"\"";
    parameters += L" -LogFile \"" + log.wstring() + L"\"";

    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"runas"; // UAC prompt
    info.lpFile = L"powershell.exe";
    info.lpParameters = parameters.c_str();
    info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info) || !info.hProcess) {
        error = GetLastError() == ERROR_CANCELLED ? "administrator permission was declined" : "cannot start PowerShell";
        return false;
    }

    // Report the result when the script is done.
    HANDLE process = info.hProcess;
    std::thread([process, install, log] {
        WaitForSingleObject(process, INFINITE);
        DWORD exitCode = 1;
        GetExitCodeProcess(process, &exitCode);
        CloseHandle(process);

        std::string detail;
        if (auto text = utils::file::readString(log)) {
            auto const& all = text.unwrap();
            auto const end = all.find_last_not_of("\r\n");
            if (end != std::string::npos) {
                auto const newline = all.find_last_of('\n', end);
                auto const from = newline == std::string::npos ? std::size_t{0} : newline + 1;
                detail = all.substr(from, end - from + 1);
            }
        }
        queueInMainThread([exitCode, install, detail] {
            bool const ok = exitCode == 0;
            auto message = std::string(install ? "Audiotrack driver: " : "Audiotrack driver removal: ") +
                           (ok ? "done" : "failed") + (detail.empty() ? "" : " - " + detail);
            Notification::create(message, ok ? NotificationIcon::Success : NotificationIcon::Error, 6.f)->show();
            if (ok && install) Mixer::get().start();
        });
    }).detach();
    return true;
}

} // namespace at
