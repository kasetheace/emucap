#!/usr/bin/env python3
"""Capture reproducible GBA input/animation evidence via SkyEmu's HTTP API.

Run on a trusted desktop emulator with the HTTP Control Server enabled.
Does not upload the ROM or transmit it over the network.
"""
import argparse
import hashlib
import json
import time
import urllib.parse
import urllib.request
from pathlib import Path

WATCH = {
    "held": 0x03003100,
    "previous": 0x03003F14,
    "pressed": 0x03003F6C,
    "released": 0x03003B5C,
    "pressed_copy": 0x03003128,
}
BUTTONS = {"A", "B", "L", "R", "Up", "Down", "Left", "Right", "Start", "Select"}


def get(base, endpoint, params=None):
    url = base.rstrip("/") + "/" + endpoint
    if params:
        url += "?" + urllib.parse.urlencode(params, doseq=True)
    with urllib.request.urlopen(url, timeout=20) as response:
        return response.read()


def memory_snapshot(base):
    params = [("addr", f"{address + i:08x}") for address in WATCH.values() for i in range(2)]
    raw = bytes.fromhex(get(base, "read_byte", params).decode().strip())
    if len(raw) != len(WATCH) * 2:
        raise ValueError("Unexpected emulator memory response")
    return {name: int.from_bytes(raw[index * 2:index * 2 + 2], "little")
            for index, name in enumerate(WATCH)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default="http://127.0.0.1:8080")
    parser.add_argument("--out", type=Path, default=Path("captures"))
    parser.add_argument("--sequence", type=Path, required=True,
                        help='JSON array: [{"buttons":["B"],"frames":8,"label":"attack"}, ...]')
    parser.add_argument("--rom-sha256", default="", help="Optional expected ROM hash for metadata")
    args = parser.parse_args()
    steps = json.loads(args.sequence.read_text())
    if not isinstance(steps, list) or not steps:
        parser.error("Sequence must be a nonempty JSON array")
    if not args.url.startswith("http://127.0.0.1:") and not args.url.startswith("http://localhost:"):
        parser.error("Only local SkyEmu HTTP servers are supported")
    status = json.loads(get(args.url, "status"))
    if not status.get("rom-loaded"):
        parser.error("Load a GBA ROM in SkyEmu first")
    args.out.mkdir(parents=True, exist_ok=True)
    manifest = {"schema": 1, "source": "SkyEmu HTTP Control Server",
                "rom_sha256_expected": args.rom_sha256 or None, "captures": []}
    last = set()
    try:
        for index, step in enumerate(steps):
            buttons = set(step["buttons"])
            if not buttons <= BUTTONS:
                raise ValueError(f"Invalid button in step {index}")
            frames = int(step.get("frames", 1))
            if not 1 <= frames <= 600:
                raise ValueError("Frames must be 1..600")
            changed = sorted(last | buttons)
            if changed:
                get(args.url, "input", [(button, int(button in buttons)) for button in changed])
            last = buttons
            before = memory_snapshot(args.url)
            get(args.url, "step", {"frames": frames})
            after = memory_snapshot(args.url)
            image = get(args.url, "screen")
            if not image.startswith(b"\x89PNG\r\n\x1a\n"):
                raise ValueError("SkyEmu did not return PNG screenshot")
            name = f"{index:04d}.png"
            (args.out / name).write_bytes(image)
            manifest["captures"].append({
                "index": index, "label": str(step.get("label", "")),
                "buttons": sorted(buttons), "frames": frames,
                "before": before, "after": after,
                "screen": name, "screen_sha256": hashlib.sha256(image).hexdigest(),
            })
            (args.out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
            print(index, step.get("label", ""), sorted(buttons), after)
    finally:
        if last:
            get(args.url, "input", [(button, 0) for button in sorted(last)])


if __name__ == "__main__":
    main()
