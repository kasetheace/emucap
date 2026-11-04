# EmuCap Trace Lab (Android)

EmuCap is a fork of SkyEmu with **opt-in GBA action capture** and a local analysis screen. The Android APK is built with the existing package ID and signing identity. No ROM or save file is embedded in the APK.

## Quick procedure for Advanced Adventure

1. Load the game and reach a reproducible standing/idle scene. **Save the game state** so each experiment can begin at approximately the same point.
2. Tap **Trace** in the emulator overlay. Stand still for a few seconds, then tap **Save trace**. This is your idle baseline.
3. Restore the same game state. Tap **Trace**, perform one action (e.g., Goku's B attack), and tap **Save trace**.
4. Tap **Analyze**. In the offline Trace Lab, use **Saved action** and **Saved baseline** to load your two recordings from app-private storage.
5. Look at **Candidate ROM instructions**, **RAM changes**, and the **Button timeline**. Copy candidate addresses and inspect them in a GBA/ARM decompiler. Repeat with other inputs to validate correlations.

The **Analyze** screen stays accessible after you restart EmuCap. The recording screen's **Share** action exports the raw JSON through Android's share sheet.

## What is captured

- GBA KEYINPUT transitions (10 buttons; active low: a clear bit means the button is pressed).
- Canonical ROM-bus execution addresses, original opcodes, Thumb/ARM state, hit counts, and first-seen frame.
- A bounded ordered instruction sample immediately after each detected input change.
- Frame-to-frame changes in 256 KiB EWRAM and 32 KiB IWRAM, sampled with caps.
- A one-time, non-cryptographic FNV-1a 64-bit fingerprint of the running ROM (for checking that action and baseline came from the same game/revision).

The recorder **does not** record a complete execution trace or exact memory write events, infer original function names, reconstruct the game's source code, or export ROM bytes. Some game instructions may execute from RAM and are not included in the ROM coverage ranking.

## File format and limits

Files are named `gba-trace-<timestamp>.json`, stored under EmuCap's external app-private `emucap-traces` directory, and explicitly exported only when shared. Trace Lab does not contact a server.

| Capture component | Limit |
| --- | ---: |
| Recorded frames | 1,200 (roughly 20 seconds at full speed) |
| Distinct ROM PCs | 65,536 hash-table entries; overflow is reported |
| Ordered instruction samples | 32,768 |
| Sampled instructions per input event | 4,096 |
| RAM differences | 16,384 total; at most 32 changes per RAM region per frame |
| Input transitions | 256 |

The JSON file reports any omitted records due to buffer limits. Keep experiments short to avoid overflows. A successful compile is not a substitute for testing on a physical Android device.

## Understanding the results

- The **extra/frame** column compares action hit counts per recorded frame against idle hit counts per recorded frame. This is a screening metric, not proof of relevance.
- If ROM fingerprints differ, don't compare code addresses. The same PC can represent different instructions in different ROM versions.
- The first addresses in a hot loop may be animation drawing, interrupts, input polling, or sound updates rather than combat code. Inspect call sites and repeated tests before naming a function.
- The program counter exported here is SkyEmu's actual executed instruction address. For a standard GBA ROM loaded at `0x08000000`, file offset is `PC - 0x08000000` after accounting for ROM mirrors. For Thumb code, ensure Ghidra interprets the instruction as Thumb.
- Memory changes capture values **between frames**. If a variable changes twice in one frame and returns to its old value, it will not appear. A hardcoded 32-byte per-region cap means not every changed RAM address is retained.

## Source layout

- `src/emucap_trace.c` and `src/emucap_trace.h`: recorder and JSON export
- `src/gba.h`: key, instruction, and frame integration points
- `tools/android_project/app/src/main/java/com/sky/SkyEmu/EnhancedNativeActivity.java`: on-screen capture controls
- `tools/android_project/app/src/main/java/com/sky/SkyEmu/TraceViewerActivity.java`: offline saved-trace browser
- `tools/android_project/app/src/main/assets/emucap_trace_viewer.html`: self-contained HTML trace comparator (can also be opened directly in a desktop browser)
- `tools/advanced_adventure_capture.py`: existing desktop SkyEmu HTTP capture automation; separate from APK tracing

This feature is added to the **user's EmuCap fork**. It is not an upstream SkyEmu contribution.
