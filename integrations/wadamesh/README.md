# WadaMesh + ponte Bitchat: o encaixe

O guia para quem usa está em [docs/bitchat/WADAMESH.md](../../docs/bitchat/WADAMESH.md). Aqui, só o
que é preciso para manter o encaixe.

| Arquivo                                  | Papel                                                                  |
| ---------------------------------------- | ---------------------------------------------------------------------- |
| `wadamesh.json`                          | Repositório e commit do WadaMesh usados; ambiente de compilação        |
| `bridge.patch`                           | Alterações no WadaMesh, todas entre `#ifdef WITH_BITCHAT_BRIDGE`       |
| `src/BitchatHost.cpp`                    | O que a ponte pede ao firmware (`bitchat::MeshHost`), no `MyMesh` dele |
| `src/helpers/esp32/SerialBLEInterface.h` | Põe a interface Bluetooth da ponte no lugar da do núcleo               |

O roteiro `tools/bitchat/build_wadamesh.py` junta tudo: baixa o WadaMesh no commit fixado, aplica o
patch, copia estes arquivos e `src/helpers/bitchat` para dentro dele e compila o ambiente
`LilyGo_TDeck_companion_radio_touch_bitchat`.

## O que o patch muda no WadaMesh

a) `src/MyMesh.h`: `MyMesh` passa a ser também `bitchat::MeshHost`.

b) `src/MyMesh.cpp`: o texto de canal ouvido no LoRa vai à ponte; os dados de canal do túnel de
pagamentos não chegam aos aplicativos; um comando da ponte digitado no aplicativo é respondido e
não transmitido.

c) `src/main.cpp`: a ponte começa depois da interface e roda no ciclo principal.

d) `src/ui-touch/UITask.cpp`: o que se digita num canal na tela passa pela ponte (comandos e texto
para o Bitchat), no editor de conversa e no envio do terminal, do controle pela web e dos apps Lua.

e) `src/helpers/esp32/MultiTransportCompanionInterface.cpp`: a ponte solta o servidor Bluetooth antes
de o WadaMesh desligar a pilha, e o MTU de 517 da ponte é mantido.

e1) `src/helpers/esp32/MultiTransportCompanionInterface.h`: com a ponte, inclui `"SerialBLEInterface.h"`, o
vizinho, que é o da ponte. Com `<helpers/esp32/SerialBLEInterface.h>`, o compilador acha antes o cabeçalho
da biblioteca do núcleo, de outra classe: compila e liga sem aviso, mas `_ble` fica com o tamanho e os
campos da classe do núcleo, e a primeira partida do Bluetooth trava o aparelho (bitchat1.2.1, T-Deck Plus).
O `build_wadamesh.py` também põe um `#error` no cabeçalho do núcleo, para que isso não volte em silêncio.

f) `src/main.cpp` também: 16 KB de pilha no ciclo principal com a ponte (o `SET_LOOP_TASK_STACK_SIZE`
do WadaMesh decide a pilha; `ARDUINO_LOOP_STACK_SIZE` não teria efeito).

g) `platformio.ini`: o ambiente `_bitchat`, com C++17, o caminho do MicroNMEA na pasta de bibliotecas
deste ambiente e a memória da ponte na PSRAM (`BRIDGE_ALLOC_CAPS` e `BRIDGE_HEAP_CAPS`).

## Trocar a versão do WadaMesh

1. Pôr o commit novo em `wadamesh.json`.
2. `python3 tools/bitchat/build_wadamesh.py --prepare-only --work /tmp/wm` e resolver o que o patch
   não aplicar.
3. Gerar o patch de novo com `git -C /tmp/wm diff -- . ':!src/helpers/bitchat' ':!src/BitchatHost.cpp'
':!src/helpers/esp32/SerialBLEInterface.h' > integrations/wadamesh/bridge.patch`.
4. Compilar sem `--prepare-only` e conferir o tamanho: o WadaMesh já usa 93% da partição sozinho.
