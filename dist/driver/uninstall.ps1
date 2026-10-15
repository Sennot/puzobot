# Removes the Audiotrack virtual audio driver. Run elevated.

param([string]$LogFile = "")

$ErrorActionPreference = 'Stop'
$hardwareId = 'Root\AudioTrackMic'

function Log([string]$text) {
    Write-Host $text
    if ($LogFile) { Add-Content -Path $LogFile -Value $text -Encoding UTF8 }
}

try {
    $devices = Get-PnpDevice -Class MEDIA -ErrorAction SilentlyContinue |
        Where-Object { $_.HardwareID -contains $hardwareId }
    foreach ($device in $devices) {
        Log "Removing device $($device.InstanceId)"
        & pnputil.exe /remove-device $device.InstanceId | ForEach-Object { Log $_ }
    }

    # Find the driver package in the driver store by its original inf name.
    $listing = & pnputil.exe /enum-drivers
    $published = $null
    foreach ($line in $listing) {
        if ($line -match 'oem\d+\.inf') { $published = $Matches[0] }
        if ($line -match 'audiotrackmic\.inf' -and $published) {
            Log "Deleting driver package $published"
            & pnputil.exe /delete-driver $published /uninstall /force | ForEach-Object { Log $_ }
            $published = $null
        }
    }
    Log "OK: Audiotrack driver removed."
    exit 0
}
catch {
    Log "ERROR: $($_.Exception.Message)"
    exit 1
}
