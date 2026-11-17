# Cardputer ADV

Firmware para o M5Stack Cardputer-Adv com o módulo **Cap LoRa-1262**: MeshCore com uma interface de
conversa no próprio aparelho, pelo teclado, e a ponte Bitchat.

**Estado em 2026-11-15: compila; nunca foi ligado em aparelho.** A interface vem de um projeto que
é usado em aparelhos reais, mas na versão dele; a adaptação feita aqui não foi ensaiada.

## Origem

A definição do aparelho, o controlador de tela e a interface de teclado vêm do projeto
[MeshCore-Cardputer-ADV](https://github.com/Stachugit/MeshCore-Cardputer-ADV), de Stachu
(licença MIT, a mesma do MeshCore), no estado `e341957`, baseado no MeshCore 1.11. Foram
adaptados aqui ao MeshCore 1.18 e ligados à ponte. O que mudou em relação ao original:

a) a interface passou a receber as mensagens pelo mecanismo atual do MeshCore;

b) o tempo de desligamento da tela é guardado com os ajustes da própria interface;

c) a chave de antena do Cap LoRa-1262 é ligada como a M5Stack documenta (pino P0 do expansor em
nível alto);

d) a pilha Bluetooth é a NimBLE, compartilhada com a ponte, e não a do projeto original.

## Teclado

As teclas são as do projeto original:

| Tecla                     | Efeito                                  |
| ------------------------- | --------------------------------------- |
| Setas para cima e baixo   | Percorrem a lista                       |
| Seta para a esquerda      | Vai para os contatos                    |
| Seta para a direita       | Vai para os canais                      |
| Enter                     | Seleciona, ou envia a mensagem          |
| Backspace                 | Apaga; mantida, apaga tudo              |
| Fn + Esc, ou Opt          | Volta                                   |
| Fn + setas cima/baixo     | Rolam as mensagens, fora da escrita     |
| Fn + Del                  | Remove o contato ou o canal selecionado |
| Botão G0 (canto superior) | Envia um anúncio do rádio               |

O menu de ajustes fica no ícone do canto superior esquerdo: nome, frequência e demais parâmetros
de rádio, GPS, tema, tempo de tela e informações do aparelho.

## Correção na `bitchat0.14`

**A tela aparecia na vertical** com as versões `bitchat0.11` a `0.13`, compiladas com a biblioteca de
tela M5GFX 0.3.0, que mudou o giro do painel. As bibliotecas da M5Stack ficam agora fixadas nas
versões de janeiro de 2026 (M5Unified 0.2.13, M5GFX 0.2.19), e a tela pede só o giro que falta.

## Abrir os ajustes

Na lista de contatos ou de canais, subir com a seta para cima até passar do primeiro item: o ícone
de ajustes, no canto superior esquerdo, fica destacado. Enter abre. Opt, ou Fn + Esc, volta.

## Rádio no Brasil

Nos ajustes de rádio, a predefinição **Brasil** (a partir da `bitchat0.14`) ou a **Australia: SA, WA**
dão a convenção MeshCore Brasil: 923,125 MHz, SF 8, 62,5 kHz, CR 4/8. A **Australia: QLD** tem a mesma
frequência com CR 4/5 e não conversa com a rede brasileira. O Cap LoRa-1262 é feito para 868 MHz;
em 923 MHz o alcance tende a ser menor.

## Correções na `bitchat0.4`

a) **`Backspace` não apagava.** A biblioteca do Cardputer mudou em maio de 2026 (versão 1.2.0): a
tecla passou a ser informada em outro campo, que a interface não lê. A `bitchat0.3` foi compilada
com essa biblioteca. Agora a versão 1.1.1 fica fixada. O defeito foi achado pela leitura do código,
não em aparelho.

b) **Mais de 64 contatos corrompiam a memória.** A lista de contatos usava uma tabela de 64 posições
para até 200 contatos; o defeito vem do projeto original. A tabela agora tem o tamanho do limite de
contatos.

## A ponte neste aparelho

a) O canal `#bitchat` é criado sozinho e aparece na lista de canais.

b) O que for escrito nesse canal, no teclado, sai pelo LoRa e também chega aos telefones Bitchat
próximos, como `[MC nome] texto`.

c) O que os telefones Bitchat escrevem no `#mesh` aparece na conversa do canal, como
`apelido: texto`.

d) `!bridge`, digitado em qualquer canal, mostra o estado da ponte na própria conversa e não é
transmitido. Os demais comandos (`on`, `off`, `local`, `lora`, `link on`, `link off`,
`economy on`, `economy off`) valem do mesmo modo.

e) A tela **Device Info**, nos ajustes, mostra o estado da ponte e quantos telefones estão falando
com o rádio.

O aplicativo MeshCore no telefone continua podendo conectar por Bluetooth; o PIN aparece na tela.

## Instalação

Arquivo `M5Stack_Cardputer_ADV_companion_radio_ble_bitchat-merged.bin`, gravado no endereço `0x0`
depois de apagar a memória, como descrito no [guia](README.md#instalação). O chip é `esp32-s3` e a
memória tem 8 MB. **Tudo o que havia no aparelho é perdido**; para voltar ao que estava, faça antes
a cópia de segurança completa descrita no guia.

## O que não se sabe

a) Se a tela, o teclado e o rádio iniciam: nada foi ligado.

b) Se a chave de antena fica correta com a mudança do item (c) acima. O projeto original usa outra
sequência; a adotada aqui é a da documentação da M5Stack e a do Meshtastic.

c) A faixa de frequência do seu módulo. O Cap LoRa-1262 é vendido para a faixa de 868 MHz; confira
se o seu atende a frequência usada na sua região antes de transmitir.

d) O consumo e a duração da bateria com a ponte ligada.
