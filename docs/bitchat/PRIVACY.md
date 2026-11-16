# Privacidade e pagamento pela ponte: estudo

Estado em 2026-11-15. Estudo escrito a partir do código do Bitchat para iOS (versão de
2026-10-23) e do código das duas pontes; nada aqui foi ensaiado em aparelho. Tempos de
transmissão são cálculos para o padrão brasileiro do MeshCore (923,125 MHz, 62,5 kHz, SF 8, CR 4/8),
não medições.

## 1. O que é público hoje

| Trecho                                  | Quem pode ler                                                     |
| --------------------------------------- | ----------------------------------------------------------------- |
| `#mesh` do Bitchat, perto de cada casa  | qualquer telefone com Bitchat ao alcance do Bluetooth             |
| Canal `#bitchat` no LoRa (padrão)       | qualquer rádio MeshCore com o canal, cuja chave é pública         |
| Mensagens privadas e grupos do Bitchat  | só os participantes; hoje **não atravessam** o LoRa               |

Quem escuta o rádio vê também quem fala e quando, mesmo quando não lê o conteúdo.

Funções da `bitchat0.11` e seguintes que expõem dados, e os limites postos a elas na
`bitchat0.12`, depois de uma revisão de segurança:

- **Socorro (`!ponte sos`):** a posição vai a todos os que têm o canal. Pedido pelo Bitchat, só vai
  se o rádio já divulga a posição nos anúncios; digitado no aplicativo do rádio, vai sempre.
- **Rádios (`!ponte radios`):** a lista mostra com quem o rádio teve atividade, inclusive
  mensagens privadas. Por isso só responde no aplicativo do próprio rádio.
- **Recado:** lido por qualquer telefone Bitchat que chega perto do rádio; só um telefone ligado
  diretamente ao rádio pode trocá-lo.
- **Teste de alcance:** mostra a quem tem o canal o nome do rádio e o sinal com que ouviu.
- **Estado (`!bridge` no Bitchat):** número de telefones e de pessoas conhecidas, e contadores do
  LoRa; dá uma ideia de quantas pessoas estão perto do rádio.
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

## 3. Privacidade de ponta a ponta

**No Meshtastic já existe, em laboratório.** O menu `Advanced: tunnel` (só em rádio com tela)
liga um túnel entre pontes que leva, além do texto, os pacotes cifrados do Bitchat: aperto de mão
Noise, mensagens privadas e os tipos de grupo, sem que a ponte consiga lê-los. Os anúncios dos
telefones também passam, a cada 30 s por pessoa, para que os dois lados se conheçam. Limites: cada
pacote do Bitchat vai em até 13 pedaços, com teto de 512 bytes; nunca foi ensaiado.

**No MeshCore não existe.** O caminho seria portar esse túnel, usando o datagrama de grupo do
MeshCore (até 165 bytes úteis por transmissão). Custo estimado por mensagem privada, em cada
salto:

| Etapa                                   | Transmissões LoRa | Tempo no ar (estimado) |
| --------------------------------------- | ----------------- | ---------------------- |
| Anúncio de cada telefone (a cada 30 s)  | 2                 | cerca de 2,5 s         |
| Aperto de mão Noise (uma vez por par)   | 3 a 4             | cerca de 5 s           |
| Mensagem privada curta (256 bytes)      | 2                 | cerca de 3 s           |
| Mensagem privada média (512 bytes)      | 4                 | cerca de 6,5 s         |

Os anúncios a cada 30 s são o custo maior: com três telefones em cada casa, ocupariam o canal
continuamente. Portar exigiria reduzir esse ritmo, e o efeito disso no aplicativo só se vê em
aparelho. **Decisão: não portar antes de ensaiar o túnel do Meshtastic com dois rádios e dois
telefones.**

## 4. Pagamento (`/pay`)

O Bitchat não guarda dinheiro: `/pay` envia um *token* Cashu, criado em uma carteira à parte,
como texto da conversa. O *token* é ao portador (quem resgatar primeiro fica com o valor) e o
resgate exige internet.

- **Pelo canal público da ponte: nunca.** Qualquer rádio com o canal leria e poderia resgatar.
- **Pelo túnel privado:** um *token* tem centenas de caracteres; cifrado e com o preenchimento do
  Bitchat, passa de 512 bytes e excede o teto do túnel. Seria preciso aumentar o teto (mais
  pedaços, mais tempo no ar) e ensaiar.
- **O que funciona hoje pelo LoRa:** mandar um endereço Lightning curto (`nome@carteira`), que é
  um pedido de pagamento, não dinheiro. Quem recebe paga pela própria carteira, com internet.

## 5. "Ponte mesh" nativa do Bitchat

O recurso do aplicativo publica cada mensagem como um evento Nostr assinado (texto JSON com
chaves e assinatura, várias centenas de bytes) e depende de um telefone com internet em cada
grupo. Pelo LoRa, cada mensagem curta custaria de 3 a 5 transmissões, contra 1 do formato atual
(`[MC nome] texto`). **Decisão: não usar pelo LoRa.** A apresentação atual continua.

## 6. Ordem proposta

1. Usar já o canal com chave própria (item 2).
2. Ensaiar o túnel do Meshtastic com dois rádios e dois telefones.
3. Só então decidir o porte para o MeshCore e o aumento do teto para o pagamento.
