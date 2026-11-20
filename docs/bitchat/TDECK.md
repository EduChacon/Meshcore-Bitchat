# T-Deck e T-Deck Plus

**Com tela de toque:** a partir da `bitchat0.23`, cada versão traz também o WadaMesh com a ponte,
que usa a tela, o teclado e o trackball. Ver [WADAMESH.md](WADAMESH.md). Esta página trata do
arquivo MeshCore comum.

Arquivo `LilyGo_TDeck_companion_radio_ble_bitchat`, para o LilyGo T-Deck e o T-Deck Plus.

**Estado em 2026-11-15: compila; nunca foi ligado em aparelho.**

## O que este arquivo faz

É o MeshCore _companion_ de código aberto com a ponte Bitchat. O aparelho funciona como rádio do
aplicativo MeshCore no telefone (PIN `123456`) e dos telefones Bitchat próximos. A tela mostra as
páginas de estado, entre elas a da ponte; passa-se de uma página a outra com o clique da esfera.

**O teclado, o movimento da esfera e o toque na tela não são usados.** O MeshCore de código aberto
não os trata neste aparelho, e esta versão não muda isso. Não há conversa pelo teclado.

## Sistemas próprios para o T-Deck

O sistema oficial do MeshCore para o T-Deck (MeshOS, da Ripple), segundo as fontes consultadas,
não tem o código publicado; sem ele, a ponte não pode ser embutida. Entre os sistemas de código aberto,
o [Saitama](https://github.com/868meshbot/Saitama) (licença GPL-3.0) é completo, com toque, teclado
e esfera, e usa o MeshCore 1.18.0. Uma versão dele com a ponte Bitchat teria de viver em um
repositório próprio, derivado do Saitama, por causa da licença; não faz parte desta versão.

## Instalação

Chip `esp32-s3`, memória de 16 MB.

- **T-Deck novo ou vindo de outro firmware:** arquivo `-merged.bin`, gravado no endereço `0x0`
  depois de apagar a memória. Tudo o que havia no aparelho é perdido.
- **T-Deck que já roda o MeshCore _companion_ Bluetooth de código aberto:** arquivo `.bin`, no
  endereço `0x10000`, mantendo contatos e canais.

O passo a passo está no [guia](README.md#instalação).

## GPS do T-Deck Plus

O MeshCore abre a porta do GPS a 38400 bps. Segundo a LilyGO, o T-Deck Plus saiu com dois módulos:
u-blox M10Q (38400) e Quectel L76K (9600). Com o segundo, o GPS provavelmente não funciona neste
firmware; isso é dedução do código, não ensaio. A ponte não depende do GPS.
