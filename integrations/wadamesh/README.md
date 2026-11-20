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

f) `platformio.ini`: o ambiente `_bitchat`, com C++17, pilha de 16 KB no ciclo principal e a memória
da ponte na PSRAM.

## Trocar a versão do WadaMesh

1. Pôr o commit novo em `wadamesh.json`.
2. `python3 tools/bitchat/build_wadamesh.py --prepare-only --work /tmp/wm` e resolver o que o patch
   não aplicar.
3. Gerar o patch de novo com `git -C /tmp/wm diff -- . ':!src/helpers/bitchat' ':!src/BitchatHost.cpp'
':!src/helpers/esp32/SerialBLEInterface.h' > integrations/wadamesh/bridge.patch`.
4. Compilar sem `--prepare-only` e conferir o tamanho: o WadaMesh já usa 93% da partição sozinho.
