# Pagamentos Cashu pela ponte (experimental, a partir da `bitchat0.20`)

Um token Cashu (dinheiro eletrônico em bitcoin) colado no chat público do Bitchat é levado pela
ponte, pelo LoRa do MeshCore, até as outras pontes do mesmo canal, que o entregam aos celulares perto
delas. Funciona como um "Pix" sem internet no caminho: quem paga e quem recebe podem estar a
quilômetros, cada um perto de uma ponte.

**Nada disso foi ligado em aparelho.** Passou nos testes em computador (descompressão, leitura de
tokens, o túnel e o fluxo completo com telefones e rádio simulados) e compila para os aparelhos
principais.

## Como pagar

a) **Quem recebe** copia, na sua carteira Cashu, a chave pública para pagamentos travados ("P2PK",
também chamada "receber travado" ou "lock to pubkey") e a envia, uma vez, a quem vai pagar.

b) **Quem paga** cria na carteira um token **travado para essa chave**. Criar o token exige contato
com a casa da moeda ("mint"), isto é, internet naquele momento; pode ser feito antes, em casa,
e o token guardado para depois.

c) No Bitchat, no **chat público**, quem paga cola o token (ou usa `/pay <token>`), perto da sua
ponte.

d) A ponte responde no chat, em português:

| Mensagem                                                  | O que quer dizer                                                                    |
| --------------------------------------------------------- | ----------------------------------------------------------------------------------- |
| `Levando 21 sat de Ana pela LoRa (9 partes)…`             | O token saiu; a resposta vem em até cerca de 2 min                                  |
| `✓ 21 sat entregue à ponte Casa (2 cel.). Confirme com…`  | A ponte de destino, contato deste rádio, assinou: entregou aos celulares perto dela |
| `✓? 21 sat entregue, diz a ponte 1a2b (não é contato)…`   | Uma ponte que este rádio não conhece diz ter entregue; confirme com quem recebe     |
| `✗ 21 sat: ponte 1a2b sem celular perto…`                 | Chegou inteiro, mas não havia ninguém com Bitchat lá; nada foi exibido              |
| `✗ 21 sat chegou tarde à ponte 1a2b…`                     | Passou de 2 min: os celulares o recusariam; nada foi exibido                        |
| `✗ A ponte 1a2b recusou 21 sat…`                          | Assinatura ou formato não conferiu; nada foi exibido                                |
| `✗ 21 sat sem confirmação. Se não chegou, o token segue…` | Nenhuma ponte respondeu; pode não ter chegado, ou a resposta se perdeu              |

e) **Do outro lado**, quem recebe vê a mensagem de quem pagou, com o token, e a nota
`21 sat de Ana chegaram pela LoRa. Toque no token para resgatar.` Ao tocar no token, o Bitchat
abre a carteira. Uma carteira que confere a prova da casa da moeda ("DLEQ") aceita o token
travado sem internet; o resgate definitivo, na casa da moeda, acontece quando houver internet.

## O que se garante

a) **Tudo ou nada.** O token vai em partes pelo LoRa, mas só aparece do outro lado inteiro. Uma
parte que se perde é pedida de novo, até duas vezes; se ainda faltar, as partes recebidas são
descartadas e nada aparece. Nenhum pedaço avulso é exibido em nenhum dos lados, nem levado como
texto: a ponte nunca manda um token pelo canal de texto do MeshCore, nem leva ao Bitchat um token
digitado no aplicativo MeshCore.

b) **Nada muda no caminho.** Vão pelo LoRa as próprias mensagens assinadas pelo celular de quem
paga (o seu anúncio e a mensagem do token). A ponte de destino confere as assinaturas antes de
entregar, e os celulares conferem de novo.

c) **A ponte não guarda, não abre e não gasta o token.** Ela só decide se ele pode viajar e conta a
quem paga como foi.

d) **Sem perda de fundos com token travado.** Só a chave de quem recebe gasta um token travado.
Se a entrega falhar, o token continua com quem pagou (na própria mensagem do chat e, nas
carteiras que guardam o que foi enviado, como pendente) e pode ser enviado de novo, por qualquer meio. Se for entregue duas vezes,
continua valendo uma vez só.

e) **Token sem trava não passa por canal aberto.** O canal padrão `#bitchat` tem chave pública
(qualquer um que saiba o nome o escuta). Ali a ponte só leva tokens em que **todas** as provas
estão travadas por chave, sem prazo ou com chave de devolução para depois do prazo. Num canal com
chave própria, combinada entre as casas, a ponte leva também o token sem trava, **mas ele aparece
no chat público do Bitchat perto da ponte de destino, e qualquer pessoa ali pode resgatá-lo
primeiro.** Para não correr esse risco, use sempre token travado.

## Limites conhecidos

a) **Duas pontes desta versão, no mesmo canal LoRa** do MeshCore. Pontes mais antigas ignoram os
pagamentos.

b) **Dois minutos.** O Bitchat recusa mensagens com mais de 2 min de diferença do relógio do
celular. A viagem inteira, com os pedidos de parte perdida, tem de caber nisso. Com o LoRa muito
ocupado, o resultado é `✗`. Por isso também a ponte espera até 45 s pelo próximo anúncio de quem
paga quando o último já está velho.

c) **Só quem está perto da ponte de destino naquele momento recebe.** O token não fica no
histórico para quem chega depois.

d) **Tamanho.** Cabem cerca de 1,8 KB, em até 12 partes: um token `cashuB` com 2 a 4 provas. Um
token maior é recusado com aviso; junte as provas na carteira (trocar por valores maiores) e
reenvie. O formato antigo `cashuA` ocupa mais espaço.

e) **Um pagamento por vez em cada ponte**, e cada um ocupa o LoRa por 15 a 30 s.

f) **A ponte não sabe se a casa da moeda aceita a trava** (NUT-11). Uma casa da moeda que não a
aceite trataria o token travado como sem trava. Use casas da moeda conhecidas.

g) **Carteiras.** Pela leitura do código delas, em 2026-11, Minibits, Macadamia, Nutstash e Sovran
criam e recebem tokens travados e conferem a prova sem internet; o Cashu.me não confere a prova
sem internet. **Nenhuma foi ensaiada com a ponte.**

h) **Outros rádios do canal** recebem as partes como dados de canal (tipo `0xFFFF`) que não sabem
ler. A ponte as esconde do aplicativo ligado a ela; o que outro aplicativo MeshCore faz com elas
não foi verificado.

i) **Bitchat iOS e Android.** O Bitchat comprime mensagens longas e confere a assinatura sobre a sua
própria recompressão. A entrega do outro lado se comporta como a de um celular que estivesse
perto do pagador; se o próprio Bitchat não se entender entre iOS e Android com uma mensagem
longa, a ponte não muda isso.

j) **A confirmação é assinada pelo rádio da ponte de destino** (a partir da `bitchat0.21`), com a
identidade MeshCore dele. A ponte de quem paga confere a assinatura com a chave que conhece pelos
anúncios ("adverts") e só então mostra `✓` com o nome do contato. De uma ponte que não é contato
chega `✓?`, com o número que ela diz ter, e quem paga deve confirmar com quem recebe. Uma
confirmação que diz vir de um contato, mas cuja assinatura não confere, é descartada, e uma
conferida vale mais que qualquer outra. A assinatura prova qual rádio respondeu, não que ele seja a
ponte certa: qualquer rádio que mande um anúncio vira contato. Confira se o nome é o da ponte de
quem recebe.

k) **Uma menção a `cashuA`/`cashuB` que não seja um token legível também fica fora do LoRa**, com o
aviso `Token Cashu ilegível`. Dois tokens na mesma mensagem não vão. Do LoRa para o Bitchat, não
passa texto que nomeie um token nem que tenha 40 ou mais letras e números seguidos, sem espaço (um
pedaço de token cortado); um código longo colado no canal fica de fora pelo mesmo motivo.

## Roteiro de ensaio

Ainda não houve ensaio em aparelho. Este é o mínimo para saber se funciona. Use um valor pequeno
(por exemplo, 21 sat) numa casa da moeda conhecida.

### Material

a) Duas pontes com a `bitchat0.21` ou mais nova: **A**, perto de quem paga, e **B**, perto de quem
recebe, no mesmo canal (o `#bitchat` padrão serve).

b) Dois celulares com o Bitchat e uma carteira Cashu que trave tokens (P2PK): **P**, de quem paga,
e **R**, de quem recebe. **Os dois celulares não podem estar ao alcance do Bluetooth um do outro**
(algumas dezenas de metros, ou paredes no caminho); senão o Bitchat entrega o token diretamente, e o
ensaio não mostra nada. Duas casas são o ideal.

c) O aplicativo MeshCore ligado a cada ponte, para os comandos e para conferir o canal.

### Preparação

1. Gravar a mesma versão nas duas pontes. Em cada uma, `!ponte` no aplicativo MeshCore deve
   responder `Ready: Bitchat + LoRa`.
2. **Uma ponte tem de conhecer a outra.** No aplicativo MeshCore de cada ponte, mandar um anúncio
   ("Advert", por inundação) e conferir se a outra aparece na lista de contatos. Sem isso, a
   confirmação chega como `✓?` (ponte que não é contato).
3. Em R, copiar na carteira a chave pública para receber travado e enviá-la a quem paga, por
   qualquer meio. Em P, com internet, criar um token travado para essa chave. Os nomes dos menus
   variam de carteira para carteira.

### Ensaio

4. **Token sem trava (deve ser recusado).** Em P, colar no chat público um token sem trava de
   1 sat. Esperado: `[bridge] Token sem trava P2PK…` e nada em R.
5. **Pagamento.** Em P, colar o token travado no chat público (ou `/pay <token>`). Esperado, em P:
   `Levando 21 sat de … pela LoRa (n partes)…` e, em até 2 minutos,
   `✓ 21 sat entregue à ponte <nome da B> (1 cel.)…`. Em R: a mensagem de P com o token e
   `21 sat de … chegaram pela LoRa. Toque no token para resgatar.`
6. **Resgate.** Em R, tocar no token e receber na carteira, primeiro sem internet (se a carteira
   permitir) e depois com internet. Anotar o que a carteira disse.
7. **Ninguém do outro lado.** Fechar o Bitchat em R e repetir o passo 5 com outro token. Esperado:
   `✗ …: ponte … sem celular perto. O token segue com você.`
8. **Ponte B desligada.** Desligar B e repetir. Esperado, depois de cerca de 100 s:
   `✗ … sem confirmação…`.
9. **Nada como texto.** Num terceiro rádio MeshCore, ou no aplicativo da ponte A, olhar o canal
   `#bitchat` durante os passos 4 a 8. Esperado: nenhum pedaço de token aparece como mensagem.
10. **Som e luz (T1000-E).** `!ponte som off` e `!ponte luz off`; mandar mensagens do Bitchat e do
    LoRa; reiniciar o rádio. Esperado: nenhum bipe e luz apagada, também depois de reiniciar. Depois,
    `!ponte som on` e `!ponte luz on`.

### O que anotar

Para cada passo: o que apareceu em P e em R (captura de tela), o tempo entre colar o token e a
resposta da ponte, e a resposta de `!ponte` nas duas pontes no fim. Se algo falhar, a captura da
mensagem da ponte basta para localizar a causa na maioria dos casos.

## Para quem desenvolve

- `src/helpers/bitchat/BridgeInflate.*`: DEFLATE puro (RFC 1951), como o Bitchat comprime acima de
  100 bytes.
- `src/helpers/bitchat/BridgeCashu.*`: leitura de tokens NUT-00 (`cashuA` JSON, `cashuB` CBOR) e da
  trava NUT-10/NUT-11 (`P2PK`, `locktime`, `refund`).
- `src/helpers/bitchat/BridgeTunnel.*`: partes de até 160 bytes em datagramas de canal do MeshCore
  (`DATA_TYPE_DEV`, `0xFFFF`), com o cabeçalho `'B' 'C'`, versão e tipo (dados, confirmação,
  pedido de partes), identificador de 4 bytes, índice e total; o pacote é o tamanho do anúncio, o
  anúncio, o tamanho da mensagem e a mensagem. Remontagem em duas vagas, entrega uma só vez.
- `src/helpers/bitchat/BridgePayments.cpp`: regras, mensagens, envio e entrega.
- Testes: `python3 tools/bitchat/run_tests.py --crypto-dir <Crypto 0.4.0> cashu flow`. Os tokens
  oficiais vêm dos vetores de teste NUT-00 (`test/bitchat/cashu_official.json`, licença MIT).
