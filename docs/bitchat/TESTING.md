# Roteiro de ensaio em aparelho

Estado em 2026-11-16. É o ensaio mínimo combinado antes de chamar a ponte de versão 1.0. Vale
para o MeshCore e, com os nomes de comando iguais, para o Saitama (T-Deck Plus e Cardputer ADV).
O roteiro do Meshtastic está no
[repositório dele](https://github.com/EduChacon/meshtastic-bitchat/blob/main/docs/bridge/TESTING.md).

## Material

a) Dois rádios com a ponte, de preferência um Heltec V3 e um T1000-E (ou T-Deck Plus).

b) Dois telefones com o Bitchat e um deles também com o aplicativo MeshCore.

c) Um terceiro rádio MeshCore comum, ou o aplicativo MeshCore em outro telefone, no canal
`#bitchat`, para ver o que sai no LoRa.

d) Para o item 9: o aplicativo **nRF Connect** em um telefone que **não** foi pareado com o rádio.

Anotar, em cada passo, o que apareceu, de preferência com captura de tela. O resultado esperado
está entre parênteses.

## Passos

1. **Gravação.** Gravar a versão nos dois rádios. Ligar e esperar dois minutos. (O aplicativo
   MeshCore encontra o rádio; no canal `#bitchat`, `!bridge` responde com `Ready`.)
2. **Texto nos dois sentidos.** No Bitchat, perto do rádio A, escrever no `#mesh`. (Sai no LoRa
   como `apelido: texto`.) No rádio comum, escrever no `#bitchat`. (Chega ao Bitchat como
   `[MC nome] texto`.)
3. **Sem o aplicativo.** Fechar o aplicativo MeshCore e desligar e religar o rádio A. Abrir só o
   Bitchat. (Em até dois minutos, `!bridge` no `#mesh` responde, e o texto volta a passar.)
4. **Quem chega depois.** Com o telefone 2 longe, mandar três textos pelo rádio comum. Aproximar o
   telefone 2. (Os textos aparecem no Bitchat dele, no trecho do que foi ouvido antes.)
5. **Recado.** No aplicativo do rádio A: `!ponte recado Teste de recado`. Afastar o telefone 2,
   fechar o Bitchat dele, aproximar e abrir. (Recebe `[bridge] Recado de …` uma vez.) Pelo
   Bitchat de um telefone ligado ao rádio: `!ponte recado outro`. (Aceita.)
6. **Funções da casa.** No Bitchat: `!ponte bateria` (tensão plausível), `!ponte radios` (responde
   que a lista fica no aplicativo), `!ponte teste` com o rádio B ligado (resultado com o sinal), e
   `!ponte sos teste` (sai no LoRa **sem** posição, se o rádio não divulga a posição). No aplicativo
   do rádio A: `!ponte radios` (lista) e `!ponte sos teste` (sai com a posição, se houver).
7. **Reinício.** Desligar e religar o rádio A. (`!bridge` mostra as mesmas escolhas de antes;
   `!ponte recado` mostra o recado; no T1000-E, `!bridge economy on` não dá `Storage error`.)
8. **Uma noite ligado.** Deixar o rádio A ligado, com bateria, por oito horas, com o Bitchat de um
   telefone por perto. (De manhã, `!bridge` responde, o texto passa nos dois sentidos, e a bateria
   caiu de modo coerente com o uso.)
9. **Serviço do MeshCore sem pareamento.** Com o aplicativo MeshCore conectado no telefone 1, abrir
   o nRF Connect no telefone 3, conectar ao rádio e tentar ativar as notificações do serviço
   Nordic UART (`6E400001-…`). (O rádio recusa, ou pede o PIN; nenhuma notificação chega sem
   pareamento.) Se chegar alguma notificação, parar e relatar: é falha de segurança.
10. **T1000-E.** Trocar o PIN padrão nos ajustes do aplicativo e conferir que o novo é pedido.

## Critério para a 1.0

Todos os passos com o resultado esperado em pelo menos um ESP32 (Heltec V3) e no T1000-E ou no
T-Deck Plus, sem reinício espontâneo durante o passo 8. Falha em qualquer passo: corrigir e
repetir o roteiro na versão seguinte.
