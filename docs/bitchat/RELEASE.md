# MeshCore + Bitchat v1.18.0-bitchat0.24

Firmware _companion_ do MeshCore 1.18.0 com ponte embutida para o aplicativo Bitchat.
**Versão experimental.**

## Estado

- **Heltec V3:** o autor instalou a `bitchat0.1`, seguiu o roteiro de primeiro uso e relatou que
  tudo funcionou; as capturas mostram texto nos dois sentidos entre o aplicativo MeshCore e o
  Bitchat e os dois autotestes. Não há medições de alcance, consumo ou duração, nem ensaio da
  travessia pelo LoRa.
- **O que veio depois da `bitchat0.1` não foi ligado em aparelho:** a página da ponte na tela, a
  ligação com as pontes Meshtastic, o Cardputer ADV, o T1000-E, o modo econômico, o guia, os
  pacotes por país, o histórico, a lista de nomes, a rede própria e as funções da casa da
  `bitchat0.11` e as correções da `bitchat0.12`. Passou nos testes em computador e compila.
- **Testes em computador desta versão:** 943 verificações do núcleo, 223 da criptografia das
  conversas privadas (conferida com uma implementação independente do Noise), 8113 dos pagamentos
  Cashu e 2015 do fluxo completo, com telefones e rádio simulados; compilados no computador o
  Heltec V3, o T1000-E, o T-Deck (MeshCore e WadaMesh) e o Cardputer ADV.
- **T1000-E:** o autor gravou a `bitchat0.14`. O aplicativo MeshCore conectou, e a ponte ficou
  pronta para o LoRa, mas o Bitchat não a encontrava; a `bitchat0.17` corrige a causa achada.
- **Cardputer ADV:** o autor gravou a `bitchat0.13` e a `0.14`. Com a `0.14`, relatou melhora,
  mas o canal `#bitchat` não apareceu e o celular não conectou, por causas ainda não
  localizadas; a `bitchat0.15` mostra em **Device Info** o que é preciso para localizá-las.
- **Demais aparelhos da [lista](https://github.com/EduChacon/Meshcore-Bitchat/blob/main/docs/bitchat/DEVICES.md):**
  só compilados; nenhum foi ligado.

## O que há de novo na `bitchat0.24`

- **Conversa privada com a ponte.** No Bitchat, a ponte aparece na lista de pessoas como
  `LoRa bridge` e quatro caracteres; abre-se com ela uma conversa privada como com qualquer pessoa.
  Ali basta a palavra (`bateria`, `quem`, `guia`, `teste`, `recado <texto>`, `sos <texto>`, ou
  `ponte` para o estado), e a resposta sai só nessa conversa, sem aparecer no chat público. As
  regras são as do chat público: a lista de rádios e os ajustes continuam só no aplicativo do rádio.
  A criptografia é a mesma das conversas entre telefones (Noise, conferido byte a byte com uma
  implementação independente e com o código do Bitchat para iPhone e Android). Mensagens privadas
  entre pessoas continuam sem atravessar o LoRa.
- **Hora pelos anúncios do LoRa.** Sem aplicativo, GPS ou telefone por perto, a ponte acerta a hora
  quando dois rádios MeshCore diferentes ao alcance concordam com diferença de até um minuto. Resolve
  o `Clock not set` do Heltec V3 ligado sozinho. A hora só avança, o relógio do rádio não é alterado,
  e a hora de um telefone por perto continua valendo mais. Também no WadaMesh e no Saitama.
- **Nada disso foi ligado em aparelho.** Guia:
  [conversa privada](https://github.com/EduChacon/Meshcore-Bitchat/blob/main/docs/bitchat/README.md#conversa-privada-com-a-ponte-a-partir-da-bitchat024).

## O que veio na `bitchat0.23`

- **T-Deck e T-Deck Plus com WadaMesh e a ponte.** O [WadaMesh](https://github.com/ALLFATHER-BV/wadamesh)
  é um firmware MeshCore com tela de toque (mapa, conversas, contatos e canais). Cada versão passa a
  trazer também o WadaMesh com a ponte embutida, já com o rádio do Brasil:
  `LilyGo_TDeck_WadaMesh_bitchat-brasil-merged.bin` e `…-brasil.bin`. No canal `#bitchat` da tela
  aparecem as mensagens do Bitchat, o que se digita nele chega aos celulares, e os comandos
  `!ponte` funcionam ali mesmo. A ponte é a mesma das outras versões, num lugar só; o WadaMesh é
  baixado numa versão fixa e recebe a ponte na compilação. Guia, instalação e licença (GPL-3.0):
  [WADAMESH.md](https://github.com/EduChacon/Meshcore-Bitchat/blob/main/docs/bitchat/WADAMESH.md).
  **Só compilado; não foi ligado em aparelho.**
- **Bluetooth reiniciado entre duas voltas do programa:** a ponte percebe o servidor novo e recomeça
  nele, em vez de ficar calada com o estado antigo (o WadaMesh reinicia o Bluetooth numa atualização
  pelo Wi-Fi que não se conclui). Coberto pelos testes no computador.
- **Memória:** num aparelho com PSRAM, a ponte pode guardar nela todos os seus buffers
  (`BRIDGE_ALLOC_CAPS`); o WadaMesh usa isso, e a RAM interna fica para o Bluetooth, o Wi-Fi e a tela.
- O repositório foi organizado: notas antigas em
  [HISTORY.md](https://github.com/EduChacon/Meshcore-Bitchat/blob/main/docs/bitchat/HISTORY.md),
  índice dos documentos no [guia](https://github.com/EduChacon/Meshcore-Bitchat/blob/main/docs/bitchat/README.md)
  e descrição das ferramentas em `tools/bitchat/README.md`.

## Versões anteriores

O que mudou da `bitchat0.1` à `bitchat0.22`:
[HISTORY.md](https://github.com/EduChacon/Meshcore-Bitchat/blob/main/docs/bitchat/HISTORY.md).

## O que faz

- Texto público do `#mesh` do Bitchat sai no canal `#bitchat` do MeshCore como `apelido: texto`.
- Texto do canal `#bitchat` chega aos telefones Bitchat próximos como `[MC nome] texto`.
- O aplicativo MeshCore e o Bitchat usam o rádio ao mesmo tempo.
- A ponte liga sozinha e cria o canal na primeira partida.
- Comandos `!bridge` no aplicativo MeshCore e página **Bitchat bridge** na tela do rádio.
- Texto que uma ponte Meshtastic–Bitchat traz ao Bitchat (`[MT nome] texto`) também sai no canal
  do MeshCore, uma vez só, como `MT nome: texto`. Desliga-se com `!bridge link off`.

## Arquivos

**Confira o nome do seu aparelho antes de gravar.**

- ESP32: `<aparelho>_bitchat-merged.bin` (rádio novo ou vindo de outro firmware; apagar a memória e
  gravar no endereço `0x0`) e `<aparelho>_bitchat.bin` (rádio que já roda o MeshCore _companion_
  Bluetooth; gravar no endereço `0x10000`, mantendo os dados).
- T1000-E: `t1000e_companion_radio_ble_bitchat.uf2`, copiado para a unidade que o aparelho mostra
  em modo de gravação; o `.zip` é o pacote para a ferramenta de atualização da Nordic.
- `MeshCore-Bitchat-<versão>-<país>.zip`: os mesmos arquivos, para todos os aparelhos, já com o
  rádio do país (`Brasil`, `EUA`, `Europa-e-Reino-Unido`) ou da rede própria em 918 MHz
  (`Rede-propria-918-MHz`).
- `LilyGo_TDeck_WadaMesh_bitchat-brasil-merged.bin` e `…-brasil.bin`: o WadaMesh com a ponte para o
  T-Deck e o T-Deck Plus, com o rádio do Brasil
  ([guia](https://github.com/EduChacon/Meshcore-Bitchat/blob/main/docs/bitchat/WADAMESH.md)).
- `SHA256SUMS`: somas de conferência.

Instalação, comandos e limites:
[guia](https://github.com/EduChacon/Meshcore-Bitchat/blob/main/docs/bitchat/README.md).

## Avisos

- Depois de gravar, pode ser preciso remover o rádio dos aparelhos Bluetooth pareados do telefone
  e parear de novo.
- A frequência e a predefinição de rádio são ajustadas no aplicativo MeshCore, ou nos ajustes do
  Cardputer ADV, como no original.
- O canal `#bitchat` é aberto: qualquer pessoa entra nele pelo nome.
- Os demais nRF52 (RAK4631 etc.) e os ESP32-C6 não têm arquivo nesta versão.
