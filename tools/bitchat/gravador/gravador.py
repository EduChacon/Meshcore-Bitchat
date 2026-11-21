"""Gravador MeshCore + Bitchat: writes the bridge firmware of this release to a radio over USB, from a
small window, for Mac and Windows. It fetches the device's file from the release on GitHub (or takes one
from the computer), checks its SHA-256 and that it is for the chip connected, can keep a full copy of the
radio first, and runs esptool. nRF52 boards (T1000-E, RAK4631...) need no flasher: it saves their .uf2 file
and says how to copy it to the board.

Run `gravador --self-test` to check that the packaged app has everything it needs (used by the build).
"""

import contextlib
import io
import queue
import re
import sys
import threading
import time
import traceback
from pathlib import Path

import gravador_core as core

TEXT = {
    "pt": {
        "title": "Gravador MeshCore + Bitchat",
        "device": "1. Aparelho",
        "action": "2. O que fazer",
        "update": "Atualizar, mantendo contatos, canais e identidade",
        "full": "Instalar do zero (apaga tudo o que há no rádio)",
        "country": "Rádio de fábrica (só ao instalar do zero):",
        "general": "Sem país (ajustar depois no aplicativo MeshCore)",
        "port": "3. Porta USB",
        "refresh": "Atualizar lista",
        "backup": "Guardar antes uma cópia completa do rádio (em Documentos/Gravador-Bitchat)",
        "local": "Usar um arquivo deste computador...",
        "local_none": "Arquivo da versão {version}, baixado do GitHub",
        "start": "Gravar",
        "uf2": "Baixar o arquivo .uf2",
        "busy": "Gravando... não desconecte o rádio.",
        "done": "Pronto. O rádio reinicia sozinho.",
        "failed": "Não deu certo: {error}",
        "no_port": "Conecte o rádio pelo cabo USB e escolha a porta.",
        "no_device": "Escolha o aparelho.",
        "confirm_full": "Instalar do zero apaga contatos, canais e a identidade do rádio. Continuar?",
        "fetching": "Baixando {name}...",
        "checking": "Conferindo o arquivo e o rádio...",
        "wrong_chip": "Este arquivo é para {file}, mas o rádio conectado é {board}. Confira o aparelho.",
        "saving_backup": "Guardando a cópia do rádio em {path} (alguns minutos)...",
        "writing": "Gravando...",
        "nrf_help": (
            "Este aparelho não usa o gravador: o arquivo .uf2 é copiado para ele.\n"
            "1. Baixe o arquivo (botão abaixo).\n"
            "2. T1000-E: segure o botão e, sem soltar, conecte o cabo duas vezes, rápido. Outros: toque\n"
            "   duas vezes no botão de reinício. O computador mostra uma unidade de disco.\n"
            "3. Copie o arquivo .uf2 para essa unidade. Ao fim da cópia o aparelho reinicia sozinho."
        ),
        "saved": "Arquivo salvo em {path}",
        "no_build": "Este gravador não sabe a que versão pertence: use um arquivo deste computador.",
        "boot_hint": (
            "Se o rádio não responder: desconecte, segure o botão BOOT (PRG no Heltec) e conecte de novo, "
            "ainda segurando."
        ),
    },
    "en": {
        "title": "MeshCore + Bitchat Flasher",
        "device": "1. Device",
        "action": "2. What to do",
        "update": "Update, keeping contacts, channels and identity",
        "full": "Install from scratch (erases everything on the radio)",
        "country": "Factory radio (only when installing from scratch):",
        "general": "No country (set it later in the MeshCore app)",
        "port": "3. USB port",
        "refresh": "Refresh",
        "backup": "First keep a full copy of the radio (in Documents/Gravador-Bitchat)",
        "local": "Use a file from this computer...",
        "local_none": "File of release {version}, downloaded from GitHub",
        "start": "Flash",
        "uf2": "Download the .uf2 file",
        "busy": "Flashing... do not unplug the radio.",
        "done": "Done. The radio restarts by itself.",
        "failed": "It did not work: {error}",
        "no_port": "Connect the radio with the USB cable and choose its port.",
        "no_device": "Choose the device.",
        "confirm_full": "Installing from scratch erases the radio's contacts, channels and identity. Go on?",
        "fetching": "Downloading {name}...",
        "checking": "Checking the file and the radio...",
        "wrong_chip": "This file is for {file}, but the radio connected is {board}. Check the device.",
        "saving_backup": "Keeping a copy of the radio in {path} (a few minutes)...",
        "writing": "Flashing...",
        "nrf_help": (
            "This device does not use the flasher: its .uf2 file is copied to it.\n"
            "1. Download the file (button below).\n"
            "2. T1000-E: hold the button and, still holding, plug the cable in twice, quickly. Others:\n"
            "   double-tap the reset button. The computer shows a disk drive.\n"
            "3. Copy the .uf2 file to that drive. When the copy ends the device restarts by itself."
        ),
        "saved": "File saved in {path}",
        "no_build": "This flasher does not know its release: use a file from this computer.",
        "boot_hint": "If the radio does not answer: unplug it, hold BOOT (PRG on the Heltec) and plug it in again, still holding.",
    },
}

COUNTRIES = ["Brasil", "EUA", "Europa e Reino Unido", "Rede propria 918 MHz"]


class Output(io.TextIOBase):
    """esptool's printed lines, to the window; its percentages, to the progress bar."""

    def __init__(self, post):
        self.post = post

    def write(self, text):
        for piece in re.split(r"[\r\n]+", text):
            if not piece.strip():
                continue
            percent = re.search(r"\((\d+)\s*%\)", piece)
            if percent:
                self.post("progress", int(percent.group(1)))
            else:
                self.post("log", piece)
        return len(text)


def detect(port):
    """The chip esptool finds on `port` (its name, e.g. "ESP32-S3")."""
    from esptool.cmds import detect_chip

    esp = detect_chip(port, 115200)
    try:
        return esp.CHIP_NAME
    finally:
        esp._port.close()


def flash(job, post, words):
    """The whole flashing, in a worker thread. `post(kind, value)` sends news to the window."""
    import esptool

    version = job["version"]
    if job["local"]:
        path = Path(job["local"])
        data = path.read_bytes()
    else:
        device = job["device"]
        name = core.file_name(device, job["full"])
        post("status", words["fetching"].format(name="SHA256SUMS"))
        sums = core.parse_sums(core.fetch(core.release_url(version, "SHA256SUMS")).decode("utf-8", "replace"))
        progress = lambda done, total: post("progress", int(done * 100 / total) if total else 0)
        if job["full"] and job["country"]:
            package = core.country_zip(version, job["country"])
            post("status", words["fetching"].format(name=package))
            archive = core.checked(package, core.fetch(core.release_url(version, package), progress), sums)
            data = core.from_country_zip(archive, name)
        else:
            post("status", words["fetching"].format(name=name))
            data = core.checked(name, core.fetch(core.release_url(version, name), progress), sums)
        path = core.backup_folder() / name
        path.write_bytes(data)
    post("status", words["checking"])
    file_chip = core.check_file(data, job["full"])
    board = detect(job["port"])
    if not core.same_chip(file_chip, board):
        raise core.CheckError(words["wrong_chip"].format(file=file_chip, board=board))
    backup = None
    if job["backup"]:
        backup = core.backup_folder() / f"copia-{time.strftime('%Y%m%d-%H%M%S')}.bin"
    for command in core.steps(job["port"], path, job["full"], backup):
        if "read_flash" in command:
            post("status", words["saving_backup"].format(path=backup))
        else:
            post("status", words["writing"])
        post("log", "esptool " + " ".join(command))
        esptool.main(command)
    post("done", None)


def nrf_save(job, post, words):
    device, version = job["device"], job["version"]
    name = core.file_name(device, False)
    post("status", words["fetching"].format(name=name))
    sums = core.parse_sums(core.fetch(core.release_url(version, "SHA256SUMS")).decode("utf-8", "replace"))
    data = core.checked(name, core.fetch(core.release_url(version, name)), sums)
    path = core.downloads_folder() / name
    path.write_bytes(data)
    post("saved", str(path))


def window():
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk

    build = core.load_build()
    words = TEXT[core.language()]
    version = build.get("version", "")
    devices = core.devices(build)
    root = tk.Tk()
    root.title(f"{words['title']} {version}".strip())
    root.resizable(False, False)
    news = queue.Queue()
    frame = ttk.Frame(root, padding=14)
    frame.grid()

    ttk.Label(frame, text=words["device"]).grid(row=0, column=0, sticky="w")
    device_box = ttk.Combobox(frame, state="readonly", width=48, values=[d.label for d in devices])
    device_box.grid(row=1, column=0, columnspan=2, sticky="w", pady=(0, 8))

    action_label = ttk.Label(frame, text=words["action"])
    action_label.grid(row=2, column=0, sticky="w")
    full = tk.BooleanVar(value=False)

    def on_action():
        country_box.state(["!disabled", "readonly"] if full.get() else ["disabled"])

    update_button = ttk.Radiobutton(frame, text=words["update"], variable=full, value=False, command=on_action)
    update_button.grid(row=3, column=0, columnspan=2, sticky="w")
    full_button = ttk.Radiobutton(frame, text=words["full"], variable=full, value=True, command=on_action)
    full_button.grid(row=4, column=0, columnspan=2, sticky="w")
    country_label = ttk.Label(frame, text=words["country"])
    country_label.grid(row=5, column=0, sticky="w", padx=(22, 0))
    country_box = ttk.Combobox(frame, state="readonly", width=40, values=COUNTRIES + [words["general"]])
    country_box.current(0)
    country_box.grid(row=6, column=0, columnspan=2, sticky="w", padx=(22, 0), pady=(0, 8))
    on_action()

    port_label = ttk.Label(frame, text=words["port"])
    port_label.grid(row=7, column=0, sticky="w")
    port_box = ttk.Combobox(frame, width=38)
    port_box.grid(row=8, column=0, sticky="w")

    def refresh():
        ports = core.serial_ports()
        port_box["values"] = [f"{name}  ({description})" if description else name for name, description in ports]
        if ports and not port_box.get():
            port_box.current(0)

    refresh_button = ttk.Button(frame, text=words["refresh"], command=refresh)
    refresh_button.grid(row=8, column=1, sticky="e")

    backup = tk.BooleanVar(value=True)
    backup_check = ttk.Checkbutton(frame, text=words["backup"], variable=backup)
    backup_check.grid(row=9, column=0, columnspan=2, sticky="w", pady=(8, 0))

    local = tk.StringVar(value="")
    local_text = tk.StringVar(value=words["local_none"].format(version=version) if version else words["no_build"])

    def choose_file():
        chosen = filedialog.askopenfilename(filetypes=[("Firmware", "*.bin"), ("*", "*")])
        if chosen:
            local.set(chosen)
            local_text.set(Path(chosen).name)

    local_button = ttk.Button(frame, text=words["local"], command=choose_file)
    local_button.grid(row=10, column=0, sticky="w", pady=(8, 0))
    ttk.Label(frame, textvariable=local_text, foreground="gray").grid(row=11, column=0, columnspan=2, sticky="w")

    help_text = ttk.Label(frame, text="", justify="left")
    help_text.grid(row=12, column=0, columnspan=2, sticky="w", pady=(8, 0))

    start = ttk.Button(frame, text=words["start"])
    start.grid(row=13, column=0, sticky="w", pady=(10, 4))
    bar = ttk.Progressbar(frame, length=420, maximum=100)
    bar.grid(row=14, column=0, columnspan=2, sticky="w")
    status = tk.StringVar(value=words["boot_hint"])
    ttk.Label(frame, textvariable=status, wraplength=440, justify="left").grid(row=15, column=0, columnspan=2, sticky="w")
    log = tk.Text(frame, width=64, height=9, font=("Courier", 9))
    log.grid(row=16, column=0, columnspan=2, pady=(6, 0))

    esp_widgets = [action_label, update_button, full_button, country_label, country_box, port_label, port_box,
                   refresh_button, backup_check, local_button]

    def chosen_device():
        index = device_box.current()
        return devices[index] if 0 <= index < len(devices) else None

    def on_device(_event=None):
        device = chosen_device()
        nrf = bool(device and device.kind == "nrf52")
        for widget in esp_widgets:
            widget.state(["disabled"] if nrf else ["!disabled"])
        if not nrf:
            on_action()
        help_text.configure(text=words["nrf_help"] if nrf else "")
        start.configure(text=words["uf2"] if nrf else words["start"])

    device_box.bind("<<ComboboxSelected>>", on_device)

    def post(kind, value):
        news.put((kind, value))

    def run(target, job):
        def body():
            out = Output(post)
            try:
                with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
                    target(job, post, words)
            except BaseException as error:  # esptool ends a failed step with SystemExit or FatalError
                post("log", traceback.format_exc(limit=1).strip().splitlines()[-1])
                post("failed", str(error) or error.__class__.__name__)

        start.state(["disabled"])
        bar["value"] = 0
        threading.Thread(target=body, daemon=True).start()

    def on_start():
        device = chosen_device()
        if not local.get() and not device:
            messagebox.showwarning(words["title"], words["no_device"])
            return
        if device and device.kind == "nrf52" and not local.get():
            run(nrf_save, {"device": device, "version": version})
            return
        port = port_box.get().split("  (")[0].strip()
        if not port:
            messagebox.showwarning(words["title"], words["no_port"])
            return
        if not local.get() and not version:
            messagebox.showwarning(words["title"], words["no_build"])
            return
        if full.get() and not messagebox.askyesno(words["title"], words["confirm_full"]):
            return
        country = country_box.get()
        job = {
            "device": device,
            "version": version,
            "full": full.get(),
            "country": country if country in COUNTRIES else "",
            "port": port,
            "backup": backup.get(),
            "local": local.get(),
        }
        status.set(words["busy"])
        log.delete("1.0", "end")
        run(flash, job)

    start.configure(command=on_start)

    def pump():
        while True:
            try:
                kind, value = news.get_nowait()
            except queue.Empty:
                break
            if kind == "progress":
                bar["value"] = value
            elif kind == "log":
                log.insert("end", value + "\n")
                log.see("end")
            elif kind == "status":
                status.set(value)
            elif kind == "done":
                bar["value"] = 100
                status.set(words["done"])
                start.state(["!disabled"])
            elif kind == "saved":
                status.set(words["saved"].format(path=value))
                start.state(["!disabled"])
            elif kind == "failed":
                status.set(words["failed"].format(error=value) + "\n" + words["boot_hint"])
                start.state(["!disabled"])
        root.after(100, pump)

    refresh()
    root.after(100, pump)
    return root


def self_test():
    """What the packaged app needs, without a radio: esptool and its flasher stubs, the release list, the
    certificates, and the window itself (built, then closed)."""
    import esptool
    import serial.tools.list_ports  # noqa: F401

    build = core.load_build()
    assert build.get("version"), "build.json is missing"
    assert core.devices(build), "no devices in build.json"
    stubs = Path(esptool.__file__).parent / "targets" / "stub_flasher"
    assert any(stubs.rglob("*.json")), "esptool's flasher stubs are missing"
    try:
        import certifi

        assert Path(certifi.where()).exists()
    except ImportError:
        pass
    print("esptool", esptool.__version__, "| release", build["version"], "|", len(build["devices"]), "devices")
    try:
        root = window()
    except Exception as error:  # no display at all: the rest was checked
        if "display" in str(error).lower():
            print("window: no display here, not built")
            return 0
        raise
    root.update()
    root.destroy()
    print("window: built")
    return 0


def main():
    if "--self-test" in sys.argv:
        # A windowed app has no console: the result also goes to a file, for the build to show.
        log = Path("gravador-self-test.log")
        out = io.StringIO()
        try:
            with contextlib.redirect_stdout(out):
                result = self_test()
        except BaseException:
            out.write(traceback.format_exc())
            result = 1
        log.write_text(out.getvalue(), encoding="utf-8")
        if sys.stdout:
            sys.stdout.write(out.getvalue())
        return result
    window().mainloop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
