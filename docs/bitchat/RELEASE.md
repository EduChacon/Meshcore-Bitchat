# MeshCore + Bitchat v1.18.0-bitchat0.9

Firmware _companion_ do MeshCore 1.18.0 com ponte embutida para o aplicativo Bitchat.
**Versão experimental.**

## Estado

- **Heltec V3:** o autor instalou a `bitchat0.1`, seguiu o roteiro de primeiro uso e relatou que
  tudo funcionou; as capturas mostram texto nos dois sentidos entre o aplicativo MeshCore e o
  Bitchat e os dois autotestes. Não há medições de alcance, consumo ou duração, nem ensaio da
  travessia pelo LoRa.
- **O que veio depois da `bitchat0.1` não foi ligado em aparelho:** a página da ponte na tela, a
  ligação com as pontes Meshtastic, o Cardputer ADV, o T1000-E, o modo econômico, o guia, os
  pacotes por país, o histórico e a lista de nomes. Passou nos testes em computador
  e compila.
- **Demais aparelhos da [lista](https://github.com/EduChacon/Meshcore-Bitchat/blob/main/docs/bitchat/DEVICES.md):**
  só compilados; nenhum foi ligado.

## O que há de novo na `bitchat0.9`

- **`!bridge who`** (ou `!ponte quem`, em português): lista os nomes ouvidos no canal LoRa nas
  últimas seis horas, do mais recente ao mais antigo. Vale no aplicativo MeshCore e no `#mesh` do
  Bitchat, cujo `/who` só mostra quem está na malha Bluetooth.

## Das versões anteriores

- **Histórico para quem chega depois** (`bitchat0.8`): o texto que vem do LoRa é guardado mesmo sem
  telefone conectado (24 mensagens, 8 no T1000-E, por até seis horas) e entregue ao telefone que
  chega, pelo mecanismo do próprio Bitchat. Não foi ensaiado com o aplicativo real.
  [Roteiro](https://github.com/EduChacon/Meshcore-Bitchat/blob/main/docs/bitchat/ROADMAP.md) das
  ideias.
- **Pacotes por país** (`bitchat0.7`): `…-Brasil.zip`, `…-EUA.zip` e `…-Europa-e-Reino-Unido.zip`; um
  aparelho gravado do zero já sai na frequência de lá e não precisa do aplicativo MeshCore.
- **Guia dentro da ponte** (`bitchat0.7`): `!bridge guide` ou `!ponte guia`, no aplicativo ou no
  `#mesh` do Bitchat.
- **O modo econômico é opcional e vem desligado** (`bitchat0.6`; na `bitchat0.5` era automático).
  Liga-se com `!bridge economy on` e desliga-se com `!bridge economy off`; a escolha fica gravada.
  Em ritmo lento, o aplicativo MeshCore também pode demorar mais para encontrar o rádio.
- **Modo econômico da ponte** (`bitchat0.5`): depois de três minutos sem nenhum telefone Bitchat, o
  rádio passa a se anunciar cerca de uma vez por segundo, em vez de várias, e a ponte trabalha com
  menos frequência. O primeiro telefone que falar traz o ritmo normal de volta. O ganho de bateria
  não foi medido.
- A ponte pode ser usada por outro projeto que carregue este repositório como biblioteca
  (`bitchat0.5`; é o caso do [Saitama com ponte](https://github.com/EduChacon/Saitama-mesh---BC-)).
- **Cardputer ADV, duas correções** (`bitchat0.4`) achadas pela leitura do código, não em aparelho:
  `Backspace` não apagava na `bitchat0.3` (a biblioteca do aparelho mudou em maio de 2026 e agora
  fica fixada na versão anterior); e mais de 64 contatos corrompiam a memória (defeito herdado do
  projeto original). **Quem gravou a `bitchat0.3` no Cardputer ADV deve trocar por esta.**
  [Notas](https://github.com/EduChacon/Meshcore-Bitchat/blob/main/docs/bitchat/CARDPUTER.md).
- **T-Deck e T-Deck Plus** (`bitchat0.4`): [notas](https://github.com/EduChacon/Meshcore-Bitchat/blob/main/docs/bitchat/TDECK.md)
  sobre o que o arquivo faz (o teclado do aparelho não é usado) e sobre os sistemas próprios para
  ele. O arquivo não mudou.
- **M5Stack Cardputer ADV com Cap LoRa-1262** (`bitchat0.3`): conversa no próprio aparelho, pelo
  teclado (contatos, canais, ajustes), com a ponte ligada a ela. A interface vem do projeto
  [MeshCore-Cardputer-ADV](https://github.com/Stachugit/MeshCore-Cardputer-ADV), de Stachu (MIT).
- **SenseCAP T1000-E, experimental** (`bitchat0.3`): primeira versão da ponte para o chip nRF52.
  [Notas e riscos](https://github.com/EduChacon/Meshcore-Bitchat/blob/main/docs/bitchat/T1000E.md).

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
  rádio do país (`Brasil`, `EUA`, `Europa-e-Reino-Unido`).
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
