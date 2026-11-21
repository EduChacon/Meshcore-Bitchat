# Roteiro da ponte Bitchat

Estado na ponte MeshCore `bitchat1.1`. Vale para as três frentes: [MeshCore](https://github.com/EduChacon/Meshcore-Bitchat),
[Meshtastic](https://github.com/EduChacon/meshtastic-bitchat) e
[Saitama](https://github.com/EduChacon/Saitama-mesh---BC-) (T-Deck Plus e Cardputer ADV).

## Para que serve

Um rádio LoRa em cada casa, ligado e esquecido. Quem está na casa usa só o Bitchat no telefone; o
rádio leva o texto público do `#mesh` às outras casas pelo LoRa e traz o delas, e as conversas
privadas entre casas passam pelo túnel. Quem recebe o rádio
de presente não precisa conhecer o MeshCore nem o Meshtastic.

## Pronto

| Recurso                                                        | MeshCore | Meshtastic  | Em aparelho                            |
| -------------------------------------------------------------- | -------- | ----------- | -------------------------------------- |
| Texto público nos dois sentidos                                | sim      | sim         | Heltec V3, versões antigas             |
| Vários telefones no mesmo rádio (3 diretos; 2 no T1000-E)      | sim      | sim         | um telefone ensaiado                   |
| Hora tomada do telefone, canal criado sozinho                  | sim      | sim         | não ensaiado sem o aplicativo do rádio |
| Ligação entre as redes Meshtastic e MeshCore pelo Bitchat      | sim      | sim         | não                                    |
| Comandos `!bridge`, inclusive para rádio sem tela              | sim      | sim         | parte                                  |
| Modo econômico opcional                                        | sim      | sim         | não                                    |
| Guia dentro da ponte (`!ponte guia`)                           | sim      | sim         | não                                    |
| Pacotes por país, sem aplicativo no primeiro uso               | sim      | sim         | não                                    |
| Histórico: texto do LoRa guardado para quem chega depois       | sim      | sim         | não                                    |
| `!bridge who`: nomes ouvidos no LoRa                           | sim      | sim         | não                                    |
| Histórico também do que os telefones escrevem                  | sim      | sim         | não                                    |
| Rede própria entre casas (pacote em 918 MHz com repetição)     | sim      | nativo      | não                                    |
| Canal LoRa com chave própria (privacidade parcial)             | sim      | sim         | não                                    |
| Só com `!lora` (`!bridge prefix on`)                           | sim      | sim         | não                                    |
| Recado da casa para quem chega (`!ponte recado`)               | sim      | sim         | não                                    |
| Rádios ao alcance (`!ponte radios`, só no aplicativo do rádio) | sim      | sim         | não                                    |
| Bateria (`!ponte bateria`) e aviso de bateria baixa            | sim      | sim         | não                                    |
| Pedido de socorro com posição (`!ponte sos`)                   | sim      | sim         | não                                    |
| Teste de alcance entre pontes (`!ponte teste`)                 | sim      | sim         | não                                    |
| Hora pelo GPS, quando nada mais deu a hora                     | sim      | sim         | não                                    |
| Conversa privada com a ponte (comandos no privado)             | sim      | sim         | Heltec V3 e T1000-E (MeshCore)         |
| Mensagem privada do Bitchat a um contato do LoRa, e resposta   | sim      | sim         | envio (MeshCore)                       |
| Conversa privada entre celulares de casas diferentes (túnel)   | sim      | laboratório | Heltec V3 e T1000-E (MeshCore)         |
| Hora pelos anúncios do LoRa                                    | sim      | nativo      | não                                    |

Na coluna do Meshtastic, "nativo" quer dizer que todo rádio Meshtastic já repassa mensagens. No
Saitama (T-Deck Plus e Cardputer ADV), o menu de ajustes tem as linhas **Bitchat** (modo) e
**Bitchat saver** (modo econômico).

## Em seguida

1. **Ensaio em aparelho** do que ainda não foi: a resposta de um contato do LoRa pelo `@contato`, o
   túnel com repetidores no caminho, a hora pelo LoRa, o histórico, a ligação com o Meshtastic e os
   [pagamentos Cashu](CASHU.md#roteiro-de-ensaio).
2. **T-Deck com WadaMesh** (desde a `bitchat0.23`): ensaiar no aparelho, em especial o Bluetooth junto
   com o Wi-Fi; depois, uma página da ponte nos ajustes do WadaMesh.
3. **Token na tela, por QR code (estudo, sem código; sem uso previsto por ora).** A ponte de destino com tela (T-Deck,
   Cardputer) mostraria o token como QR, para quem recebe sem o Bitchat. Achados: um token de até 2
   provas cabe, em geral, num QR comum; acima disso a especificação Cashu prevê QR animado (NUT-16,
   opcional, protocolo UR da Blockchain Commons, sem tamanho de quadro definido). Minibits e Cashu.me
   mostram QR animado; **não está confirmado que as carteiras o leiam**. A tela do Cardputer (240 × 135) é pequena para um QR comum desse tamanho; a do T-Deck (320 × 240), no limite. Antes de
   programar, conferir com uma carteira se ela lê um QR animado na tela do T-Deck.

O estudo completo, com tamanhos e tempos de transmissão, está em
[PRIVACY.md](PRIVACY.md). A "ponte mesh" nativa do Bitchat foi estudada e descartada para o LoRa:
cada mensagem custaria de 3 a 5 transmissões, contra 1 do formato atual.

## Fora do alcance

- Canais locais do Bitchat (dependem de internet).
- Voz, arquivos e imagens: não cabem no LoRa.
- `/block`, `/clear`, modo pânico: são do telefone.

## Depende de decisão

- Mapas do Brasil para o T-Deck: autorização para baixar a ferramenta e os dados do Protomaps.
- Nota de licença: MeshCore (MIT) com o núcleo da ponte nascido no repositório GPL do Meshtastic.

## O que falta acima de tudo

Ensaio em aparelho e medições. O texto público, a conversa privada com a ponte e o túnel foram
ensaiados pelo autor num Heltec V3 e num T1000-E; o resto foi testado no computador e compilado.
Faltam medições de alcance, consumo e duração.
