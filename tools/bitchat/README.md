# Ferramentas da ponte

| Script                | Para quê                                                                                |
| --------------------- | --------------------------------------------------------------------------------------- |
| `generate_envs.py`    | Gera `variants/bitchat/platformio.ini`, um ambiente com ponte por _companion_ Bluetooth |
| `main_targets.txt`    | Os aparelhos de cada versão; os demais saem só com _scope_ `all`                        |
| `run_tests.py`        | Testes em computador: núcleo, Noise, pagamentos, Bluetooth e fluxo completo             |
| `cashu_vectors.py`    | Casos de teste dos pagamentos (tokens e fluxos DEFLATE), usados por `run_tests.py`      |
| `noise_vectors.py`    | Gera `test/bitchat/noise_vectors.json` com uma implementação independente do Noise      |
| `nimble_pre.py`       | Executado na compilação: data, rádio do país e correção do NimBLE                       |
| `patch_nimble.py`     | A correção de partida do NimBLE 1.4.3, conferida pelo conteúdo                          |
| `regions.json`        | O rádio de fábrica de cada país                                                         |
| `collect_firmware.sh` | Copia os arquivos de um ambiente compilado, com o nome dele                             |
| `pack_regions.py`     | Junta os arquivos de cada país num ZIP                                                  |
| `build_wadamesh.py`   | Monta e compila o WadaMesh com a ponte ([guia](../../docs/bitchat/WADAMESH.md))         |

Testes, depois de uma compilação ter baixado as bibliotecas:

```bash
python3 tools/bitchat/run_tests.py --crypto-dir .pio/libdeps/Heltec_v3_companion_radio_ble_bitchat/Crypto
```
