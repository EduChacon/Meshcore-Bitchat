# MeshCore + Bitchat v1.18.0-bitchat0.20

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
- **Testes em computador desta versão:** 943 verificações do núcleo, 8005 dos pagamentos Cashu e
  1502 do fluxo completo, com telefones e rádio simulados; compilados no computador o Heltec V3, o
  T1000-E, o Cardputer ADV e o T-Deck.
- **T1000-E:** o autor gravou a `bitchat0.14`. O aplicativo MeshCore conectou, e a ponte ficou
  pronta para o LoRa, mas o Bitchat não a encontrava; a `bitchat0.17` corrige a causa achada.
- **Cardputer ADV:** o autor gravou a `bitchat0.13` e a `0.14`. Com a `0.14`, relatou melhora,
  mas o canal `#bitchat` não apareceu e o celular não conectou, por causas ainda não
  localizadas; a `bitchat0.15` mostra em **Device Info** o que é preciso para localizá-las.
- **Demais aparelhos da [lista](https://github.com/EduChacon/Meshcore-Bitchat/blob/main/docs/bitchat/DEVICES.md):**
  só compilados; nenhum foi ligado.

## O que há de novo na `bitchat0.20`

- **Pagamentos Cashu pelo LoRa.** Um token Cashu colado no chat público do Bitchat é levado pela
  ponte, inteiro, até as outras pontes do mesmo canal, que o entregam aos celulares perto delas. Vão
  as próprias mensagens assinadas pelo celular de quem paga, em partes que só aparecem do outro lado
  quando todas chegam; uma parte perdida é pedida de novo. Quem paga recebe no chat a confirmação
  (`✓`) ou o motivo da falha (`✗`). No canal padrão `#bitchat`, de chave pública, só passam tokens
  travados para a chave de quem recebe (P2PK); um token sem trava é recusado com aviso. A ponte não
  guarda nem gasta o token. Um token nunca vai como texto pelo MeshCore nem chega ao Bitchat em
  pedaços. Guia e limites: [pagamentos](https://github.com/EduChacon/Meshcore-Bitchat/blob/main/docs/bitchat/CASHU.md).
- **Som e luz do rádio (relato do T1000-E).** O rádio apitava a cada mensagem que a própria ponte
  levava do Bitchat ao LoRa, e a luz verde não parava de piscar. Agora o rádio não apita nem vibra
  com o próprio texto da ponte, e há duas opções, guardadas no rádio e digitadas no aplicativo
  MeshCore: `!ponte som off` (ou `on`) e `!ponte luz off` (ou `on`). Os bipes do MeshCore original
  continuam para mensagens de outras pessoas enquanto o aplicativo MeshCore não está conectado; o
  `som off` os cala também. No T1000-E, o toque triplo no botão continua ligando e desligando o
  som. **Não foi ligado em aparelho.**
- As duas pontes precisam desta versão para os pagamentos. **Não foi ligado em aparelho:** passou em 8005 verificações
  novas (descompressão, tokens oficiais NUT-00 e gerados, túnel) e no fluxo completo simulado
  (1502 verificações), e compila para os aparelhos principais.

## O que veio na `bitchat0.19`

- **Versões mais rápidas.** Cada versão passa a trazer só os aparelhos principais (Heltec V3, os
  quatro Heltec V4, T1000-E, Cardputer ADV e T-Deck) e o pacote do Brasil: 16 compilações em vez
  de 220. Os demais aparelhos e países saem sob pedido, numa execução à parte, e continuam nas
  versões anteriores. O firmware é o mesmo da `bitchat0.18`.

## O que veio na `bitchat0.18`

- **Heltec V3 e V4 (e outros com desligamento por bateria fraca): o rádio se desligava sozinho.**
  Relatado em aparelho: os Heltec V3 desligavam depois de algum tempo, com bateria ligada. O
  MeshCore desliga o rádio quando a bateria lê menos de 3,4 V, e uma única leitura bastava. A tensão
  cai por instantes quando o rádio transmite, a leitura do Heltec é grosseira, e a placa não sabe
  quando está no USB. Agora o rádio só se desliga se a bateria ficar abaixo do limite em todas as
  leituras por cerca de dois minutos, e a tela mostra a tensão lida. Uma bateria de fato vazia
  continua desligando o rádio, para não ser danificada.
- Compilados no computador o Heltec V3 e o V4 com ponte. **Não foi ligado em aparelho.**

## O que veio na `bitchat0.17`

- **T1000-E: o Bitchat não encontrava a ponte.** Relatado em aparelho: com a ponte pronta
  (`Ready: Bitchat + LoRa`), nenhum telefone Bitchat se conectava (`Bitchat 0`, `sub 0`), nem com o
  Bitchat sozinho. Achado na revisão do código: a biblioteca Bluetooth do nRF52 (Bluefruit) guarda
  apenas o endereço dos bytes do identificador do serviço e os lê de novo ao montar o anúncio; a
  ponte os montava numa área temporária da memória, já reaproveitada quando o anúncio era feito. O
  rádio anunciava bytes sem sentido no lugar do serviço do Bitchat, e o aplicativo nunca o
  reconhecia. Os identificadores agora ficam em memória permanente. Só o T1000-E muda; os ESP32 usam
  outra biblioteca e não tinham o defeito.
- Compilado no computador o T1000-E. **A correção não foi ligada em aparelho.**

## O que veio na `bitchat0.16`

Só o Cardputer ADV muda; nos demais aparelhos, esta versão é igual à `bitchat0.14`.

- **Contatos e conversas particulares.** A lista de contatos lia a tabela como no MeshCore 1.11 e,
  no 1.18, mostrava as oito posições reservadas (vazias) no lugar dos contatos; não havia como abrir
  uma conversa particular. Corrigido; a lista põe os favoritos primeiro e, depois, os ouvidos mais
  recentemente, com o tipo e há quanto tempo cada um foi ouvido. Mensagem não lida fica marcada, e a
  mensagem enviada mostra se o destinatário confirmou.
- **Aba Mesh**: anúncio aos rádios ao alcance ou a toda a rede, busca de repetidores ao alcance (com
  o sinal de cada um) e a lista dos rádios ouvidos.
- **Canais**: entrar num canal "hashtag" pelo nome, com a mesma chave do aplicativo MeshCore; sair
  de um canal pede confirmação.
- **PIN do Bluetooth** sempre no cabeçalho, com a bateria e o estado da conexão com o aplicativo.
- Bluetooth a +9 dBm no Cardputer ADV, para a conexão com o celular cair menos (não medido).
- Mensagens de até 160 caracteres guardadas inteiras na tela (antes, cortadas em 127).
- Compilado no computador o Cardputer ADV, com e sem ponte, e o Heltec V3 (sem mudança de memória).
  **Não foi ligado em aparelho.**

## O que veio na `bitchat0.15`

Só o Cardputer ADV muda; nos demais aparelhos, esta versão é igual à `bitchat0.14`.

- **Tela sem piscar.** Relatado em aparelho que a interface não era fluida. A tela inteira era
  apagada e redesenhada direto no painel a cada tecla, e o apagado aparecia. Agora cada quadro é
  montado na memória e enviado de uma vez. Como o Cardputer ADV não tem PSRAM, a memória do quadro
  (63 KB em cores de 16 bits, ou 32 KB em 8 bits) só é tomada oito segundos depois de ligar, quando a
  ponte já começou, e só se sobrarem 64 KB livres; sem isso, a tela é desenhada como antes.
- **Tecla mantida repete**, como num computador: as setas para cima e para baixo (`;` e `.`)
  percorrem a lista, e as letras se repetem na escrita. Enter, Backspace, Fn, Opt e as setas para
  os lados (que trocam de tela) não repetem.
- **O botão G0 não trava mais o rádio por um segundo**: o aviso "Advert sent" aparece sem parar o
  LoRa e o Bluetooth.
- **Device Info mostra o Bluetooth e a memória**: se o Bluetooth está ligado, o PIN, se o aplicativo
  MeshCore está pareado, a memória livre e o modo da tela. Frequência, SF, largura e potência ficam
  numa linha só, para caber tudo.
- Compilado no computador o Cardputer ADV, com e sem ponte. **Não foi ligado em aparelho.**

## O que veio na `bitchat0.14`

- **Cardputer ADV: a tela aparecia na vertical.** Relatado em aparelho. A biblioteca de tela da
  M5Stack (M5GFX 0.3.0, de 5 de novembro de 2026) mudou o giro do painel do Cardputer ADV, e o giro
  que a interface pedia passou a deixar a tela de pé. As bibliotecas da M5Stack ficam agora nas
  versões de janeiro de 2026 (M5Unified 0.2.13, M5GFX 0.2.19), as mesmas da época do projeto
  MeshCore-Cardputer-ADV de onde vem a interface, e a tela pede só o giro que falta, qualquer que
  seja a versão. **Quem gravou a `bitchat0.11` a `0.13` no Cardputer ADV deve trocar por esta.**
- **Cardputer ADV: predefinição "Brasil"** no topo da lista de rádio (923,125 MHz, SF 8, 62,5 kHz,
  CR 4/8), a convenção MeshCore Brasil; é a mesma "Australia: SA, WA". Cuidado com a "Australia: QLD",
  que tem CR 4/5.
- Compilado no computador o Cardputer ADV com as bibliotecas fixadas (M5Unified 0.2.13, M5GFX 0.2.19).
- As tabelas da ponte podem ser reduzidas por aparelho (`BITCHAT_DEDUP_ENTRIES`,
  `BITCHAT_PEER_ENTRIES`); nos aparelhos desta página, nada muda.

## O que veio na `bitchat0.13`

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
