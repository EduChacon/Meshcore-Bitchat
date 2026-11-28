# T-Deck e T-Deck Plus com WadaMesh e a ponte Bitchat (experimental)

[WadaMesh](https://github.com/ALLFATHER-BV/wadamesh) é um firmware MeshCore com tela de toque para o
T-Deck e o T-Deck Plus: mapa, conversas, contatos e canais na tela, com teclado e trackball, além do
aplicativo MeshCore pelo Bluetooth. A partir da `bitchat0.23`, cada versão da ponte traz também o
WadaMesh com a ponte Bitchat embutida.

**Ainda não funcionou em aparelho.** Até a `bitchat1.2.1`, o arquivo reiniciava em ciclo no T-Deck
Plus, na primeira partida do Bluetooth: uma parte do WadaMesh era montada com a peça de Bluetooth do
núcleo, e não com a da ponte. A `bitchat1.2.2` corrige isso e passa ao WadaMesh `beta_109`; ela
compila, mas não foi ligada em aparelho.

## O que funciona como nas outras pontes

a) Texto público do `#mesh` do Bitchat sai no canal `#bitchat` do MeshCore, e o contrário.

b) **Na tela do T-Deck**, o canal `#bitchat` mostra as mensagens que vêm do Bitchat, com o nome de
quem escreveu, e o que você digita nele vai também para os celulares com Bitchat por perto.

c) Os comandos (`!ponte`, `!ponte guia`, `!ponte bateria`, `!ponte teste` e os demais) valem digitados
no canal, na tela do T-Deck ou no aplicativo MeshCore. A resposta aparece no próprio canal e não sai
pelo LoRa.

d) Os pagamentos Cashu pelo LoRa, como nas outras pontes ([guia](CASHU.md)).

## O que é diferente

a) **Não há página da ponte no menu do WadaMesh.** O estado se vê com `!ponte` no canal.

b) **Rádio de fábrica do Brasil.** O arquivo publicado começa em 923,125 MHz, como os pacotes do
Brasil das outras pontes, se o T-Deck nunca tiver sido configurado. Um T-Deck que já tinha
WadaMesh guarda a configuração de antes; confira o rádio nos ajustes.

c) **Bluetooth e Wi-Fi.** O WadaMesh desliga o Bluetooth em alguns momentos por causa do Wi-Fi (por
exemplo, ao reconectar o Wi-Fi ou ao atualizar pelo Wi-Fi). Enquanto o Bluetooth estiver
desligado, a ponte fica parada e volta sozinha depois.

d) **Espaço.** O firmware ocupa 98,7% do espaço reservado a ele (WadaMesh `beta_109` com a ponte):
sobram cerca de 53 KB.

e) **Versão fixa do WadaMesh.** Cada versão da ponte usa um WadaMesh específico, anotado em
`integrations/wadamesh/wadamesh.json`. Novidades do WadaMesh só chegam quando essa versão é
trocada aqui.

f) **Memória e pilha.** Os buffers da ponte ficam na PSRAM do T-Deck, para não tirar RAM interna do
Bluetooth, do Wi-Fi e da tela; o ciclo principal tem 16 KB de pilha, como nas outras pontes.

g) **Escopo de região.** O texto e os pagamentos que a ponte põe no LoRa saem com o escopo de região
do próprio canal, como o que se digita na tela.

## Instalação

Arquivos da versão: `LilyGo_TDeck_WadaMesh_bitchat-brasil-merged.bin` (instalação completa) e
`LilyGo_TDeck_WadaMesh_bitchat-brasil.bin` (só o programa).

a) **Vindo de outro firmware** (Saitama, MeshCore, Meshtastic): grave o arquivo `-merged.bin` no
endereço `0x0`, por exemplo pelo [ESP Web Tool da Espressif](https://espressif.github.io/esptool-js/),
no Chrome ou no Edge. Se o computador não reconhecer o T-Deck, segure o clique do trackball ao
ligar o cabo (é o botão de partida do chip, pela configuração da placa no WadaMesh). A gravação
completa apaga contatos e canais guardados no aparelho.

b) **Atualizando um T-Deck que já tem este WadaMesh com a ponte:** grave só o `.bin` no endereço
`0x10000`. Assim os ajustes e o Wi-Fi guardados ficam, como recomenda o WadaMesh.

**Volta ao WadaMesh original:** o gravador do próprio WadaMesh,
[flasher.wadamesh.com](https://flasher.wadamesh.com).

## Licença

O WadaMesh é GPL-3.0-or-later, e o arquivo com a ponte também é. O código-fonte correspondente é o
WadaMesh no commit anotado em `integrations/wadamesh/wadamesh.json` mais este repositório: a ponte
(`src/helpers/bitchat`), o encaixe (`integrations/wadamesh`) e o roteiro que monta e compila tudo
(`tools/bitchat/build_wadamesh.py`).

## Para quem desenvolve

```bash
python3 tools/bitchat/build_wadamesh.py --region brasil
```

O roteiro baixa o WadaMesh no commit fixado, aplica `integrations/wadamesh/bridge.patch` (ganchos nas
mensagens de canal, no envio pela tela e pelo aplicativo, no Bluetooth e no ciclo principal), copia a
ponte e `integrations/wadamesh/src` para dentro dele, deixa de fora o `SerialBLEInterface.cpp` do
núcleo (a ponte traz o seu, com o mesmo serviço e a mesma senha do aplicativo), aplica no NimBLE a
mesma correção de partida das outras pontes e compila. Detalhes em
[integrations/wadamesh/README.md](../../integrations/wadamesh/README.md).
