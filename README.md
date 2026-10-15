# xdBot Audiotrack

Two parts:

- `AudioTrack/` - Geode mod (depends on xdBot 2.7.7, Windows). Adds an **Audiotrack** row to the xdBot menu:
  waveform editor that aligns a click `.wav` to the macro's first input, plays it with the macro and can
  send it into the microphone cable.
- `AudioTrackDriver/` - virtual audio driver ("Audiotrack Out" / "Audiotrack Mic"), derived from the
  Microsoft SysVAD sample (MS-PL, see `AudioTrackDriver/LICENSE.sysvad-MS-PL.txt`). Work in progress,
  unsigned: it only installs on a test-signing machine until it goes through Microsoft attestation signing.

CI builds both on GitHub Actions (`.github/workflows`).
