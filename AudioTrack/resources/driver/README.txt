Audiotrack driver package
=========================

Put the SIGNED driver package here before you pack the mod:

    AudioTrackMic.inf
    audiotrackmic.sys
    audiotrackmic.cat

It comes from the AudioTrackDriver repository (CI artifact) after it went through
Microsoft attestation signing. An unsigned build only installs on a machine in
test-signing mode.

install.ps1 / uninstall.ps1 are started by the mod through a UAC prompt
(Audiotrack editor > "Install driver"). They change nothing except adding or
removing the "Audiotrack Out" / "Audiotrack Mic" audio devices.
