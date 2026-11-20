# Aparelhos

Um arquivo por aparelho, com o nome do firmware _companion_ Bluetooth original do MeshCore e o
final `_bitchat`. **Só o Heltec V3 foi ligado.** "Compila" quer dizer apenas que o arquivo foi
gerado sem erro: nada garante que o rádio funcione com ele.

**A partir da `bitchat0.19`, cada versão traz só os aparelhos de `tools/bitchat/main_targets.txt`**
(Heltec V3, os quatro Heltec V4, T1000-E, Cardputer ADV e T-Deck). Os demais desta lista são
compilados sob pedido (_Actions_ > _Bitchat bridge_ > _Run workflow_, _scope_ `all`).

Os 42 primeiros ambientes foram compilados pela compilação automática do GitHub em 2026-11-15, para a
versão `v1.18.0-bitchat0.2`. O Cardputer ADV e o T1000-E entraram na `v1.18.0-bitchat0.3`.

| Arquivo (sem a extensão)                                  | Chip     | Estado                                                    |
| --------------------------------------------------------- | -------- | --------------------------------------------------------- |
| `Ebyte_EoRa-S3_companion_radio_ble_bitchat`               | ESP32-S3 | Compila; não foi ligado                                   |
| `Heltec_ct62_companion_radio_ble_bitchat`                 | ESP32-C3 | Compila; não foi ligado                                   |
| `Heltec_E213_companion_radio_ble_bitchat`                 | ESP32-S3 | Compila; não foi ligado                                   |
| `Heltec_E290_companion_ble_bitchat`                       | ESP32-S3 | Compila; não foi ligado                                   |
| `heltec_rc32_companion_radio_ble_bitchat`                 | ESP32-S3 | Compila; não foi ligado                                   |
| `heltec_rc32_without_display_companion_radio_ble_bitchat` | ESP32-S3 | Compila; não foi ligado                                   |
| `Heltec_T190_companion_radio_ble_bitchat`                 | ESP32-S3 | Compila; não foi ligado                                   |
| `heltec_tracker_v2_companion_radio_ble_bitchat`           | ESP32-S3 | Compila; não foi ligado                                   |
| `Heltec_v2_companion_radio_ble_bitchat`                   | ESP32    | Compila; não foi ligado                                   |
| `Heltec_v3_companion_radio_ble_bitchat`                   | ESP32-S3 | Funcionou no ensaio do autor (versão `bitchat0.1`)        |
| `heltec_v4_companion_radio_ble_bitchat`                   | ESP32-S3 | Compila; não foi ligado                                   |
| `heltec_v4_r8_companion_radio_ble_bitchat`                | ESP32-S3 | Compila; não foi ligado                                   |
| `heltec_v4_r8_tft_companion_radio_ble_bitchat`            | ESP32-S3 | Compila; não foi ligado                                   |
| `heltec_v4_tft_companion_radio_ble_bitchat`               | ESP32-S3 | Compila; não foi ligado                                   |
| `Heltec_Wireless_Paper_companion_radio_ble_bitchat`       | ESP32-S3 | Compila; não foi ligado                                   |
| `Heltec_Wireless_Tracker_companion_radio_ble_bitchat`     | ESP32-S3 | Compila; não foi ligado                                   |
| `Heltec_WSL3_companion_radio_ble_bitchat`                 | ESP32-S3 | Compila; não foi ligado                                   |
| `LilyGo_T3S3_sx1262_companion_radio_ble_bitchat`          | ESP32-S3 | Compila; não foi ligado                                   |
| `LilyGo_T3S3_sx1276_companion_radio_ble_bitchat`          | ESP32-S3 | Compila; não foi ligado                                   |
| `LilyGo_TBeam_1W_companion_radio_ble_bitchat`             | ESP32-S3 | Compila; não foi ligado                                   |
| `LilyGo_TDeck_companion_radio_ble_bitchat`                | ESP32-S3 | Compila; não foi ligado. [Notas](TDECK.md)                |
| `LilyGo_TETH_Elite_sx1262_companion_radio_ble_bitchat`    | ESP32-S3 | Compila; não foi ligado                                   |
| `LilyGo_TLora_V2_1_1_6_companion_radio_ble_bitchat`       | ESP32    | Compila; não foi ligado                                   |
| `M5Stack_Cardputer_ADV_companion_radio_ble_bitchat`       | ESP32-S3 | Compila; não foi ligado. [Notas](CARDPUTER.md)            |
| `Meshadventurer_sx1262_companion_radio_ble_bitchat`       | ESP32    | Compila; não foi ligado                                   |
| `Meshadventurer_sx1268_companion_radio_ble_bitchat`       | ESP32    | Compila; não foi ligado                                   |
| `meshnology_w12_companion_radio_ble_bitchat`              | ESP32-S3 | Compila; não foi ligado                                   |
| `nibble_screen_connect_companion_radio_ble_bitchat`       | ESP32-S3 | Compila; não foi ligado                                   |
| `nibble_zero_connect_companion_radio_ble_bitchat`         | ESP32-S3 | Compila; não foi ligado                                   |
| `RAK_3112_companion_radio_ble_bitchat`                    | ESP32-S3 | Compila; não foi ligado                                   |
| `Station_G2_companion_radio_ble_bitchat`                  | ESP32-S3 | Compila; não foi ligado                                   |
| `Station_G3_ESP32_companion_radio_ble_bitchat`            | ESP32-S3 | Compila; não foi ligado                                   |
| `t1000e_companion_radio_ble_bitchat`                      | nRF52840 | Experimental: compila; não foi ligado. [Notas](T1000E.md) |
| `T_Beam_S3_Supreme_SX1262_companion_radio_ble_bitchat`    | ESP32-S3 | Compila; não foi ligado                                   |
| `Tbeam_SX1262_companion_radio_ble_bitchat`                | ESP32    | Compila; não foi ligado                                   |
| `Tbeam_SX1276_companion_radio_ble_bitchat`                | ESP32    | Compila; não foi ligado                                   |
| `ThinkNode_M2_companion_radio_ble_bitchat`                | ESP32-S3 | Compila; não foi ligado                                   |
| `ThinkNode_M5_companion_radio_ble_bitchat`                | ESP32-S3 | Compila; não foi ligado                                   |
| `ThinkNode_M7_companion_radio_ble_bitchat`                | ESP32-S3 | Compila; não foi ligado                                   |
| `ThinkNode_M9_companion_radio_ble_bitchat`                | ESP32-S3 | Compila; não foi ligado                                   |
| `Waveshare_S3_LR1121_companion_radio_ble_bitchat`         | ESP32-S3 | Compila; não foi ligado                                   |
| `Xiao_C3_companion_radio_ble_bitchat`                     | ESP32-C3 | Compila; não foi ligado                                   |
| `Xiao_S3_companion_radio_ble_bitchat`                     | ESP32-S3 | Compila; não foi ligado                                   |
| `Xiao_S3_WIO_companion_radio_ble_bitchat`                 | ESP32-S3 | Compila; não foi ligado                                   |

Ficam fora: os aparelhos ESP32-C6, que usam outra versão da biblioteca Bluetooth, e os demais nRF52
(RAK4631 e outros), até o T1000-E ser ensaiado.

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
