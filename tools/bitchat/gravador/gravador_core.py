"""The flasher's logic, without its window: which file a device needs, fetching and checking it, and the
esptool steps. The window (gravador.py) and the tests (test_gravador.py) both use it.

A release names its files after the build environment: <env>.bin (the application, written at 0x10000,
keeping contacts, channels and identity), <env>-merged.bin (everything, written at 0x0 after erasing),
<env>.uf2 (nRF52 boards, copied to the board's drive) and one ZIP per country with the same files built so
that a device written from scratch starts on that country's radio settings.
"""

import hashlib
import io
import json
import os
import re
import ssl
import sys
import urllib.request
import zipfile
from dataclasses import dataclass
from pathlib import Path

REPOSITORY = "EduChacon/Meshcore-Bitchat"
APP_OFFSET = 0x10000
SUFFIX = "_companion_radio_ble_bitchat"
# Chip identifiers in an ESP image header (esptool's IMAGE_CHIP_ID).
CHIPS = {0: "ESP32", 2: "ESP32-S2", 5: "ESP32-C3", 9: "ESP32-S3", 12: "ESP32-C2", 13: "ESP32-C6", 16: "ESP32-H2"}


def resource(name):
    """A file shipped with the flasher (beside this module, or inside the packaged app)."""
    base = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parent))
    return base / name


def load_build():
    """The release this flasher belongs to and its devices, written when the flasher was built."""
    path = resource("build.json")
    if not path.exists():
        return {"version": "", "devices": []}
    return json.loads(path.read_text(encoding="utf-8"))


@dataclass(frozen=True)
class Device:
    env: str
    kind: str  # "esp32" or "nrf52"

    @property
    def label(self):
        name = self.env[: -len(SUFFIX)] if self.env.endswith(SUFFIX) else self.env
        return name.replace("_", " ")


def devices(build):
    return sorted((Device(d["env"], d["kind"]) for d in build.get("devices", [])), key=lambda d: d.label.lower())


def release_url(version, name):
    return f"https://github.com/{REPOSITORY}/releases/download/{version}/{name}"


def file_name(device, full):
    if device.kind == "nrf52":
        return f"{device.env}.uf2"
    return f"{device.env}-merged.bin" if full else f"{device.env}.bin"


def country_zip(version, title):
    return f"MeshCore-Bitchat-{version}-{title.replace(' ', '-')}.zip"


def parse_sums(text):
    """SHA256SUMS lines ("<hex>  <name>") as a dict name -> hex."""
    sums = {}
    for line in text.splitlines():
        match = re.match(r"^([0-9a-fA-F]{64})\s+\*?(.+)$", line.strip())
        if match:
            sums[match.group(2).strip()] = match.group(1).lower()
    return sums


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def _context():
    # The packaged app has no system certificates on every platform: certifi's bundle when present.
    try:
        import certifi

        return ssl.create_default_context(cafile=certifi.where())
    except ImportError:
        return ssl.create_default_context()


def fetch(url, progress=None, opener=None):
    """The bytes at `url`. `progress(done, total)` is called as they arrive."""
    request = urllib.request.Request(url, headers={"User-Agent": "gravador-bitchat"})
    open_url = opener or (lambda r: urllib.request.urlopen(r, timeout=60, context=_context()))
    with open_url(request) as response:
        total = int(response.headers.get("Content-Length") or 0)
        chunks, done = [], 0
        while True:
            chunk = response.read(64 * 1024)
            if not chunk:
                break
            chunks.append(chunk)
            done += len(chunk)
            if progress:
                progress(done, total)
    return b"".join(chunks)


class CheckError(Exception):
    pass


def checked(name, data, sums):
    """`data` if its SHA-256 is the one SHA256SUMS lists for `name`."""
    expected = sums.get(name)
    if not expected:
        raise CheckError(f"{name}: not in SHA256SUMS")
    if sha256(data) != expected:
        raise CheckError(f"{name}: SHA-256 does not match")
    return data


def from_country_zip(archive_bytes, name):
    """One file out of a country ZIP, checked against the SHA256SUMS inside it."""
    with zipfile.ZipFile(io.BytesIO(archive_bytes)) as archive:
        members = {Path(m).name: m for m in archive.namelist()}
        if name not in members or "SHA256SUMS" not in members:
            raise CheckError(f"{name}: not in the country package")
        inner = parse_sums(archive.read(members["SHA256SUMS"]).decode("utf-8", "replace"))
        return checked(name, archive.read(members[name]), inner)


def image_chip(data, full):
    """The chip an ESP firmware file is for, from its image header; None if it is not an ESP image.

    The application image starts with its header. A complete (merged) image starts with the bootloader's,
    at 0x0 on most chips and at 0x1000 on the ESP32 and ESP32-S2 (the space before it is padding)."""
    for offset in (0, 0x1000) if full else (0,):
        header = data[offset : offset + 24]
        if len(header) == 24 and header[0] == 0xE9:
            chip = header[12] | header[13] << 8
            # The ESP32's original header has no chip field: zero there means ESP32.
            return CHIPS.get(chip, f"chip {chip}")
    return None


def is_merged(data):
    """Whether a firmware file is a complete image: it carries the partition table at 0x8000."""
    return len(data) > 0x8002 and data[0x8000:0x8002] == b"\xaa\x50"


def check_file(data, full):
    """The chip a file is for, or CheckError if it is not the kind of file the chosen action writes."""
    chip = image_chip(data, full)
    if not chip:
        raise CheckError("not an ESP32 firmware file")
    if full != is_merged(data):
        raise CheckError("merged file needed" if full else "application file needed")
    return chip


def same_chip(file_chip, detected):
    """Whether the board esptool found is the chip the file was built for (ESP32-S3 vs "ESP32-S3")."""
    return bool(file_chip) and detected.upper().startswith(file_chip.upper()) and not (
        file_chip.upper() == "ESP32" and detected.upper() != "ESP32"
    )


def steps(port, path, full, backup=None, baud=460800):
    """The esptool command lines, in order, for one flashing."""
    base = ["--port", port, "--baud", str(baud)]
    commands = []
    if backup:
        commands.append(base + ["read_flash", "0", "ALL", str(backup)])
    if full:
        commands.append(base + ["erase_flash"])
        commands.append(base + ["write_flash", "0x0", str(path)])
    else:
        commands.append(base + ["write_flash", hex(APP_OFFSET), str(path)])
    return commands


def serial_ports():
    """USB serial ports (all serial ports if none is USB). On a Mac only the /dev/cu.* side of each port."""
    try:
        from serial.tools import list_ports
    except ImportError:
        return []
    found = []
    for port in list_ports.comports():
        name = port.device
        if sys.platform == "darwin" and name.startswith("/dev/tty."):
            continue
        if "Bluetooth" in name or "debug-console" in name:
            continue
        usb = bool(port.vid) or "usb" in name.lower()
        found.append((0 if usb else 1, name, port.description or ""))
    if any(rank == 0 for rank, _, _ in found):
        found = [entry for entry in found if entry[0] == 0]
    return [(name, description) for _, name, description in sorted(found)]


def backup_folder():
    home = Path.home()
    documents = home / "Documents"
    folder = (documents if documents.is_dir() else home) / "Gravador-Bitchat"
    folder.mkdir(parents=True, exist_ok=True)
    return folder


def downloads_folder():
    folder = Path.home() / "Downloads"
    return folder if folder.is_dir() else Path.home()


def language():
    """ "pt" for a Portuguese system, else "en"."""
    for variable in ("LC_ALL", "LC_MESSAGES", "LANG"):
        value = os.environ.get(variable, "")
        if value:
            return "pt" if value.lower().startswith("pt") else "en"
    try:
        import locale

        code = (locale.getlocale()[0] or "").lower()
        if not code and sys.platform == "win32":
            import ctypes

            code = locale.windows_locale.get(ctypes.windll.kernel32.GetUserDefaultUILanguage(), "").lower()
        return "pt" if code.startswith("pt") else "en"
    except Exception:
        return "en"
