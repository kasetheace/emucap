# EmuCap: Advanced Adventure capture workflow

This fork contains a **capture harness** for documenting original GBA behavior and an **Android APK build workflow**.

## Android

Open the GitHub Actions page for **Build EmuCap Android APK**. If Actions is enabled, the `EmuCap-Android-APK` artifact contains a release APK built from the fork's existing SkyEmu Android app. This build is separately packaged as `com.kasetheace.emucap` so it can coexist with stock SkyEmu. It uses the existing repository's publicly committed testing key, **not a private production signing key**.

Install the APK, choose your legally obtained original `.gba` ROM, and use the emulator's built-in screenshots/save states. Android's system screen recorder can also capture gameplay. The capture harness described below **does not run inside the Android APK**; the HTTP Control Server is a separate desktop automation interface. In particular, this build does not automatically extract sprite atlases or stream phone RAM dumps.

## Reproducible button, RAM and screenshot capture (desktop)

SkyEmu already supports a documented HTTP Control Server; enable it in advanced settings and set its listening port to `8080`. Keep the server accessible only on localhost. Load the original game, navigate to gameplay and pause it. Then run:

```bash
python3 tools/advanced_adventure_capture.py \
  --url http://127.0.0.1:8080 \
  --sequence tools/advanced_adventure_capture_sequence.json \
  --out captures/advanced_adventure
```

Each test step captures a PNG frame, input buttons, frame count, and 16-bit reads of the following RAM addresses: current keys `0x03003100`, previous `0x03003F14`, newly pressed `0x03003F6C`, newly released `0x03003B5C`, and pressed copy `0x03003128`.

The resulting `manifest.json` includes screenshots' SHA-256 hashes. Input is released in a `finally` block if the test completes or fails. You can edit the JSON sequence to capture other characters, directional attacks and combinations.

**Important:** these RAM addresses were recovered from the specific USA v0 revision of Dragon Ball: Advanced Adventure. They may not apply to another game or ROM revision. The tool does not discover function entry points, trace CPU execution, or extract sprite assets automatically; those are separate reverse-engineering tasks.

Do not commit ROMs, save files, proprietary graphics, or captured gameplay media to a public repository without considering distribution rights. Keep captures local or share privately.
