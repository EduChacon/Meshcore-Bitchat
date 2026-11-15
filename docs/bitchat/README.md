# Ponte Bitchat no MeshCore

Firmware _companion_ do MeshCore com uma ponte embutida para o aplicativo
[Bitchat](https://github.com/permissionlesstech/bitchat). Um telefone com o Bitchat, perto do
rádio, conversa com quem está na malha MeshCore, e o contrário, sem internet e sem servidor.

**Estado em 2026-11-14.** No Heltec V3, o autor seguiu o roteiro de instalação e de primeiro uso
abaixo e relatou que tudo funcionou. O relato não traz medições: alcance, consumo, memória livre e
duração não foram medidos, e ele não detalha quais trajetos foram conferidos. Os demais aparelhos
da [lista](DEVICES.md) só foram compilados; nenhum foi ligado. Trate como versão experimental.

O que está pronto, o que vem em seguida e o que ainda é estudo: [roteiro](ROADMAP.md).

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
Repetição`, no aplicativo) somente em 433,000, 869,495 e 918,000 MHz. Todos os rádios nessa
frequência e com a repetição ligada formam uma malha à parte, fora da rede dos repetidores. Esse
arranjo exige o aplicativo uma vez em cada rádio e não foi ensaiado com a ponte.

c) Só o texto público do `#mesh` atravessa o LoRa. As mensagens privadas do Bitchat ficam no
Bluetooth.

## Comandos

Digitados no aplicativo MeshCore, em qualquer canal do rádio que tem a ponte. São respondidos só
nesse aplicativo e **não são transmitidos**.

| Comando               | Efeito                                                                         |
| --------------------- | ------------------------------------------------------------------------------ |
| `!bridge`             | Mostra o estado, os contadores e uma linha por conexão Bluetooth               |
| `!bridge on`          | Liga a ponte                                                                   |
| `!bridge off`         | Desliga a ponte; o rádio segue como MeshCore comum                             |
| `!bridge local`       | Só Bluetooth: os telefones Bitchat se falam pelo rádio, nada vai ao LoRa       |
| `!bridge lora`        | Passa a usar o canal em que o comando foi digitado (o canal `Public` não vale) |
| `!bridge link on`     | Liga a passagem de texto vindo de pontes Meshtastic (padrão)                   |
| `!bridge link off`    | Desliga essa passagem; o texto delas fica só no Bitchat                        |
| `!bridge economy on`  | Liga o modo econômico (ver abaixo)                                             |
| `!bridge economy off` | Desliga o modo econômico (padrão)                                              |
| `!bridge reset`       | Derruba as conexões Bluetooth para os telefones lerem os serviços de novo      |
| `!bridge who`         | Lista os nomes ouvidos no canal LoRa nas últimas seis horas, em inglês         |
| `!ponte quem`         | A mesma lista, em português                                                    |
| `!bridge guide`       | Explica a ponte em quatro mensagens curtas, em inglês                          |
| `!ponte guia`         | O mesmo guia, em português                                                     |
| `!bridge help`        | Lista os comandos                                                              |

`!ponte` vale no lugar de `!bridge`. No Bitchat, escritos no `#mesh`, valem três: `!bridge`, que
devolve o estado; `!bridge who` (ou `!ponte quem`), que devolve os nomes ouvidos no LoRa; e
`!bridge guide` (ou `!ponte guia`), que devolve o guia, no máximo uma vez a cada 30 segundos. As
respostas vão a todos os telefones próximos. O guia diz se o texto está indo ao LoRa ou não.

O `/who` do Bitchat só mostra quem está na malha Bluetooth; quem fala pelo LoRa não aparece ali.
`!bridge who` cobre essa falta: lista até doze nomes, do mais recente ao mais antigo, e, quando
nem todos cabem em uma mensagem, informa quantos ficaram de fora (`+3`). A lista fica na memória
do rádio e some ao desligar.

## Histórico para quem chega depois

O Bitchat guarda as mensagens públicas recentes e entrega a quem chega as que faltam; é o que o
aplicativo mostra como "ouvido aqui antes". A ponte participa disso com o texto que traz do LoRa.

a) O texto do canal é assinado e guardado mesmo sem nenhum telefone conectado. Quando um telefone
chega, o próprio Bitchat pergunta o que falta e a ponte entrega, três mensagens por vez, da mais
nova para a mais antiga; o telefone repete a pergunta a cada 15 segundos, e o restante segue.

b) Ficam guardadas as últimas 24 mensagens (8 no T1000-E), por até seis horas, na memória do
rádio. Desligar o rádio, ou `!bridge off`, apaga tudo.

c) Só entra no histórico o que veio do LoRa ou do aplicativo MeshCore deste rádio. O que os
telefones escrevem entre si não é guardado pela ponte; eles o trocam diretamente.

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

h) O relógio do firmware não é alterado pela ponte. Sem o aplicativo MeshCore, as mensagens que
vêm do Bitchat saem com a hora certa, mas o resto do MeshCore segue com a hora que tiver.

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
pareamento, e as notificações dele só vão a conexões cifradas.

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
