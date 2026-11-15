# Aparelhos

Um arquivo por aparelho, com o nome do firmware _companion_ Bluetooth original do MeshCore e o
final `_bitchat`. **Só o Heltec V3 foi ligado.** "Compila" quer dizer apenas que o arquivo foi
gerado sem erro: nada garante que o rádio funcione com ele.

Os 42 ambientes foram compilados pela compilação automática do GitHub em 2026-11-15, para a versão
`v1.18.0-bitchat0.2`.

| Arquivo (sem a extensão)                                  | Chip     | Estado                                             |
| --------------------------------------------------------- | -------- | -------------------------------------------------- |
| `Ebyte_EoRa-S3_companion_radio_ble_bitchat`               | ESP32-S3 | Compila; não foi ligado                            |
| `Heltec_ct62_companion_radio_ble_bitchat`                 | ESP32-C3 | Compila; não foi ligado                            |
| `Heltec_E213_companion_radio_ble_bitchat`                 | ESP32-S3 | Compila; não foi ligado                            |
| `Heltec_E290_companion_ble_bitchat`                       | ESP32-S3 | Compila; não foi ligado                            |
| `heltec_rc32_companion_radio_ble_bitchat`                 | ESP32-S3 | Compila; não foi ligado                            |
| `heltec_rc32_without_display_companion_radio_ble_bitchat` | ESP32-S3 | Compila; não foi ligado                            |
| `Heltec_T190_companion_radio_ble_bitchat`                 | ESP32-S3 | Compila; não foi ligado                            |
| `heltec_tracker_v2_companion_radio_ble_bitchat`           | ESP32-S3 | Compila; não foi ligado                            |
| `Heltec_v2_companion_radio_ble_bitchat`                   | ESP32    | Compila; não foi ligado                            |
| `Heltec_v3_companion_radio_ble_bitchat`                   | ESP32-S3 | Funcionou no ensaio do autor (versão `bitchat0.1`) |
| `heltec_v4_companion_radio_ble_bitchat`                   | ESP32-S3 | Compila; não foi ligado                            |
| `heltec_v4_r8_companion_radio_ble_bitchat`                | ESP32-S3 | Compila; não foi ligado                            |
| `heltec_v4_r8_tft_companion_radio_ble_bitchat`            | ESP32-S3 | Compila; não foi ligado                            |
| `heltec_v4_tft_companion_radio_ble_bitchat`               | ESP32-S3 | Compila; não foi ligado                            |
| `Heltec_Wireless_Paper_companion_radio_ble_bitchat`       | ESP32-S3 | Compila; não foi ligado                            |
| `Heltec_Wireless_Tracker_companion_radio_ble_bitchat`     | ESP32-S3 | Compila; não foi ligado                            |
| `Heltec_WSL3_companion_radio_ble_bitchat`                 | ESP32-S3 | Compila; não foi ligado                            |
| `LilyGo_T3S3_sx1262_companion_radio_ble_bitchat`          | ESP32-S3 | Compila; não foi ligado                            |
| `LilyGo_T3S3_sx1276_companion_radio_ble_bitchat`          | ESP32-S3 | Compila; não foi ligado                            |
| `LilyGo_TBeam_1W_companion_radio_ble_bitchat`             | ESP32-S3 | Compila; não foi ligado                            |
| `LilyGo_TDeck_companion_radio_ble_bitchat`                | ESP32-S3 | Compila; não foi ligado                            |
| `LilyGo_TETH_Elite_sx1262_companion_radio_ble_bitchat`    | ESP32-S3 | Compila; não foi ligado                            |
| `LilyGo_TLora_V2_1_1_6_companion_radio_ble_bitchat`       | ESP32    | Compila; não foi ligado                            |
| `Meshadventurer_sx1262_companion_radio_ble_bitchat`       | ESP32    | Compila; não foi ligado                            |
| `Meshadventurer_sx1268_companion_radio_ble_bitchat`       | ESP32    | Compila; não foi ligado                            |
| `meshnology_w12_companion_radio_ble_bitchat`              | ESP32-S3 | Compila; não foi ligado                            |
| `nibble_screen_connect_companion_radio_ble_bitchat`       | ESP32-S3 | Compila; não foi ligado                            |
| `nibble_zero_connect_companion_radio_ble_bitchat`         | ESP32-S3 | Compila; não foi ligado                            |
| `RAK_3112_companion_radio_ble_bitchat`                    | ESP32-S3 | Compila; não foi ligado                            |
| `Station_G2_companion_radio_ble_bitchat`                  | ESP32-S3 | Compila; não foi ligado                            |
| `Station_G3_ESP32_companion_radio_ble_bitchat`            | ESP32-S3 | Compila; não foi ligado                            |
| `T_Beam_S3_Supreme_SX1262_companion_radio_ble_bitchat`    | ESP32-S3 | Compila; não foi ligado                            |
| `Tbeam_SX1262_companion_radio_ble_bitchat`                | ESP32    | Compila; não foi ligado                            |
| `Tbeam_SX1276_companion_radio_ble_bitchat`                | ESP32    | Compila; não foi ligado                            |
| `ThinkNode_M2_companion_radio_ble_bitchat`                | ESP32-S3 | Compila; não foi ligado                            |
| `ThinkNode_M5_companion_radio_ble_bitchat`                | ESP32-S3 | Compila; não foi ligado                            |
| `ThinkNode_M7_companion_radio_ble_bitchat`                | ESP32-S3 | Compila; não foi ligado                            |
| `ThinkNode_M9_companion_radio_ble_bitchat`                | ESP32-S3 | Compila; não foi ligado                            |
| `Waveshare_S3_LR1121_companion_radio_ble_bitchat`         | ESP32-S3 | Compila; não foi ligado                            |
| `Xiao_C3_companion_radio_ble_bitchat`                     | ESP32-C3 | Compila; não foi ligado                            |
| `Xiao_S3_companion_radio_ble_bitchat`                     | ESP32-S3 | Compila; não foi ligado                            |
| `Xiao_S3_WIO_companion_radio_ble_bitchat`                 | ESP32-S3 | Compila; não foi ligado                            |

Ficam fora: os aparelhos ESP32-C6, que usam outra versão da biblioteca Bluetooth, e os nRF52
(T1000-E, RAK4631 e outros), que dependem de uma adaptação ainda não ensaiada.
