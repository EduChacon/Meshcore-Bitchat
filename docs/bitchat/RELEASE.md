# MeshCore + Bitchat v1.18.0-bitchat1.2

Firmware _companion_ do MeshCore 1.18.0 com uma ponte embutida para o aplicativo Bitchat: celulares
com o Bitchat conversam com a malha MeshCore, e entre si, pelo LoRa, sem internet.

A `bitchat1.2` deixa o túnel utilizável no dia a dia: a pessoa da outra casa fica na lista do
Bitchat enquanto as duas casas estão com ele aberto, e a conversa privada abre sem esperar minutos.

## Estado

- **Ensaiado em aparelho pelo autor:** o texto público entre o Bitchat e o MeshCore (Heltec V3); a
  conversa privada com a ponte (Heltec V3 e T1000-E); o envio de `@contato` a um favorito do rádio;
  o túnel entre um Heltec V3 e um T1000-E (`bitchat1.0`), com a conversa privada entre os dois
  celulares funcionando.
- **Visto em aparelho com a `bitchat1.1`:** a pessoa do outro lado aparecia na lista só de vez em
  quando, e a conversa privada demorava ou parava no meio da abertura. A `bitchat1.2` corrige (abaixo).
- **Não ensaiado em aparelho:** as mudanças desta versão, a resposta de um contato pelo
  `@contato`, o túnel com repetidores no caminho, a hora pelos anúncios do LoRa, os pagamentos Cashu
  e os demais aparelhos da
  [lista](https://github.com/EduChacon/Meshcore-Bitchat/blob/main/docs/bitchat/DEVICES.md). Não há
  medições de alcance, consumo ou duração.
- **Testes no computador:** 1056 verificações do núcleo, 223 da criptografia das conversas privadas
  (conferida com uma implementação independente do Noise), 8146 dos pagamentos Cashu e 3269 do fluxo
  completo, com celulares e rádio simulados. Compilados no GitHub os 27 aparelhos da versão e o
  WadaMesh.

## O que a ponte faz

- **Texto público:** o `#mesh` do Bitchat vai ao canal `#bitchat` do MeshCore como `apelido: texto`,
  e o canal chega aos celulares como `[MC nome] texto`. O aplicativo MeshCore e o Bitchat usam o
  rádio ao mesmo tempo, e a ponte liga sozinha.
- **Conversa privada com a ponte:** no Bitchat, a ponte aparece como `LoRa bridge` e quatro
  caracteres; ali basta a palavra (`ajuda`, `bateria`, `quem`, `teste`, `recado`, `sos`, `ponte`).
- **Mensagem a um contato do rádio:** na conversa com a ponte, `@Fulano texto` vai ao contato pelo
  LoRa, só se ele for favorito do dono; a resposta volta à conversa. A ponte lê o texto.
- **Túnel:** celulares perto de pontes diferentes aparecem um para o outro no Bitchat e conversam no
  privado, cifrado de ponta a ponta. **Vem ligado**; o dono desliga com `!ponte túnel off`. Com ele
  ligado, o apelido de quem está perto da ponte vai pelo LoRa (ver
  [privacidade](https://github.com/EduChacon/Meshcore-Bitchat/blob/main/docs/bitchat/PRIVACY.md)).
- **Funções da casa:** recado para quem chega, bateria, rádios ao alcance, socorro com posição,
  teste de alcance, hora pelo GPS e pelos anúncios do LoRa, pagamentos Cashu (experimental).
- **Ajuda:** `!ponte ajuda` (ou `!bridge help`) no aplicativo MeshCore; `ajuda` (ou `help`) na
  conversa privada com a ponte. Cada língua tem a ajuda completa.

## Novidades na `bitchat1.2`

- **Túnel: a pessoa do outro lado fica na lista.** O Bitchat tira da lista, 60 segundos depois, quem
  chega retransmitido, e não manda mensagem privada a quem não está na lista: a mensagem ficava na
  fila, e a abertura da conversa podia parar no meio. O túnel repetia a presença de cada celular só a
  cada 3 minutos, e só quando o próprio celular se anunciava. Agora, enquanto houver celular perto
  das duas pontes, a própria ponte manda a presença a cada 40 segundos, com o anúncio mais recente
  do celular: um celular por ponte na configuração de rádio brasileira, até dois nas mais rápidas,
  conforme o tempo de ar. Sem ninguém do outro lado, volta ao ritmo lento, e sem celular por perto
  nada vai.
- **Custo:** cerca de 5% do tempo do canal por celular no ritmo rápido (estimativa), repetido por
  cada repetidor que encaminha. A presença tem a menor prioridade, só começa com folga para todos os
  seus pedaços no limite de rádio da ponte (10% em média), guardando reserva para as conversas e para a
  abertura delas, e não segura o texto público enquanto espera.
- **Revisões independentes** do código novo antes da publicação; os problemas apontados foram
  corrigidos e os principais ganharam testes.

## O que veio na `bitchat1.1`

- **Túnel: a conversa abre mais depressa.** O Bitchat do iPhone desiste de abrir uma conversa
  privada em cerca de 10 segundos, e pelo túnel a abertura vai e volta pelo LoRa. A ponte esperava
  1,5 segundo entre dois envios e às vezes mandava antes o anúncio do celular; agora os pedaços da
  abertura saem assim que o rádio fica livre, e o anúncio só vai antes quando a outra ponte pode ter
  esquecido o celular (depois de 4 minutos).
- **Contadores do túnel:** o `!ponte` mostra `Tunnel: out N, in M, far phones K` (pedaços enviados,
  pacotes recebidos e celulares do outro lado).
- **Configurações pelo Bitchat:** um comando de configuração escrito no `#mesh` (por exemplo
  `!ponte túnel off`) recebe agora o aviso de que só o dono do rádio muda isso, como já acontecia na
  conversa privada; antes recebia o estado da ponte.
- **Publicada como versão normal**, não mais como pré-lançamento.
- Documentos: guia, roteiro e lista de aparelhos com o resultado do ensaio do túnel.

## O que veio na `bitchat1.0`

- **Túnel ligado de fábrica**, com aviso no guia e na resposta de `!ponte túnel on`. Também em quem
  atualiza e nunca mexeu nessa escolha.
- **Túnel com a ponte em reserva.** Quando duas pontes se ouvem pelo Bluetooth, uma fica de reserva
  para o texto público, para ele não ir duas vezes ao LoRa. Até a `bitchat0.25`, a reserva também
  parava o túnel dessa ponte, e a conversa entre as casas não abria (visto em aparelho). Agora a
  reserva vale só para o texto público.
- **Hora por um rádio só.** Sem nenhuma hora, a ponte adota a do primeiro anúncio do LoRa que ouvir,
  como hora provisória; dois rádios que concordam, ou um celular, a substituem.
- **Ajuda e guia revistos:** a ajuda do dono em cinco mensagens, completa em português
  (`!ponte ajuda`) e em inglês (`!bridge help`); o guia diz se o túnel está ligado e não fala mais
  que mensagens privadas não atravessam; na conversa privada, a ajuda sai na língua da palavra.
  Palavras novas em português: `ajuda`, `economia on/off`, `prefixo on/off`. O `!ponte` mostra se o
  túnel está ligado.
- **Documentos revistos:** guia com diagramas, privacidade (agora com o túnel), roteiro de ensaio.

## Versões anteriores

O que mudou da `bitchat0.1` à `bitchat0.25`:
[HISTORY.md](https://github.com/EduChacon/Meshcore-Bitchat/blob/main/docs/bitchat/HISTORY.md).

## Arquivos

**Confira o nome do seu aparelho antes de gravar.**

- ESP32: `<aparelho>_bitchat-merged.bin` (rádio novo ou vindo de outro firmware; apagar a memória e
  gravar no endereço `0x0`) e `<aparelho>_bitchat.bin` (rádio que já roda o MeshCore _companion_
  Bluetooth; gravar no endereço `0x10000`, mantendo os dados).
- nRF52 (T1000-E, RAK4631, T-Echo, T114, ThinkNode M1, Xiao nRF52): `<aparelho>_bitchat.uf2`,
  copiado para a unidade que o aparelho mostra em modo de gravação; o `.zip` é o pacote para a
  ferramenta de atualização da Nordic.
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
- Nos nRF52, uma mensagem privada com mais de cerca de 150 caracteres não chega à ponte.
- Os ESP32-C6 não têm arquivo nesta versão.
