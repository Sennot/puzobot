# Installs the Audiotrack virtual audio driver ("Audiotrack Out" + "Audiotrack Mic").
# Run elevated (the mod starts it through a UAC prompt).
#
#   install.ps1 -DriverDir <folder with AudioTrackMic.inf, audiotrackmic.sys and audiotrackmic.cat>
#
# The driver must be signed (Microsoft attestation signing) unless the machine runs in
# test-signing mode. This script does not change any signing / boot settings.

param(
    [Parameter(Mandatory = $true)][string]$DriverDir,
    [string]$LogFile = ""
)

$ErrorActionPreference = 'Stop'
$hardwareId = 'Root\AudioTrackMic'

function Log([string]$text) {
    Write-Host $text
    if ($LogFile) { Add-Content -Path $LogFile -Value $text -Encoding UTF8 }
}

try {
    $inf = Join-Path $DriverDir 'AudioTrackMic.inf'
    if (-not (Test-Path $inf)) { throw "AudioTrackMic.inf was not found in $DriverDir" }
    if (-not (Test-Path (Join-Path $DriverDir 'audiotrackmic.sys'))) { throw "audiotrackmic.sys was not found in $DriverDir" }

    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class AtSetup {
    [StructLayout(LayoutKind.Sequential)]
    public struct SP_DEVINFO_DATA {
        public int cbSize;
        public Guid ClassGuid;
        public int DevInst;
        public IntPtr Reserved;
    }

    [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern bool SetupDiGetINFClass(string inf, out Guid classGuid, StringBuilder className, int size, out int required);

    [DllImport("setupapi.dll", SetLastError = true)]
    public static extern IntPtr SetupDiCreateDeviceInfoList(ref Guid classGuid, IntPtr hwnd);

    [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern bool SetupDiCreateDeviceInfo(IntPtr set, string name, ref Guid classGuid, string description, IntPtr hwnd, int flags, ref SP_DEVINFO_DATA data);

    [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern bool SetupDiSetDeviceRegistryProperty(IntPtr set, ref SP_DEVINFO_DATA data, int property, byte[] buffer, int size);

    [DllImport("setupapi.dll", SetLastError = true)]
    public static extern bool SetupDiCallClassInstaller(int function, IntPtr set, ref SP_DEVINFO_DATA data);

    [DllImport("setupapi.dll", SetLastError = true)]
    public static extern bool SetupDiDestroyDeviceInfoList(IntPtr set);

    [DllImport("newdev.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern bool UpdateDriverForPlugAndPlayDevices(IntPtr hwnd, string hardwareId, string inf, int flags, out bool reboot);

    public static void CreateRootDevice(string inf, string hardwareId) {
        Guid classGuid;
        var className = new StringBuilder(256);
        int required;
        if (!SetupDiGetINFClass(inf, out classGuid, className, 256, out required))
            throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error(), "SetupDiGetINFClass");

        IntPtr set = SetupDiCreateDeviceInfoList(ref classGuid, IntPtr.Zero);
        if (set == new IntPtr(-1))
            throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error(), "SetupDiCreateDeviceInfoList");
        try {
            var data = new SP_DEVINFO_DATA();
            data.cbSize = Marshal.SizeOf(typeof(SP_DEVINFO_DATA));
            // DICD_GENERATE_ID = 1
            if (!SetupDiCreateDeviceInfo(set, className.ToString(), ref classGuid, null, IntPtr.Zero, 1, ref data))
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error(), "SetupDiCreateDeviceInfo");

            // SPDRP_HARDWAREID = 1, REG_MULTI_SZ: the id, a terminating NUL and an empty string.
            byte[] id = Encoding.Unicode.GetBytes(hardwareId + "\0\0");
            if (!SetupDiSetDeviceRegistryProperty(set, ref data, 1, id, id.Length))
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error(), "SetupDiSetDeviceRegistryProperty");

            // DIF_REGISTERDEVICE = 0x19
            if (!SetupDiCallClassInstaller(0x19, set, ref data))
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error(), "DIF_REGISTERDEVICE");
        } finally {
            SetupDiDestroyDeviceInfoList(set);
        }
    }

    public static bool InstallDriver(string inf, string hardwareId) {
        bool reboot;
        // INSTALLFLAG_FORCE = 1
        if (!UpdateDriverForPlugAndPlayDevices(IntPtr.Zero, hardwareId, inf, 1, out reboot))
            throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error(), "UpdateDriverForPlugAndPlayDevices");
        return reboot;
    }
}
'@

    $existing = Get-PnpDevice -Class MEDIA -ErrorAction SilentlyContinue |
        Where-Object { $_.HardwareID -contains $hardwareId }
    if ($existing) {
        Log "The Audiotrack device already exists, updating its driver."
    } else {
        Log "Creating the Audiotrack device..."
        [AtSetup]::CreateRootDevice($inf, $hardwareId)
    }

    Log "Installing the driver from $DriverDir ..."
    $reboot = [AtSetup]::InstallDriver($inf, $hardwareId)
    if ($reboot) { Log "A restart is required to finish the installation." }
    Log "OK: Audiotrack driver installed."
    exit 0
}
catch {
    Log "ERROR: $($_.Exception.Message)"
    if ($_.Exception -is [System.ComponentModel.Win32Exception]) {
        Log ("Win32 error 0x{0:X8}. 0xE0000247 / 0x800B0109 / 0x800B0100 usually mean the driver is not signed for this machine." -f $_.Exception.NativeErrorCode)
    }
    exit 1
}
