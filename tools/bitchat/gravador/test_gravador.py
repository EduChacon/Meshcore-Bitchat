"""Tests of the flasher's logic, without a radio or a window: python3 tools/bitchat/gravador/test_gravador.py.
With --release-dir, also against real files of a release (a folder with SHA256SUMS and some of its files)."""

import argparse
import hashlib
import io
import sys
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gravador_core as core  # noqa: E402
import make_build_json  # noqa: E402

RELEASE_DIR = None


def header(chip):
    """A minimal ESP image header for `chip` (its IMAGE_CHIP_ID)."""
    data = bytearray(24)
    data[0] = 0xE9
    data[12] = chip & 0xFF
    data[13] = chip >> 8
    return bytes(data)


def merged(chip, at=0):
    data = bytearray(b"\xff" * 0x9000)
    data[at : at + 24] = header(chip)
    data[0x8000:0x8002] = b"\xaa\x50"
    return bytes(data)


class Files(unittest.TestCase):
    def test_names(self):
        esp = core.Device("Heltec_v3_companion_radio_ble_bitchat", "esp32")
        nrf = core.Device("t1000e_companion_radio_ble_bitchat", "nrf52")
        self.assertEqual(core.file_name(esp, False), "Heltec_v3_companion_radio_ble_bitchat.bin")
        self.assertEqual(core.file_name(esp, True), "Heltec_v3_companion_radio_ble_bitchat-merged.bin")
        self.assertEqual(core.file_name(nrf, True), "t1000e_companion_radio_ble_bitchat.uf2")
        self.assertEqual(esp.label, "Heltec v3")
        self.assertEqual(core.country_zip("v1.18.0-bitchat1.2", "Europa e Reino Unido"),
                         "MeshCore-Bitchat-v1.18.0-bitchat1.2-Europa-e-Reino-Unido.zip")
        self.assertEqual(core.release_url("v1", "a.bin"),
                         "https://github.com/EduChacon/Meshcore-Bitchat/releases/download/v1/a.bin")

    def test_sums(self):
        data = b"firmware"
        sums = core.parse_sums(f"{hashlib.sha256(data).hexdigest()}  a.bin\nnot a line\n")
        self.assertEqual(core.checked("a.bin", data, sums), data)
        with self.assertRaises(core.CheckError):
            core.checked("a.bin", data + b"!", sums)
        with self.assertRaises(core.CheckError):
            core.checked("b.bin", data, sums)

    def test_country_zip(self):
        data = b"merged image"
        buffer = io.BytesIO()
        with zipfile.ZipFile(buffer, "w") as archive:
            archive.writestr("Brasil/LEIA-ME.txt", "nota")
            archive.writestr("Brasil/SHA256SUMS", f"{hashlib.sha256(data).hexdigest()}  x-merged.bin\n")
            archive.writestr("Brasil/x-merged.bin", data)
            archive.writestr("Brasil/y-merged.bin", b"not listed")
        self.assertEqual(core.from_country_zip(buffer.getvalue(), "x-merged.bin"), data)
        with self.assertRaises(core.CheckError):
            core.from_country_zip(buffer.getvalue(), "y-merged.bin")
        with self.assertRaises(core.CheckError):
            core.from_country_zip(buffer.getvalue(), "z-merged.bin")


class Images(unittest.TestCase):
    def test_chip_and_kind(self):
        app = header(9) + b"\x00" * 100
        self.assertEqual(core.check_file(app, False), "ESP32-S3")
        self.assertEqual(core.check_file(merged(9), True), "ESP32-S3")
        # the ESP32's bootloader sits at 0x1000 in a complete image
        self.assertEqual(core.check_file(merged(0, 0x1000), True), "ESP32")
        with self.assertRaises(core.CheckError):
            core.check_file(app, True)  # an application image is not a complete one
        with self.assertRaises(core.CheckError):
            core.check_file(merged(9), False)  # nor the reverse
        with self.assertRaises(core.CheckError):
            core.check_file(b"\x00" * 64, False)

    def test_same_chip(self):
        self.assertTrue(core.same_chip("ESP32-S3", "ESP32-S3"))
        self.assertTrue(core.same_chip("ESP32", "ESP32"))
        self.assertFalse(core.same_chip("ESP32", "ESP32-S3"))
        self.assertFalse(core.same_chip("ESP32-S3", "ESP32-C3"))
        self.assertFalse(core.same_chip(None, "ESP32"))


class Steps(unittest.TestCase):
    def test_update_keeps_data(self):
        self.assertEqual(core.steps("COM3", "a.bin", False),
                         [["--port", "COM3", "--baud", "460800", "write_flash", "0x10000", "a.bin"]])

    def test_full_erases_and_backs_up_first(self):
        commands = core.steps("/dev/cu.usbmodem1", "m.bin", True, "copy.bin")
        self.assertEqual([c[4] for c in commands], ["read_flash", "erase_flash", "write_flash"])
        self.assertEqual(commands[0][-3:], ["0", "ALL", "copy.bin"])
        self.assertEqual(commands[2][-2:], ["0x0", "m.bin"])


class Build(unittest.TestCase):
    def test_build_json(self):
        data = make_build_json.build()
        self.assertTrue(data["version"].startswith("v1.18.0-bitchat"))
        kinds = {d["env"]: d["kind"] for d in data["devices"]}
        self.assertEqual(kinds["Heltec_v3_companion_radio_ble_bitchat"], "esp32")
        self.assertEqual(kinds["t1000e_companion_radio_ble_bitchat"], "nrf52")
        self.assertEqual(kinds["LilyGo_TDeck_companion_radio_ble_bitchat"], "esp32")


class Flow(unittest.TestCase):
    """The worker from file to esptool, with a fake release, a fake radio and esptool recorded."""

    def run_flash(self, full, board="ESP32-S3", country=""):
        import tempfile
        import types

        import gravador

        app, complete = header(9) + b"\x00" * 64, merged(9)
        env = "Heltec_v3_companion_radio_ble_bitchat"
        files = {f"{env}.bin": app, f"{env}-merged.bin": complete}
        archive = io.BytesIO()
        with zipfile.ZipFile(archive, "w") as z:
            z.writestr("Brasil/SHA256SUMS", f"{hashlib.sha256(complete).hexdigest()}  {env}-merged.bin\n")
            z.writestr(f"Brasil/{env}-merged.bin", complete)
        files[core.country_zip("v9", "Brasil")] = archive.getvalue()
        files["SHA256SUMS"] = "".join(f"{hashlib.sha256(d).hexdigest()}  {n}\n" for n, d in files.items()).encode()
        commands, news = [], []
        fake_esptool = types.SimpleNamespace(main=lambda c: commands.append(c))
        with tempfile.TemporaryDirectory() as home:
            saved = (core.fetch, gravador.detect, core.backup_folder, sys.modules.get("esptool"))
            core.fetch = lambda url, progress=None: files[url.rsplit("/", 1)[1]]
            gravador.detect = lambda port: board
            core.backup_folder = lambda: Path(home)
            sys.modules["esptool"] = fake_esptool
            try:
                job = {"device": core.Device(env, "esp32"), "version": "v9", "full": full, "country": country,
                       "port": "COM3", "backup": True, "local": ""}
                gravador.flash(job, lambda kind, value: news.append((kind, value)), gravador.TEXT["pt"])
            finally:
                core.fetch, gravador.detect, core.backup_folder, previous = saved
                if previous is None:
                    sys.modules.pop("esptool", None)
                else:
                    sys.modules["esptool"] = previous
        return commands, news

    def test_update(self):
        commands, news = self.run_flash(False)
        self.assertEqual([c[4] for c in commands], ["read_flash", "write_flash"])
        self.assertEqual(commands[1][5], "0x10000")
        self.assertIn(("done", None), news)

    def test_full_from_country_package(self):
        commands, news = self.run_flash(True, country="Brasil")
        self.assertEqual([c[4] for c in commands], ["read_flash", "erase_flash", "write_flash"])
        self.assertEqual(commands[2][5], "0x0")

    def test_wrong_chip_writes_nothing(self):
        with self.assertRaises(core.CheckError):
            self.run_flash(False, board="ESP32-C3")


class Release(unittest.TestCase):
    """Real files of a release, when a folder of them is given."""

    def setUp(self):
        if not RELEASE_DIR:
            self.skipTest("no --release-dir")

    def test_real_files(self):
        folder = Path(RELEASE_DIR)
        sums = core.parse_sums((folder / "SHA256SUMS").read_text())
        checked = 0
        for path in sorted(folder.glob("*.bin")):
            data = core.checked(path.name, path.read_bytes(), sums)
            full = path.name.endswith("-merged.bin")
            chip = core.check_file(data, full)
            self.assertTrue(chip.startswith("ESP32"), path.name)
            checked += 1
        self.assertGreater(checked, 0)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--release-dir")
    args, rest = parser.parse_known_args()
    RELEASE_DIR = args.release_dir
    unittest.main(argv=[sys.argv[0]] + rest)
