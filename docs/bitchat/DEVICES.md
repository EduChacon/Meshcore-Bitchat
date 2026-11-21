# Aparelhos

Um arquivo por aparelho, com o nome do firmware _companion_ Bluetooth original do MeshCore e o
final `_bitchat`. **Só o Heltec V3, o T1000-E e o Cardputer ADV foram ligados.** "Compila" quer
dizer apenas que o arquivo foi gerado sem erro: nada garante que o rádio funcione com ele.

**Cada versão traz os aparelhos populares de `tools/bitchat/main_targets.txt`** (marcados com
"Em toda versão" na tabela), mais o T-Deck com WadaMesh ([guia](WADAMESH.md)). A lista cresceu na
`bitchat0.24`: de 8 para 27 aparelhos, entre eles os nRF52 mais comuns (RAK4631, T-Echo, T114,
ThinkNode M1 e Xiao nRF52). Os demais são compilados sob pedido (_Actions_ > _Bitchat bridge_ >
_Run workflow_, _scope_ `all`).

Os 42 primeiros ambientes foram compilados pela compilação automática do GitHub em 2026-11-15, para a
versão `v1.18.0-bitchat0.2`. O Cardputer ADV e o T1000-E entraram na `v1.18.0-bitchat0.3`.

| Arquivo (sem a extensão)                                  | Chip     | Estado                                                                             |
| --------------------------------------------------------- | -------- | ---------------------------------------------------------------------------------- |
| `Ebyte_EoRa-S3_companion_radio_ble_bitchat`               | ESP32-S3 | Compila; não foi ligado                                                            |
| `Heltec_ct62_companion_radio_ble_bitchat`                 | ESP32-C3 | Compila; não foi ligado                                                            |
| `Heltec_E213_companion_radio_ble_bitchat`                 | ESP32-S3 | Compila; não foi ligado                                                            |
| `Heltec_E290_companion_ble_bitchat`                       | ESP32-S3 | Compila; não foi ligado                                                            |
| `heltec_rc32_companion_radio_ble_bitchat`                 | ESP32-S3 | Compila; não foi ligado                                                            |
| `heltec_rc32_without_display_companion_radio_ble_bitchat` | ESP32-S3 | Compila; não foi ligado                                                            |
| `Heltec_t114_companion_radio_ble_bitchat`                 | nRF52840 | Em toda versão; experimental: compila; não foi ligado. [Notas do nRF52](T1000E.md) |
| `Heltec_t114_without_display_companion_radio_ble_bitchat` | nRF52840 | Em toda versão; experimental: compila; não foi ligado. [Notas do nRF52](T1000E.md) |
| `Heltec_T190_companion_radio_ble_bitchat`                 | ESP32-S3 | Compila; não foi ligado                                                            |
| `heltec_tracker_v2_companion_radio_ble_bitchat`           | ESP32-S3 | Em toda versão; compila; não foi ligado                                            |
| `Heltec_v2_companion_radio_ble_bitchat`                   | ESP32    | Em toda versão; compila; não foi ligado                                            |
| `Heltec_v3_companion_radio_ble_bitchat`                   | ESP32-S3 | Em toda versão; ensaiado pelo autor: texto público, conversa privada e túnel       |
| `heltec_v4_companion_radio_ble_bitchat`                   | ESP32-S3 | Em toda versão; compila; não foi ligado                                            |
| `heltec_v4_r8_companion_radio_ble_bitchat`                | ESP32-S3 | Em toda versão; compila; não foi ligado                                            |
| `heltec_v4_r8_tft_companion_radio_ble_bitchat`            | ESP32-S3 | Em toda versão; compila; não foi ligado                                            |
| `heltec_v4_tft_companion_radio_ble_bitchat`               | ESP32-S3 | Em toda versão; compila; não foi ligado                                            |
| `Heltec_Wireless_Paper_companion_radio_ble_bitchat`       | ESP32-S3 | Em toda versão; compila; não foi ligado                                            |
| `Heltec_Wireless_Tracker_companion_radio_ble_bitchat`     | ESP32-S3 | Em toda versão; compila; não foi ligado                                            |
| `Heltec_WSL3_companion_radio_ble_bitchat`                 | ESP32-S3 | Compila; não foi ligado                                                            |
| `LilyGo_T-Echo_companion_radio_ble_bitchat`               | nRF52840 | Em toda versão; experimental: compila; não foi ligado. [Notas do nRF52](T1000E.md) |
| `LilyGo_T3S3_sx1262_companion_radio_ble_bitchat`          | ESP32-S3 | Em toda versão; compila; não foi ligado                                            |
| `LilyGo_T3S3_sx1276_companion_radio_ble_bitchat`          | ESP32-S3 | Compila; não foi ligado                                                            |
| `LilyGo_TBeam_1W_companion_radio_ble_bitchat`             | ESP32-S3 | Compila; não foi ligado                                                            |
| `LilyGo_TDeck_companion_radio_ble_bitchat`                | ESP32-S3 | Em toda versão; compila; não foi ligado. [Notas](TDECK.md)                         |
| `LilyGo_TETH_Elite_sx1262_companion_radio_ble_bitchat`    | ESP32-S3 | Compila; não foi ligado                                                            |
| `LilyGo_TLora_V2_1_1_6_companion_radio_ble_bitchat`       | ESP32    | Em toda versão; compila; não foi ligado                                            |
| `M5Stack_Cardputer_ADV_companion_radio_ble_bitchat`       | ESP32-S3 | Em toda versão; ligado pelo autor. [Notas](CARDPUTER.md)                           |
| `Meshadventurer_sx1262_companion_radio_ble_bitchat`       | ESP32    | Compila; não foi ligado                                                            |
| `Meshadventurer_sx1268_companion_radio_ble_bitchat`       | ESP32    | Compila; não foi ligado                                                            |
| `meshnology_w12_companion_radio_ble_bitchat`              | ESP32-S3 | Compila; não foi ligado                                                            |
| `nibble_screen_connect_companion_radio_ble_bitchat`       | ESP32-S3 | Compila; não foi ligado                                                            |
| `nibble_zero_connect_companion_radio_ble_bitchat`         | ESP32-S3 | Compila; não foi ligado                                                            |
| `RAK_3112_companion_radio_ble_bitchat`                    | ESP32-S3 | Em toda versão; compila; não foi ligado                                            |
| `RAK_4631_companion_radio_ble_bitchat`                    | nRF52840 | Em toda versão; experimental: compila; não foi ligado. [Notas do nRF52](T1000E.md) |
| `Station_G2_companion_radio_ble_bitchat`                  | ESP32-S3 | Em toda versão; compila; não foi ligado                                            |
| `Station_G3_ESP32_companion_radio_ble_bitchat`            | ESP32-S3 | Compila; não foi ligado                                                            |
| `t1000e_companion_radio_ble_bitchat`                      | nRF52840 | Em toda versão; ensaiado pelo autor: conversa privada e túnel. [Notas](T1000E.md)  |
| `T_Beam_S3_Supreme_SX1262_companion_radio_ble_bitchat`    | ESP32-S3 | Em toda versão; compila; não foi ligado                                            |
| `Tbeam_SX1262_companion_radio_ble_bitchat`                | ESP32    | Em toda versão; compila; não foi ligado                                            |
| `Tbeam_SX1276_companion_radio_ble_bitchat`                | ESP32    | Compila; não foi ligado                                                            |
| `ThinkNode_M1_companion_radio_ble_bitchat`                | nRF52840 | Em toda versão; experimental: compila; não foi ligado. [Notas do nRF52](T1000E.md) |
| `ThinkNode_M2_companion_radio_ble_bitchat`                | ESP32-S3 | Em toda versão; compila; não foi ligado                                            |
| `ThinkNode_M5_companion_radio_ble_bitchat`                | ESP32-S3 | Em toda versão; compila; não foi ligado                                            |
| `ThinkNode_M7_companion_radio_ble_bitchat`                | ESP32-S3 | Compila; não foi ligado                                                            |
| `ThinkNode_M9_companion_radio_ble_bitchat`                | ESP32-S3 | Compila; não foi ligado                                                            |
| `Waveshare_S3_LR1121_companion_radio_ble_bitchat`         | ESP32-S3 | Compila; não foi ligado                                                            |
| `Xiao_C3_companion_radio_ble_bitchat`                     | ESP32-C3 | Compila; não foi ligado                                                            |
| `Xiao_nrf52_companion_radio_ble_bitchat`                  | nRF52840 | Em toda versão; experimental: compila; não foi ligado. [Notas do nRF52](T1000E.md) |
| `Xiao_S3_companion_radio_ble_bitchat`                     | ESP32-S3 | Compila; não foi ligado                                                            |
| `Xiao_S3_WIO_companion_radio_ble_bitchat`                 | ESP32-S3 | Em toda versão; compila; não foi ligado                                            |

Os nRF52 entraram na `bitchat0.3` (T1000-E) e na `bitchat0.24` (os demais). Os novos não foram
ligados: o Bluetooth deles é o mesmo programa do T1000-E, diferente do dos ESP32. Ficam fora os
aparelhos ESP32-C6, que usam outra versão da biblioteca Bluetooth, e os nRF52 que ainda não
compilam com a ponte.

## Heltec V4: qual dos dois arquivos

O MeshCore tem duas variantes do Heltec WiFi LoRa 32 V4, conforme a memória do chip:

| Arquivo          | Chip do módulo | Memória extra (PSRAM) | Ligação do LoRa e do LED |
| ---------------- | -------------- | --------------------- | ------------------------ |
| `heltec_v4_…`    | ESP32-S3R2     | 2 MB                  | alimentação no pino 36   |
| `heltec_v4_r8_…` | ESP32-S3R8     | 8 MB                  | alimentação no pino 40   |

Os valores vêm das definições de placa do próprio MeshCore (`boards/heltec_v4.json`,
`boards/heltec_v4_r8.json` e as pastas `variants/`). O arquivo errado pode não iniciar ou deixar o
rádio sem transmitir. Para saber qual é o seu:

a) leia a inscrição do chip, sob a tampa metálica ou no anúncio do vendedor (`S3R2` ou `S3R8`); ou

b) com o [esptool](https://docs.espressif.com/projects/esptool/) e o rádio no cabo, rode
`python -m esptool chip_id` e veja a linha `Embedded PSRAM`: 2 MB indica `heltec_v4`, 8 MB indica
`heltec_v4_r8`.

O programa do `heltec_v4` reconhece sozinho as revisões 4.2 e 4.3 (amplificadores diferentes). As
variantes `_tft` são para o modelo com tela colorida. Nada disso foi conferido em aparelho.
