# T-Deck e T-Deck Plus

Há dois arquivos para o LilyGo T-Deck. Os dois têm a ponte Bitchat e servem ao T-Deck e ao T-Deck
Plus; a diferença é o que o próprio aparelho faz.

| Arquivo (sem a extensão)                            | Tela e teclado do aparelho                                     |
| --------------------------------------------------- | -------------------------------------------------------------- |
| `LilyGo_TDeck_keyboard_companion_radio_ble_bitchat` | Conversa no aparelho: contatos, canais e ajustes pelo teclado  |
| `LilyGo_TDeck_companion_radio_ble_bitchat`          | Só páginas de estado; o teclado não é usado (MeshCore original) |

**Estado em 2026-11-15: os dois compilam; nenhum foi ligado em aparelho.**

## A versão com teclado

A interface é a mesma do [Cardputer ADV](CARDPUTER.md), do projeto
[MeshCore-Cardputer-ADV](https://github.com/Stachugit/MeshCore-Cardputer-ADV), de Stachu (licença
MIT). Ela não foi redesenhada para o T-Deck: desenha a mesma tela de 240x135 pontos, que é mostrada
ampliada (320x180) no meio da tela do aparelho, com uma faixa preta acima e outra abaixo. O teclado
e a esfera do T-Deck são traduzidos para as teclas que a interface já conhece.

### Teclas

O T-Deck não tem `Esc`, `Fn` nem setas. Por isso:

| Tecla                                   | Efeito                                                       |
| --------------------------------------- | ------------------------------------------------------------ |
| Esfera para cima e para baixo           | Percorre a lista; na conversa, rola as mensagens             |
| Esfera para a esquerda e para a direita | Alterna entre contatos e canais; na conversa, esquerda volta |
| Clique da esfera, ou `Enter`            | Seleciona, ou envia a mensagem                               |
| Esfera pressionada por um segundo       | Envia um anúncio do rádio                                    |
| Letras e números                        | Na lista, filtram pelo nome; na conversa, começam a mensagem |
| `Backspace`                             | Apaga; sem nada para apagar, volta                           |
| `Alt` + `C`                             | Remove o contato ou o canal selecionado, sem pedir confirmação |
| `Alt` + `B`                             | Luz do teclado (função do próprio teclado)                   |

Durante a escrita, a esfera para a esquerda só volta se a mensagem estiver vazia, para um toque
acidental não descartar o texto.

Os sinais `,` `.` `;` e `/`, quando digitados fora de uma mensagem, movem a seleção como as setas,
porque no Cardputer essas são as teclas de seta.

O menu de ajustes fica no ícone do canto superior esquerdo: suba além do primeiro item da lista e
pressione `Enter`.

### A ponte

Como no Cardputer ADV: o canal `#bitchat` aparece na lista de canais; o que for escrito nele sai
pelo LoRa e chega aos telefones Bitchat próximos; `!bridge`, digitado em qualquer canal, mostra o
estado da ponte na própria conversa; a tela **Device Info**, nos ajustes, também mostra o estado.

O aplicativo MeshCore no telefone continua podendo conectar por Bluetooth (PIN `123456`).

### Diferenças em relação ao Cardputer ADV

a) Letras acentuadas recebidas aparecem com acento quando a fonte da tela tem o sinal (`á`, `é`,
`ç`, `ô` e outros); `ã` e `õ` aparecem sem o til. O teclado do T-Deck não escreve acentos.

b) O brilho da tela tem 16 níveis.

c) O ajuste de GPS não aparece nos ajustes do aparelho; liga-se o GPS pelo aplicativo MeshCore.

d) Segurar `Backspace` não apaga em sequência: o teclado do T-Deck informa cada toque, não a tecla
mantida.

e) O limite é de 200 contatos e 30 canais, como no Cardputer ADV; o arquivo sem teclado guarda 350
e 40. Vindo dele com mais de 200 contatos, os excedentes não são carregados.

## Instalação

Chip `esp32-s3`, memória de 16 MB.

- **T-Deck novo ou vindo de outro firmware:** arquivo `-merged.bin`, gravado no endereço `0x0`
  depois de apagar a memória. Tudo o que havia no aparelho é perdido.
- **T-Deck que já roda o MeshCore _companion_ Bluetooth de código aberto:** arquivo `.bin`, no
  endereço `0x10000`, mantendo contatos e canais.

O passo a passo está no [guia](README.md#instalação). Para entrar em modo de gravação, segure a
esfera pressionada enquanto liga o aparelho ou aperta o botão de reinício.

## GPS do T-Deck Plus

O MeshCore abre a porta do GPS a 38400 bps. Segundo a LilyGO, o T-Deck Plus saiu com dois módulos:
u-blox M10Q (38400) e Quectel L76K (9600). Com o segundo, o GPS provavelmente não funciona neste
firmware; isso é dedução do código, não ensaio. A ponte não depende do GPS.

## O que não se sabe

a) Se tela, teclado e esfera funcionam como descrito: nada foi ligado. A ampliação da tela foi
conferida só em simulação no computador.

b) A sensibilidade da esfera. Foi adotada a contagem que o Meshtastic usa neste aparelho (três
pulsos por passo).

c) Se o código `Alt` + `C` é o mesmo em todos os teclados: ele existe no programa de teclado
publicado pela LilyGO, mas a versão gravada em cada aparelho pode variar.

d) O consumo e a duração da bateria.

Se a versão com teclado não iniciar ou a tela ficar ilegível, grave o arquivo sem teclado: ele usa
a mesma placa e o controlador de tela original do MeshCore.
