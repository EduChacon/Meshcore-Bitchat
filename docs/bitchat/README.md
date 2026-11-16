# Ponte Bitchat no MeshCore

Firmware _companion_ do MeshCore com uma ponte embutida para o aplicativo
[Bitchat](https://github.com/permissionlesstech/bitchat). Um telefone com o Bitchat, perto do
rádio, conversa com quem está na malha MeshCore, e o contrário, sem internet e sem servidor.

**Estado em 2026-11-16.** No Heltec V3, com a versão `bitchat0.1`, o autor seguiu o roteiro de
instalação e de primeiro uso abaixo e relatou que tudo funcionou. O relato não traz medições:
alcance, consumo, memória livre e duração não foram medidos. Tudo o que veio depois da
`bitchat0.1` (ligação entre as redes, modo econômico, guia, pacotes por país, histórico, lista de
nomes e, na `bitchat0.11`, prefixo, recado, rádios, bateria, socorro, teste de alcance e hora pelo
GPS) só foi simulado e compilado. Os demais aparelhos da [lista](DEVICES.md) nunca foram
ligados. Trate como versão experimental.

O que está pronto, o que vem em seguida e o que ainda é estudo: [roteiro](ROADMAP.md).
O ensaio em aparelho combinado antes da versão 1.0: [roteiro de ensaio](TESTING.md).

## O que faz

a) **Bitchat → MeshCore.** Texto público do `#mesh` do Bitchat sai no canal `#bitchat` do
MeshCore como `apelido: texto`, que é o formato nativo das mensagens de canal. Quem usa o
aplicativo MeshCore vê o apelido como remetente.

b) **MeshCore → Bitchat.** Texto do canal `#bitchat` chega aos telefones Bitchat próximos como
`[MC nome] texto`, sob a identidade da ponte (`LoRa bridge xxxx`).

c) **Entre telefones Bitchat.** A ponte também repassa por Bluetooth o que um telefone envia aos
outros conectados a ela, como um participante comum da malha Bitchat.

d) **Os dois aplicativos ao mesmo tempo.** O aplicativo MeshCore e o Bitchat usam o rádio juntos,
inclusive no mesmo telefone. Não há modo a trocar por botão.

e) **Sem configuração.** Na primeira partida a ponte liga sozinha e cria o canal `#bitchat`. O
canal é do tipo "hashtag": a chave deriva do nome, de modo que qualquer pessoa entra nele pelo
aplicativo MeshCore digitando `#bitchat`. **Não é um canal reservado.**

Só texto público é levado de um lado ao outro. Mensagens privadas do Bitchat, arquivos, áudio e
pagamentos não atravessam a ponte.

## Aparelhos

| Aparelhos                                                | Estado                                           |
| -------------------------------------------------------- | ------------------------------------------------ |
| Heltec V3 (ESP32-S3, SX1262)                             | Funcionou no ensaio do autor                     |
| Outros ESP32, ESP32-S3 e ESP32-C3 da [lista](DEVICES.md) | Compilam; nenhum foi ligado                      |
| ESP32-C6                                                 | Fora: a biblioteca Bluetooth usada não os atende |
| M5Stack Cardputer ADV com Cap LoRa-1262                  | Compila; não foi ligado. [Notas](CARDPUTER.md)   |
| LilyGo T-Deck e T-Deck Plus (sem uso do teclado)         | Compila; não foi ligado. [Notas](TDECK.md)       |
| SenseCAP T1000-E (nRF52)                                 | Experimental; não foi ligado. [Notas](T1000E.md) |
| Outros nRF52 (RAK4631 etc.)                              | Fora por ora (ver "O que falta")                 |

Há um arquivo por aparelho, com o mesmo nome do firmware _companion_ Bluetooth original e o final
`_bitchat`. Nos aparelhos sem memória PSRAM (ESP32 clássico e C3) a ponte pode recusar-se a ligar
por falta de memória e avisar `Low memory: bridge OFF`; o rádio segue como MeshCore comum.

## Arquivos por país

A página de cada versão traz, além dos arquivos gerais, um pacote por país:

| Pacote                 | Frequência  | Banda    | SF  | CR  | _Hash_ do caminho |
| ---------------------- | ----------- | -------- | --- | --- | ----------------- |
| `Brasil`               | 923,125 MHz | 62,5 kHz | 8   | 8   | 2 bytes           |
| `EUA`                  | 910,525 MHz | 62,5 kHz | 7   | 5   | 1 byte            |
| `Europa-e-Reino-Unido` | 869,618 MHz | 62,5 kHz | 8   | 8   | 1 byte            |
| `Rede-propria-918-MHz` | 918,000 MHz | 62,5 kHz | 8   | 8   | 2 bytes           |

Os valores de rádio são os das predefinições do aplicativo oficial (`Brazil`, `USA` e
`EU/UK (Narrow)`), lidas em <https://api.meshcore.nz/api/v1/config> em 2026-11-15; os 2 bytes do
Brasil seguem a [comunidade MeshCore Brasil](https://meshcore.com.br/valores-de-referencia/).

a) Dentro de cada pacote há uma pasta com os arquivos de todos os aparelhos, as somas de
conferência e um `LEIA-ME.txt`.

b) O programa é o mesmo dos arquivos gerais. Muda só o rádio com que um aparelho **nunca
configurado** começa. Gravado do zero com o pacote do país, o rádio já sai na frequência certa e
**não precisa do aplicativo MeshCore**: a ponte cria o canal e toma a hora do primeiro telefone
Bitchat.

c) Na atualização (arquivo sem `merged`, ou T1000-E que já rodava MeshCore), a configuração que
o aparelho tinha é mantida, inclusive a frequência antiga.

d) Os arquivos gerais continuam saindo em 869,618 MHz, 62,5 kHz, SF 8, CR 5, como o MeshCore
original, e pedem o ajuste da frequência pelo aplicativo.

e) Transmitir em frequência não permitida no lugar de uso pode ser proibido. O cumprimento da
regra local é de quem usa o rádio.

f) **`Rede-propria-918-MHz`** não fala com a rede da comunidade (923,125 MHz). É para casas sem
repetidor por perto: todos os rádios gravados com ele repassam o que ouvem (`Ativar Modo de
Repetição` já vem ligado) e formam uma malha à parte. O MeshCore só permite essa repetição em
433, 869,495 e 918 MHz; 918 MHz está na faixa de 915 a 928 MHz usada no Brasil. Repassar ocupa
mais tempo de transmissão e gasta mais bateria.

Os pacotes por país compilam, e o ajuste foi conferido no arquivo gerado; **nenhum foi ligado em
aparelho**.

## Instalação

Cada aparelho tem dois arquivos na página da versão: `<aparelho>_bitchat.bin` e
`<aparelho>_bitchat-merged.bin`. **Gravar o arquivo de outro aparelho pode impedir o rádio de
ligar**; confira o nome.

a) **Rádio com outro firmware (Meshtastic, por exemplo) ou novo:** apagar a memória e gravar o
arquivo `merged` no endereço `0x0`. Tudo o que havia no rádio é perdido.

b) **Rádio que já roda o MeshCore _companion_ Bluetooth:** gravar o arquivo sem `merged` no
endereço `0x10000`. Contatos, canais e identidade do rádio são mantidos.

### Pelo navegador

Este caminho não foi conferido pelo autor; o do terminal, sim. No Chrome ou no Edge, em um computador, abrir <https://espressif.github.io/esptool-js/>, conectar
o rádio pelo cabo USB e clicar em **Connect**. Para o caso (a): **Erase Flash**, depois endereço
`0x0`, escolher o arquivo `merged` e **Program**. Para o caso (b): endereço `0x10000`, escolher o
arquivo sem `merged` e **Program**, sem apagar. Ao fim, apertar o botão `RST` do rádio.

### Pelo terminal

Com o [esptool](https://docs.espressif.com/projects/esptool/) instalado (`pip install esptool`).
Cópia de segurança completa do que está no rádio, para poder voltar (tamanho de 8 MB, o do Heltec
V3; outros aparelhos têm outros tamanhos):

```bash
python -m esptool --chip esp32-s3 -b 921600 read_flash 0x0 0x800000 backup.bin
```

Caso (a):

```bash
python -m esptool --chip esp32-s3 erase_flash
```

```bash
python -m esptool --chip esp32-s3 write_flash 0x0 Heltec_v3_companion_radio_ble_bitchat-merged.bin
```

Caso (b):

```bash
python -m esptool --chip esp32-s3 write_flash 0x10000 Heltec_v3_companion_radio_ble_bitchat.bin
```

Em aparelhos com outro chip, trocar `esp32-s3` por `esp32` ou `esp32-c3`.

**Volta ao original:** gravar o firmware oficial pelo [gravador do MeshCore](https://flasher.meshcore.io),
ou a cópia de segurança no endereço `0x0`.

## Primeiro uso

1. Ligar o rádio. O nome Bluetooth continua `MeshCore-…`.
2. Abrir o aplicativo MeshCore e conectar. O PIN aparece na tela do rádio. **Se o telefone já
   estava pareado com esse rádio**, pode ser preciso removê-lo nos ajustes de Bluetooth do
   telefone e parear de novo: este firmware usa outra pilha Bluetooth, com outro registro de
   pareamentos.
3. Conferir a frequência e a predefinição de rádio no aplicativo, como em qualquer MeshCore. A
   ponte não altera isso.
4. Conferir se existe o canal `#bitchat`. Enviar `!bridge` nele: a resposta chega no próprio canal,
   como mensagens de `bridge`.
5. Abrir o Bitchat perto do rádio. No primeiro minuto depois de ligado, o rádio ainda se anuncia
   como o firmware original e o Bitchat não o encontra; o estado mostra `Bitchat starts: N s`.
   Passado esse minuto, esperar mais um e enviar `!bridge` de novo: `Bitchat` e `people` devem
   passar de zero.
6. Escrever no `#mesh` do Bitchat. O texto deve aparecer no canal `#bitchat` do aplicativo
   MeshCore deste rádio e dos outros da malha que tenham o canal.

Sem o aplicativo MeshCore por perto, a ponte funciona do mesmo modo: ela toma a hora do
primeiro telefone Bitchat que se apresenta. A frequência do rádio, porém, precisa estar certa:
ou ajustada antes pelo aplicativo, uma vez, ou vinda de um [arquivo do país](#arquivos-por-país).

### Várias casas, cada uma com um rádio

a) **Pela rede existente:** o rádio de conversa do MeshCore não repassa mensagens de outros;
quem repassa são os repetidores. As casas conversam se cada uma alcança um repetidor, ou se os
rádios se alcançam diretamente.

b) **Rede própria:** o MeshCore deixa o rádio de conversa repassar mensagens (`Ativar Modo de
Repetição`) somente em 433,000, 869,495 e 918,000 MHz. O pacote `Rede-propria-918-MHz` já sai
assim, sem o aplicativo: todos os rádios gravados com ele formam uma malha à parte, fora da rede
dos repetidores. Não foi ensaiado.

c) Só o texto público do `#mesh` atravessa o LoRa. As mensagens privadas do Bitchat ficam no
Bluetooth. Para que quem está fora do grupo não leia o LoRa, use um canal com chave própria; ver o
[estudo de privacidade](PRIVACY.md).

## Comandos

Digitados no aplicativo MeshCore, em qualquer canal do rádio que tem a ponte. São respondidos só
nesse aplicativo e **não são transmitidos**.

| Comando                 | Efeito                                                                         |
| ----------------------- | ------------------------------------------------------------------------------ |
| `!bridge`               | Mostra o estado, os contadores e uma linha por conexão Bluetooth               |
| `!bridge on`            | Liga a ponte                                                                   |
| `!bridge off`           | Desliga a ponte; o rádio segue como MeshCore comum                             |
| `!bridge local`         | Só Bluetooth: os telefones Bitchat se falam pelo rádio, nada vai ao LoRa       |
| `!bridge lora`          | Passa a usar o canal em que o comando foi digitado (o canal `Public` não vale) |
| `!bridge link on`       | Liga a passagem de texto vindo de pontes Meshtastic (padrão)                   |
| `!bridge link off`      | Desliga essa passagem; o texto delas fica só no Bitchat                        |
| `!bridge economy on`    | Liga o modo econômico (ver abaixo)                                             |
| `!bridge economy off`   | Desliga o modo econômico (padrão)                                              |
| `!bridge reset`         | Derruba as conexões Bluetooth para os telefones lerem os serviços de novo      |
| `!bridge who`           | Lista os nomes ouvidos no canal LoRa nas últimas seis horas, em inglês         |
| `!ponte quem`           | A mesma lista, em português                                                    |
| `!bridge guide`         | Explica a ponte em quatro mensagens curtas, em inglês                          |
| `!ponte guia`           | O mesmo guia, em português                                                     |
| `!bridge prefix on`     | Só o texto do `#mesh` iniciado por `!lora` vai ao LoRa (ver abaixo)            |
| `!bridge prefix off`    | Todo o texto público do `#mesh` vai ao LoRa (padrão)                           |
| `!ponte recado <texto>` | Deixa o recado da casa (ver abaixo); sem texto, mostra o recado atual          |
| `!ponte recado apagar`  | Apaga o recado (`!bridge note off` em inglês)                                  |
| `!ponte bateria`        | Tensão e carga aproximada da bateria do rádio                                  |
| `!ponte radios`         | Rádios ouvidos no LoRa nas últimas 24 horas, do mais recente ao mais antigo    |
| `!ponte sos <texto>`    | Pedido de socorro ao LoRa, com a posição do rádio, se ele a tiver              |
| `!ponte teste`          | Teste de alcance com as outras pontes (ver abaixo)                             |
| `!bridge help`          | Lista os comandos                                                              |

`!ponte` vale no lugar de `!bridge`, e a resposta sai em português quando se usa `!ponte` ou a
palavra em português (`recado`, `bateria`, `socorro`, `teste`). Em inglês: `note`, `battery`,
`radios`, `sos`, `test`.

No Bitchat, escritos no `#mesh`, valem todos, menos os que mudam a configuração: o estado
(`!bridge`), os nomes (`!ponte quem`), o guia (`!ponte guia`, no máximo uma vez a cada 30
segundos), o recado, a bateria, os rádios, o socorro e o teste. As respostas vão a todos os
telefones próximos, uma a cada cinco segundos. Os comandos que mudam a configuração (`on`, `off`,
`local`, `lora`, `link`, `economy`, `prefix`, `reset`) só valem no aplicativo MeshCore; no Bitchat,
recebem o estado como resposta. O guia diz se o texto está indo ao LoRa ou não.

O `/who` do Bitchat só mostra quem está na malha Bluetooth; quem fala pelo LoRa não aparece ali.
`!bridge who` cobre essa falta: lista até doze nomes, do mais recente ao mais antigo, e, quando
nem todos cabem em uma mensagem, informa quantos ficaram de fora (`+3`). A lista fica na memória
do rádio e some ao desligar.

## Funções da casa (a partir da `bitchat0.11`)

Todas foram simuladas e compiladas; nenhuma foi ensaiada em aparelho.

a) **Só com `!lora`.** Com `!bridge prefix on`, o texto do `#mesh` só vai ao LoRa quando começa por
`!lora ` (a marca é retirada antes do envio). O resto fica entre os telefones da casa. O texto que
outras pontes trazem segue a regra de `!bridge link`, não esta. A escolha fica gravada; `!bridge`
mostra `(!lora)` ao lado do canal, e o guia passa a explicar a marca.

b) **Recado da casa.** `!ponte recado Jantar às 20h` guarda um recado de até 55 bytes, com o nome
de quem o deixou (o nome do rádio, se veio do aplicativo MeshCore; o apelido, se veio do Bitchat).
Cada telefone Bitchat que chega e se anuncia diretamente ao rádio recebe o recado uma vez a cada
12 horas, só ele, numa mensagem que os outros telefones não repassam. O recado fica gravado e
sobrevive ao reinício; um recado novo vai de novo a todos. No Bitchat, o recado só é trocado por um
telefone ligado diretamente ao rádio (não por um que chega pela malha de outros telefones), no
máximo uma vez a cada dez minutos, porque cada troca é gravada na memória do rádio. **Qualquer
pessoa perto do rádio, com o Bitchat, pode trocar ou apagar o recado.**

c) **Rádios ao alcance.** `!ponte radios` lista os contatos MeshCore com atividade nas últimas 24
horas, pelo relógio do rádio, com o número de saltos quando a rota é conhecida (`direto`,
`2 saltos`). O MeshCore não guarda o sinal de cada contato, de modo que a lista não traz SNR.
**Só no aplicativo MeshCore do próprio rádio.** A atividade de um contato inclui mensagens
privadas trocadas com ele, de modo que a lista diria a estranhos com quem o dono conversa e
quando; no Bitchat, o comando responde que a lista fica no aplicativo.

d) **Bateria.** `!ponte bateria` responde com a tensão e uma carga aproximada, calculada por uma
curva típica de bateria de lítio, que não vale para outras baterias. Quando a tensão cai abaixo
de 3,45 V com algum telefone conectado, a ponte avisa no `#mesh` uma vez (`Bateria baixa /
Battery low`); o aviso volta a valer depois que a tensão passar de 3,6 V. Rádio que não mede a
bateria responde que não informa a carga e não avisa.

e) **Pedido de socorro.** `!ponte sos caí na trilha` envia ao LoRa `apelido: SOS: caí na trilha`.
Digitado no aplicativo MeshCore do próprio rádio, leva a posição que o rádio tiver (definida no
aplicativo ou dada pelo GPS), como `(-23.55052, -46.63331)`. Pedido pelo Bitchat, que qualquer
pessoa por perto usa, só leva a posição se o rádio já a divulga nos anúncios (ajuste de
localização do aplicativo MeshCore); senão, vai sem ela. Sem texto, vai `pedido de socorro`. Vale
com o prefixo ligado e está limitado a um a cada cinco minutos. **A posição, quando vai, chega a
todos os que têm o canal.** Não substitui serviço de emergência: o LoRa não confirma a entrega, e
não se sabe se alguém está ouvindo.

f) **Teste de alcance.** `!ponte teste` envia `nome: #ponte-teste 1234`. Cada ponte MeshCore que
ouvir responde uma vez com o sinal com que ouviu (`#ponte-teste 1234 ouvido -4.5 dB`; `? dB`
quando a mensagem chegou por um repetidor, porque o sinal medido é o do repetidor), e quem
pediu recebe, em até dois minutos, `Teste 1234: Casa Ana ouviu com -4,5 dB; a volta chegou com
-6,0 dB.`, ou o aviso de que ninguém respondeu. As linhas do teste não vão aos telefones como
texto comum, mas aparecem no aplicativo MeshCore de quem tem o canal. Um teste por vez.
Um `!ponte sos` ou `!ponte recado` digitado em **outro** rádio chega aos telefones como texto comum
dessa pessoa; só o rádio da ponte obedece aos comandos digitados nele.

g) **Hora pelo GPS.** Em rádio com GPS desligado, se em 30 segundos nada tiver dado a hora (nem o
aplicativo, nem um telefone), a ponte liga o GPS por até dez minutos; o próprio MeshCore acerta o
relógio com o GPS. Quando a hora fica certa, ou ao fim dos dez minutos, o GPS volta a ficar
desligado. Sem hora, tenta de novo a cada seis horas. O GPS **não** é ligado quando o rádio está
configurado para incluir a posição nos anúncios, para que a posição do GPS não substitua a que o
operador escolheu divulgar. Ao desligar o GPS, a ponte devolve ao rádio a posição que ele tinha
antes: a posição do GPS não fica guardada.

## Histórico para quem chega depois

O Bitchat guarda as mensagens públicas recentes e entrega a quem chega as que faltam; é o que o
aplicativo mostra como "ouvido aqui antes". A ponte participa disso com o texto que traz do LoRa.

a) O texto do canal é assinado e guardado mesmo sem nenhum telefone conectado. Quando um telefone
chega, o próprio Bitchat pergunta o que falta e a ponte entrega, três mensagens por vez, da mais
nova para a mais antiga; o telefone repete a pergunta a cada 15 segundos, e o restante segue.

b) Ficam guardadas as últimas 24 mensagens (8 no T1000-E), por até seis horas, na memória do
rádio. Desligar o rádio, ou `!bridge off`, apaga tudo.

c) A partir da `bitchat0.10`, entra também o que os telefones escrevem no `#mesh` perto do rádio,
com a assinatura de quem escreveu: a ponte faz o papel de um telefone que fica em casa. Quem
chega depois só vê essas mensagens se já conheceu o autor antes; o Bitchat aceita o anúncio de
uma pessoa por 15 minutos e, depois disso, usa as identidades que guardou. Mensagens com 100
bytes ou mais (que o Bitchat compacta) não são guardadas. As últimas mensagens, das duas
origens, dividem a mesma tabela.

d) Depois de religado sem ninguém por perto, o rádio fica sem hora e não consegue assinar: o
texto que chegar até o primeiro telefone (ou o aplicativo MeshCore) aparecer se perde, como antes.

e) `!bridge` mostra `Kept N`, o número de mensagens guardadas.

O formato do pedido foi escrito a partir do código do aplicativo e conferido com valores
calculados à parte, mas **não foi ensaiado com o aplicativo real**.

## Consumo e modo econômico

Com a ponte ligada, o rádio gasta mais: anuncia-se por Bluetooth várias vezes por segundo, mantém
ligação com cada telefone Bitchat próximo e transmite pelo LoRa o que leva do Bitchat. O consumo não
foi medido.

a) **Modo econômico, opcional:** vem desligado. `!bridge economy on` liga, `!bridge economy off`
desliga, e a escolha fica gravada. Ligado, depois de três minutos sem nenhum telefone Bitchat falando
com o rádio, ele passa a se anunciar cerca de uma vez por segundo e a ponte examina as filas com menos
frequência. O primeiro telefone que falar traz o ritmo normal de volta. `!bridge` mostra `Economy off`,
`Economy on` (ligado, em ritmo normal) ou `Economy active` (ligado, em ritmo lento).

Enquanto o ritmo está lento, o Bitchat pode demorar mais para encontrar o rádio. O aplicativo MeshCore
também: passado o primeiro minuto depois de ligar o Bluetooth, ele acha o rádio pelo mesmo anúncio.
Depois de conectado, o uso é o normal. Nem o ganho de bateria nem essa demora foram medidos.

b) **Ponte desligada:** `!bridge off` desliga tudo o que é da ponte, e a escolha fica gravada. É o
recomendado quando a ponte não está em uso. `!bridge on` religa.

## Ligação com o Meshtastic

Quando uma ponte deste projeto e uma ponte [Meshtastic–Bitchat](https://github.com/EduChacon/meshtastic-bitchat)
estão ao alcance Bluetooth dos mesmos telefones Bitchat, as duas redes LoRa passam a conversar
pelo Bitchat, sem ajuste algum:

- o que a ponte Meshtastic traz ao Bitchat, `[MT nome] texto`, sai no canal do MeshCore como
  `MT nome: texto`;
- no sentido contrário, a ponte Meshtastic (a partir da `1.0.0-beta.6`) leva `[MC nome] texto` à
  rede dela como `MC nome: texto`.

Cada texto atravessa **uma vez só**: o que já veio da outra rede chega marcado e nenhuma ponte o
leva de volta. É preciso haver ao menos um telefone com o Bitchat aberto entre as duas pontes,
porque elas não se conectam uma à outra. `!bridge link off` desliga a passagem neste rádio.

Limites dessa ligação: com mais de um lugar ligando as mesmas duas redes, o mesmo texto pode
aparecer repetido; e ela **não foi ensaiada em aparelho**, só em simulação.

## Na tela do rádio

Nos rádios com tela há uma página **Bitchat bridge** entre as páginas iniciais, depois da de
Bluetooth. Ela mostra o estado, quantos telefones Bitchat estão falando com o rádio, quantas
pessoas são conhecidas e quantos textos foram e vieram pelo LoRa. Nessa página, o toque longo no
botão (ou Enter, nos aparelhos com teclas) troca o modo, nesta ordem: `Bitchat + LoRa`,
`Bitchat only`, `Off`. Ao voltar para `Bitchat + LoRa` por esse caminho, o canal usado é o
`#bitchat`.

## Limites conhecidos

a) Cada mensagem MeshCore leva até 160 bytes, remetente incluído. Texto maior do Bitchat é
dividido em até três partes (`apelido (1/3): …`); acima disso é recusado, não cortado.

b) No sentido contrário, cada mensagem para o Bitchat leva até 96 bytes, e o texto é dividido em
até cinco partes.

c) Até três conexões Bluetooth ao mesmo tempo, somando o aplicativo MeshCore e os telefones
Bitchat.

d) A ponte se limita a 10% do tempo de transmissão, além dos limites do próprio MeshCore. Com o
rádio ocupado, o texto espera até dois minutos e depois é descartado e contado em `lost`.

e) Duas pontes que se ouvem por Bluetooth e usam o mesmo canal: a de número maior fica em espera,
para não duplicar texto. Duas pontes que não se ouvem por Bluetooth podem levar o mesmo texto do
Bitchat duas vezes ao LoRa.

f) O nome anunciado no Bluetooth é abreviado para 11 caracteres enquanto a ponte está ligada
(`MeshCore-1A` em vez de `MeshCore-1A2B3C4D`), porque o anúncio leva os dois serviços. Não foi
verificado se o aplicativo MeshCore encontra o rádio em todos os telefones com esse anúncio.
Se não encontrar, reiniciar o rádio e conectar no primeiro minuto, em que o anúncio é o
original; a conexão feita nesse minuto continua depois dele.

g) A página da ponte na tela existe só na interface mais comum dos rádios com tela (`ui-new`). Em
aparelhos com outra interface, ou sem tela, o estado é lido por `!bridge`.

h) A ponte não acerta o relógio do firmware por conta própria. Sem o aplicativo MeshCore, as
mensagens que vêm do Bitchat saem com a hora certa, mas o resto do MeshCore segue com a hora que
tiver, salvo em rádio com GPS, que o próprio MeshCore acerta quando a ponte liga o GPS (item g
das funções da casa).

## O que muda em relação ao firmware original

a) **Pilha Bluetooth.** O original usa a pilha Bluedroid; este usa a NimBLE (biblioteca
NimBLE-Arduino 1.4.3), que permite ao rádio ler o serviço que o aplicativo Bitchat publica no
telefone. Esse é o caminho que funcionou com o Bitchat do iPhone na ponte para Meshtastic do
mesmo autor. O serviço do aplicativo MeshCore (Nordic UART) e o pareamento por PIN são os
mesmos.

b) **Anúncio.** Com a ponte ligada, o rádio continua visível enquanto houver conexão livre, com o
serviço do Bitchat no anúncio e o do MeshCore na resposta de varredura. O original deixa de se
anunciar quando o aplicativo conecta. No primeiro minuto depois de ligar (ou de religar o
Bluetooth pelo botão), o anúncio é igual ao do original: é a garantia de que o aplicativo MeshCore
encontra o rádio.

c) **Serviço do Bitchat sem pareamento.** Qualquer telefone próximo pode conectar e trocar
pacotes do Bitchat, que são públicos e assinados. O serviço do MeshCore continua exigindo
pareamento, e as notificações dele devem ir só a conexões cifradas. **Isso não foi verificado em
aparelho**; o [roteiro de ensaio](TESTING.md) inclui a conferência.

d) **Memória.** A ponte reserva seus blocos ao ligar e os devolve ao desligar; se faltar memória,
ela se desliga e informa `Low memory: bridge OFF`. O consumo real não foi medido.

## Para quem desenvolve

| Onde                                  | O que há                                                       |
| ------------------------------------- | -------------------------------------------------------------- |
| `src/helpers/bitchat/`                | Núcleo da ponte (codec, assinaturas, estado, Bluetooth, texto) |
| `src/helpers/bitchat/BitchatBridge.*` | Ligação com o MeshCore: canal, relógio, comandos, filas        |
| `src/helpers/bitchat/esp32/`          | Interface Bluetooth do _companion_ sobre NimBLE                |
| `examples/companion_radio/`           | Três pontos de contato, todos sob `WITH_BITCHAT_BRIDGE`        |
| `variants/bitchat/platformio.ini`     | Ambientes de compilação                                        |
| `test/bitchat/`, `tools/bitchat/`     | Testes em computador e scripts                                 |

Os arquivos originais do MeshCore só mudam em `MyMesh.h`, `MyMesh.cpp` e `main.cpp`, em blocos
`#ifdef WITH_BITCHAT_BRIDGE`. Sem essa opção, o firmware compila como o original.

Compilar:

```bash
pio run -e Heltec_v3_companion_radio_ble_bitchat -t mergebin
```

Depois de trazer mudanças do MeshCore oficial, refazer a lista de ambientes:

```bash
python tools/bitchat/generate_envs.py
```

Testes em computador (sem aparelho), depois de uma compilação ter baixado as bibliotecas:

```bash
python tools/bitchat/run_tests.py --crypto-dir .pio/libdeps/Heltec_v3_companion_radio_ble_bitchat/Crypto
```

A definição do Cardputer ADV e a interface de teclado vêm do projeto
[MeshCore-Cardputer-ADV](https://github.com/Stachugit/MeshCore-Cardputer-ADV), de Stachu (licença
MIT), como descrito nas [notas](CARDPUTER.md).

O núcleo veio da ponte para Meshtastic do mesmo autor
([EduChacon/meshtastic-bitchat](https://github.com/EduChacon/meshtastic-bitchat), _commit_
`47d4d67`). A base é o MeshCore oficial no _commit_ `75b04ef` (versão 1.18.0).

### O que falta

a) Ensaio em aparelho de todos os modelos além do Heltec V3, e medições no próprio Heltec V3.

b) Os demais nRF52 (RAK4631 e outros): só o T1000-E foi preparado, e ainda não foi ligado; os
outros dependem do resultado desse ensaio.

c) Página da ponte nas outras interfaces de tela (`ui-orig`, `ui-tiny`).

d) ESP32-C6, que depende de outra versão da biblioteca Bluetooth.
