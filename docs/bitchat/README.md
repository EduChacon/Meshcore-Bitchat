# Ponte Bitchat no MeshCore

Firmware _companion_ do MeshCore com uma ponte embutida para o aplicativo
[Bitchat](https://github.com/permissionlesstech/bitchat). Um telefone com o Bitchat, perto do
rádio, conversa com quem está na malha MeshCore, e o contrário, sem internet e sem servidor.

**Estado em 2026-11-14: nada aqui foi ligado em aparelho.** O código compila para o Heltec V3 e
passa nos testes em computador (telefones e rádio simulados). Falta todo o ensaio real:
pareamento com o aplicativo MeshCore, encontro pelo Bitchat, troca de mensagens, alcance,
consumo e duração. Trate como experimento até haver relato de uso.

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

| Aparelho                     | Ambiente de compilação                  | Estado                    |
| ---------------------------- | --------------------------------------- | ------------------------- |
| Heltec V3 (ESP32-S3, SX1262) | `Heltec_v3_companion_radio_ble_bitchat` | Compila; não ensaiado     |
| Outros ESP32                 | —                                       | Não preparados            |
| nRF52 (T1000-E, RAK4631 etc.) | —                                      | Não preparados (ver fim)  |

## Instalação no Heltec V3

Os arquivos são `meshcore-bitchat-heltec-v3.bin` e `meshcore-bitchat-heltec-v3-merged.bin`.

a) **Rádio que já roda o MeshCore _companion_ Bluetooth:** gravar o arquivo sem `merged` no
endereço `0x10000`. Contatos, canais e identidade do rádio são mantidos.

```bash
esptool.py --chip esp32-s3 write_flash 0x10000 meshcore-bitchat-heltec-v3.bin
```

b) **Rádio com outro firmware (Meshtastic, por exemplo) ou novo:** apagar a memória e gravar o
arquivo `merged` no endereço `0x0`. Tudo o que havia no rádio é perdido.

```bash
esptool.py --chip esp32-s3 erase_flash
```

```bash
esptool.py --chip esp32-s3 write_flash 0x0 meshcore-bitchat-heltec-v3-merged.bin
```

**Volta ao original:** gravar o firmware oficial pelo [gravador do MeshCore](https://flasher.meshcore.io).

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
   como o firmware original e o Bitchat não o encontra; o estado mostra `Bitchat visible in N s`.
   Passado esse minuto, esperar mais um e enviar `!bridge` de novo: `Bitchat` e `people` devem
   passar de zero.
6. Escrever no `#mesh` do Bitchat. O texto deve aparecer no canal `#bitchat` do aplicativo
   MeshCore deste rádio e dos outros da malha que tenham o canal.

Sem o aplicativo MeshCore por perto, a ponte funciona do mesmo modo: ela toma a hora do
primeiro telefone Bitchat que se apresenta. A frequência do rádio, porém, precisa ter sido
ajustada antes, uma vez.

## Comandos

Digitados no aplicativo MeshCore, em qualquer canal do rádio que tem a ponte. São respondidos só
nesse aplicativo e **não são transmitidos**.

| Comando         | Efeito                                                                        |
| --------------- | ----------------------------------------------------------------------------- |
| `!bridge`       | Mostra o estado, os contadores e uma linha por conexão Bluetooth              |
| `!bridge on`    | Liga a ponte                                                                  |
| `!bridge off`   | Desliga a ponte; o rádio segue como MeshCore comum                            |
| `!bridge local` | Só Bluetooth: os telefones Bitchat se falam pelo rádio, nada vai ao LoRa      |
| `!bridge lora`  | Passa a usar o canal em que o comando foi digitado (o canal `Public` não vale) |
| `!bridge reset` | Derruba as conexões Bluetooth para os telefones lerem os serviços de novo     |
| `!bridge help`  | Lista os comandos                                                             |

`!ponte` vale no lugar de `!bridge`. No Bitchat, `!bridge` no `#mesh` devolve só o estado.

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

g) A tela do rádio não mostra o estado da ponte; ele é lido por `!bridge`.

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

| Onde                                    | O que há                                                            |
| --------------------------------------- | ------------------------------------------------------------------- |
| `src/helpers/bitchat/`                  | Núcleo da ponte (codec, assinaturas, estado, Bluetooth, texto)      |
| `src/helpers/bitchat/BitchatBridge.*`   | Ligação com o MeshCore: canal, relógio, comandos, filas             |
| `src/helpers/bitchat/esp32/`            | Interface Bluetooth do _companion_ sobre NimBLE                     |
| `examples/companion_radio/`             | Três pontos de contato, todos sob `WITH_BITCHAT_BRIDGE`             |
| `variants/bitchat/platformio.ini`       | Ambientes de compilação                                             |
| `test/bitchat/`, `tools/bitchat/`       | Testes em computador e scripts                                      |

Os arquivos originais do MeshCore só mudam em `MyMesh.h`, `MyMesh.cpp` e `main.cpp`, em blocos
`#ifdef WITH_BITCHAT_BRIDGE`. Sem essa opção, o firmware compila como o original.

Compilar:

```bash
pio run -e Heltec_v3_companion_radio_ble_bitchat
```

```bash
pio run -e Heltec_v3_companion_radio_ble_bitchat -t mergebin
```

Testes em computador (sem aparelho), depois de uma compilação ter baixado as bibliotecas:

```bash
python tools/bitchat/run_tests.py --crypto-dir .pio/libdeps/Heltec_v3_companion_radio_ble_bitchat/Crypto
```

O núcleo veio da ponte para Meshtastic do mesmo autor
([EduChacon/meshtastic-bitchat](https://github.com/EduChacon/meshtastic-bitchat), _commit_
`47d4d67`). A base é o MeshCore oficial no _commit_ `75b04ef` (versão 1.18.0).

### O que falta

a) Ensaio em aparelho, de tudo.

b) Outros aparelhos ESP32: cada um precisa de um ambiente em `variants/bitchat/platformio.ini`.

c) nRF52 (T1000-E, RAK4631): o MeshCore usa ali a biblioteca Bluefruit. A ponte para Meshtastic
tem uma camada de adaptação para esse chip, ainda não ensaiada em aparelho; ela não foi trazida
para cá.

d) Estado da ponte na tela do rádio.
