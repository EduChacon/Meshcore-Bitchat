#!/usr/bin/env python3
"""Packs the firmware built for each country into one ZIP per country, with a note on what it sets."""

import argparse
import hashlib
import json
import zipfile
from pathlib import Path

NOTE = """MeshCore + Bitchat {version} - {title}

Arquivos para aparelhos usados em: {title}.
Um aparelho gravado do zero com um destes arquivos já parte com o rádio assim:

  Frequência ............ {freq} MHz
  Largura de banda ...... {bw} kHz
  Fator de espalhamento . {sf}
  Taxa de codificação ... {cr}
  Hash do caminho ....... {path_hash_bytes} byte(s)

Com isso, não é preciso abrir o aplicativo MeshCore para ajustar a frequência.
{repeat_note}O uso destas frequências fora do lugar indicado pode ser proibido: confira a regra local.

Qual arquivo usar (confira o nome do seu aparelho):
  <aparelho>_bitchat-merged.bin  ESP32, instalação limpa: apagar a memória e gravar em 0x0.
  <aparelho>_bitchat.bin         ESP32, atualização em 0x10000. MANTÉM a configuração que o
                                 aparelho já tinha, inclusive a frequência antiga.
  t1000e_..._bitchat.uf2         SenseCAP T1000-E, copiado para a unidade do aparelho. Se o
                                 aparelho já rodava MeshCore, a configuração antiga é mantida.

SHA256SUMS traz as somas de conferência dos arquivos desta pasta.
Guia: https://github.com/EduChacon/Meshcore-Bitchat/blob/main/docs/bitchat/README.md
"""

REPEAT_NOTE = """
REDE PRÓPRIA: este pacote não fala com a rede MeshCore da comunidade (923,125 MHz). Ele forma
uma malha à parte, para casas sem repetidor por perto: todos os rádios gravados com ele repassam
o que ouvem ("Ativar Modo de Repetição" já vem ligado). Repassar ocupa mais tempo de transmissão
e gasta mais bateria. O MeshCore só permite essa repetição em 433, 869,495 e 918 MHz.
"""

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("countries", type=Path, help="folder with one subfolder per country")
    parser.add_argument("output", type=Path, help="folder that receives the ZIP files")
    parser.add_argument("version")
    args = parser.parse_args()
    regions = json.loads((Path(__file__).with_name("regions.json")).read_text(encoding="utf-8"))
    args.output.mkdir(parents=True, exist_ok=True)
    for name, radio in regions.items():
        folder = args.countries / name
        files = sorted(path for path in folder.glob("*") if path.is_file()) if folder.is_dir() else []
        if not files:
            raise SystemExit(f"No firmware was built for {name}")
        title = radio["title"]
        repeat_note = REPEAT_NOTE if radio.get("repeat") else ""
        note = NOTE.format(version=args.version, repeat_note=repeat_note, **radio)
        sums = "".join(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n" for path in files)
        target = args.output / f"MeshCore-Bitchat-{args.version}-{title.replace(' ', '-')}.zip"
        top = f"{title.replace(' ', '-')}/"
        with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as archive:
            archive.writestr(top + "LEIA-ME.txt", note)
            archive.writestr(top + "SHA256SUMS", sums)
            for path in files:
                archive.write(path, top + path.name)
        print(f"{target.name}: {len(files)} files")


if __name__ == "__main__":
    main()
