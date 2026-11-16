# MeshCore + Bitchat v1.18.0-bitchat0.13

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
- **Testes em computador desta versão:** 943 verificações do núcleo e 1075 do fluxo completo, com
  telefones e rádio simulados; compilados no computador o Heltec V3 (com e sem ponte), o T1000-E,
  o Heltec Wireless Tracker e o Cardputer ADV.
- **Demais aparelhos da [lista](https://github.com/EduChacon/Meshcore-Bitchat/blob/main/docs/bitchat/DEVICES.md):**
  só compilados; nenhum foi ligado.

## O que há de novo na `bitchat0.13`

- **`!bridge position on`** (ou `!ponte posição on`), só no aplicativo do rádio: o socorro pedido
  pelo Bitchat passa a levar a posição do rádio, sem que ele a divulgue nos anúncios. Pensado para
  trilha e grupo em movimento. `!bridge position off` desliga; vem desligado. A escolha fica
  gravada, também no T1000-E, cujo arquivo de opções é convertido sem perder as anteriores.
- `!bridge help` passa a ter seis mensagens.
- Ensaio no GitHub e compilação local (Heltec V3, T1000-E) aprovados antes da publicação; a
  conversão do arquivo de opções do T1000-E foi conferida à parte, em computador.

## O que veio na `bitchat0.12`

Correções de segurança e de privacidade, a partir de uma revisão do código. Nada muda no uso
normal pelo aplicativo do próprio rádio.

- **Socorro pedido pelo Bitchat** só leva a posição se o rádio já a divulga nos anúncios; antes,
  qualquer pessoa por perto podia fazer o rádio transmitir a posição exata. Digitado no
  aplicativo do rádio, o socorro leva a posição como antes.
- **`!ponte radios` só responde no aplicativo do próprio rádio.** A lista mostra com quem o rádio
  teve atividade, inclusive mensagens privadas, e não deve ir a estranhos.
- **O recado só é trocado por um telefone ligado diretamente ao rádio**, não por um que chega pela
  malha de outros telefones.
- **GPS ligado para a hora:** ao desligá-lo, a ponte devolve ao rádio a posição que ele tinha; a
  posição do GPS não fica mais guardada.
- **Identidade:** ao importar uma chave nova para o nó, a ponte também passa a ter identidade
  nova, para que as duas não fiquem ligadas.
- **T1000-E:** se o gerador de números aleatórios não entregar tudo a tempo, a ponte não cria uma
  identidade fraca; tenta de novo.
- **Publicação:** só a partir da `main`; versões fixas das ferramentas de compilação.
- **Não verificado:** se o serviço do aplicativo MeshCore entrega notificações só a conexões
  pareadas, com a pilha NimBLE. Fica para o [roteiro de ensaio](https://github.com/EduChacon/Meshcore-Bitchat/blob/main/docs/bitchat/TESTING.md).
- Ensaios no GitHub e compilação local (Heltec V3 com e sem ponte, T1000-E, Wireless Tracker)
  aprovados antes da publicação.
- No T1000-E, trocar o PIN padrão `123456` depois do primeiro uso
  ([notas](https://github.com/EduChacon/Meshcore-Bitchat/blob/main/docs/bitchat/T1000E.md)).

## O que veio na `bitchat0.11`

Funções da casa, todas por comando, no aplicativo MeshCore ou no `#mesh` do Bitchat
([detalhes](https://github.com/EduChacon/Meshcore-Bitchat/blob/main/docs/bitchat/README.md#funções-da-casa-a-partir-da-bitchat011)):

- **`!bridge prefix on`**: só o texto do `#mesh` iniciado por `!lora` vai ao LoRa; o resto fica na
  casa. Só pelo aplicativo MeshCore; `!bridge prefix off` volta ao padrão.
- **`!ponte recado <texto>`**: recado da casa, entregue uma vez a cada 12 horas a cada telefone
  Bitchat que chega. Fica gravado. Qualquer pessoa perto do rádio pode trocá-lo.
- **`!ponte radios`**: rádios ouvidos no LoRa nas últimas 24 horas, com os saltos.
- **`!ponte bateria`** e aviso de bateria baixa no `#mesh`.
- **`!ponte sos <texto>`**: pedido de socorro ao LoRa, com a posição do rádio, se houver; um a cada
  cinco minutos. A posição vai para todos os que têm o canal.
- **`!ponte teste`**: teste de alcance; as outras pontes respondem com o sinal com que ouviram.
- **Hora pelo GPS**: em rádio com GPS, sem hora depois de 30 segundos, a ponte liga o GPS por até
  dez minutos para o MeshCore acertar o relógio. Não liga se o rádio divulga a posição nos anúncios.
- **Correção no T1000-E:** `!bridge economy on` respondia `Storage error`. As opções (`link`,
  `economy`, `prefix`) e o recado passam a ficar gravados num arquivo à parte, que sobrevive ao
  reinício; a identidade da ponte é mantida.
- `!bridge help` passa a ter cinco mensagens.

## Das versões anteriores

- **Histórico, segunda parte** (`bitchat0.10`): a ponte passa a guardar também o que os telefones escrevem no
  `#mesh` perto do rádio, com a assinatura de quem escreveu, para quem chega depois. Quem chega só
  vê essas mensagens se já conheceu o autor antes (regra do próprio Bitchat).
- **Pacote `Rede-propria-918-MHz`** (`bitchat0.10`). Para casas sem repetidor por perto: 918,000 MHz, com a
  repetição já ligada; os rádios gravados com ele formam uma malha à parte, fora da rede da
  comunidade.
- [Estudo de privacidade e pagamento](https://github.com/EduChacon/Meshcore-Bitchat/blob/main/docs/bitchat/PRIVACY.md):
  o que já é possível (canal com chave própria) e o que depende de ensaio.

- **`!bridge who`** (`bitchat0.9`), ou `!ponte quem`: nomes ouvidos no canal LoRa nas últimas seis
  horas, no aplicativo MeshCore ou no `#mesh` do Bitchat.
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
  rádio do país (`Brasil`, `EUA`, `Europa-e-Reino-Unido`) ou da rede própria em 918 MHz
  (`Rede-propria-918-MHz`).
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
