# Privacidade

O que cada função da ponte expõe, e como reduzir. Escrito a partir do código do Bitchat para iOS e
do código da ponte. Tempos de transmissão são cálculos para o padrão brasileiro do MeshCore
(923,125 MHz, 62,5 kHz, SF 8, CR 4/8), não medições.

## 1. O que é público hoje

| Trecho                                 | Quem pode ler                                              |
| -------------------------------------- | ---------------------------------------------------------- |
| `#mesh` do Bitchat, perto de cada casa | qualquer telefone com Bitchat ao alcance do Bluetooth      |
| Canal `#bitchat` no LoRa (padrão)      | qualquer rádio MeshCore com o canal, cuja chave é pública  |
| Conversa privada entre celulares       | só os dois celulares; pelo túnel, cifrada de ponta a ponta |
| Conversa privada com a ponte           | o celular e a ponte                                        |
| `@contato` (mensagem a um favorito)    | a ponte, o contato e o aplicativo MeshCore do dono         |
| Anúncios do túnel                      | qualquer rádio MeshCore com o canal: apelido e chaves      |

Quem escuta o rádio vê também quem fala e quando, mesmo quando não lê o conteúdo.

Funções que expõem dados, e os limites postos a elas:

- **Socorro (`!ponte sos`):** a posição vai a todos os que têm o canal. Pedido pelo Bitchat, só vai
  se o rádio já divulga a posição nos anúncios ou se o dono ligou `!bridge position on` (trilha);
  digitado no aplicativo do rádio, vai sempre.
- **Rádios (`!ponte radios`):** a lista mostra com quem o rádio teve atividade, inclusive
  mensagens privadas. Por isso só responde no aplicativo do próprio rádio.
- **Recado:** lido por qualquer telefone Bitchat que chega perto do rádio; só um telefone ligado
  diretamente ao rádio pode trocá-lo.
- **Teste de alcance:** mostra a quem tem o canal o nome do rádio e o sinal com que ouviu.
- **Estado (`!bridge` no Bitchat):** número de telefones e de pessoas conhecidas, e contadores do
  LoRa; dá uma ideia de quantas pessoas estão perto do rádio.
- **Túnel (vem ligado):** o apelido e as chaves públicas de cada celular ligado direto à ponte vão
  pelo LoRa, a cada 30 minutos, ou a cada 3 minutos enquanto outra ponte com túnel estiver no ar;
  sem celular por perto, nada vai. Quem escuta o canal sabe que aquele apelido está perto daquela
  ponte, e a chave fixa do Bitchat permite reconhecê-lo em outro dia. O conteúdo das conversas não
  é visível. O dono desliga com `!ponte túnel off`.
- **`@contato`:** a ponte lê o texto e o manda como mensagem direta do MeshCore, cifrada só entre os
  dois rádios; vai apenas aos favoritos do dono.
- **Identidade:** o nome `LoRa bridge xxxx` e o anúncio da ponte trazem parte do número do rádio,
  o que liga a ponte ao nó LoRa. Ao trocar a identidade do nó (importação de chave no MeshCore,
  nova identidade no Saitama), a ponte também passa a ter uma identidade nova.

## 2. Privacidade parcial que já existe

**Canal LoRa com chave própria.** Crie no aplicativo MeshCore um canal privado (com chave
gerada, não um `#nome`), compartilhe-o só com as casas do grupo e, nesse canal, envie
`!bridge lora`. A ponte passa a usar esse canal. Efeito:

- quem está fora do grupo e escuta o LoRa não lê nada;
- o `#mesh` continua público perto de cada casa: um vizinho com o Bitchat lê o que é escrito ali;
- **e o caminho de volta também:** o que chega desse canal é entregue a qualquer telefone Bitchat
  perto de cada casa, inclusive pelo histórico, que guarda as últimas mensagens por seis horas para
  quem chegar depois. O canal privado protege o trecho do LoRa, não o que cada casa recebe.

O mesmo vale para o Meshtastic (`!bridge lora` em um canal secundário com chave própria). É a
recomendação para hoje.

## 3. Privacidade de ponta a ponta: o túnel

O túnel leva, entre pontes, os pacotes cifrados do Bitchat (aperto de mão Noise e mensagens
privadas), sem que as pontes consigam lê-los, e os anúncios dos celulares, para que os dois lados se
conheçam. Usa o datagrama de grupo do MeshCore, no máximo três pedaços por pacote. Custo estimado
por salto:

| Etapa                                         | Transmissões LoRa | Tempo no ar (estimado) |
| --------------------------------------------- | ----------------- | ---------------------- |
| Anúncio de cada celular (a cada 3 ou 30 min)  | 1 a 2             | 1,5 a 3 s              |
| Abertura da conversa (uma vez por par)        | 3                 | cerca de 5 s           |
| Mensagem privada curta (256 bytes no Bitchat) | 1 a 2             | 1,5 a 3 s              |

Só texto: fotos e áudio não passam. Detalhes e limites no [guia](README.md).

## 4. Pagamento (`/pay`)

O Bitchat não guarda dinheiro: `/pay` envia um _token_ Cashu, criado em uma carteira à parte,
como texto da conversa. O _token_ é ao portador (quem resgatar primeiro fica com o valor) e o
resgate exige internet.

- **Token comum pelo canal público: nunca.** Qualquer rádio com o canal leria e poderia resgatar.
  No canal `#bitchat`, a ponte só leva tokens **travados** para a chave de quem recebe ("P2PK"):
  quem lê não consegue gastar. Como fazer: [pagamentos](CASHU.md).
- **Pelo túnel:** um _token_ tem centenas de caracteres; cifrado e com o preenchimento do Bitchat,
  passa do teto do túnel. Os pagamentos pelo LoRa usam um caminho próprio: ver [pagamentos](CASHU.md).
- **Sem carteira Cashu:** mandar um endereço Lightning curto (`nome@carteira`), que é
  um pedido de pagamento, não dinheiro. Quem recebe paga pela própria carteira, com internet.

## 5. "Ponte mesh" nativa do Bitchat

O recurso do aplicativo publica cada mensagem como um evento Nostr assinado (texto JSON com
chaves e assinatura, várias centenas de bytes) e depende de um telefone com internet em cada
grupo. Pelo LoRa, cada mensagem curta custaria de 3 a 5 transmissões, contra 1 do formato atual
(`[MC nome] texto`). **Decisão: não usar pelo LoRa.** A apresentação atual continua.

## 6. Recomendação

1. Para que quem está fora do grupo não leia o LoRa, usar um canal com chave própria (item 2).
2. Para conversas privadas entre casas, o túnel (item 3).
3. Para não divulgar quem está perto da ponte, desligar o túnel: `!ponte túnel off`.
