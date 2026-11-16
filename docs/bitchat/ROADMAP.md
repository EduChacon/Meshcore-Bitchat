# Roteiro da ponte Bitchat

Estado em 2026-11-16. Vale para as três frentes: [MeshCore](https://github.com/EduChacon/Meshcore-Bitchat),
[Meshtastic](https://github.com/EduChacon/meshtastic-bitchat) e
[Saitama](https://github.com/EduChacon/Saitama-mesh---BC-) (T-Deck Plus e Cardputer ADV).

## Para que serve

Um rádio LoRa em cada casa, ligado e esquecido. Quem está na casa usa só o Bitchat no telefone; o
rádio leva o texto público do `#mesh` às outras casas pelo LoRa e traz o delas. Quem recebe o rádio
de presente não precisa conhecer o MeshCore nem o Meshtastic.

## Pronto

| Recurso                                                         | MeshCore | Meshtastic | Em aparelho                                |
| --------------------------------------------------------------- | -------- | ---------- | ------------------------------------------ |
| Texto público nos dois sentidos                                 | sim      | sim        | Heltec V3, versões antigas                 |
| Vários telefones no mesmo rádio (3 diretos; 2 no T1000-E)       | sim      | sim        | um telefone ensaiado                       |
| Hora tomada do telefone, canal criado sozinho                   | sim      | sim        | não ensaiado sem o aplicativo do rádio     |
| Ligação entre as redes Meshtastic e MeshCore pelo Bitchat       | sim      | sim        | não                                        |
| Comandos `!bridge`, inclusive para rádio sem tela               | sim      | sim        | parte                                      |
| Modo econômico opcional                                         | sim      | sim        | não                                        |
| Guia dentro da ponte (`!ponte guia`)                            | sim      | sim        | não                                        |
| Pacotes por país, sem aplicativo no primeiro uso                | sim      | sim        | não                                        |
| Histórico: texto do LoRa guardado para quem chega depois        | sim      | sim        | não                                        |
| `!bridge who`: nomes ouvidos no LoRa                            | sim      | sim        | não                                        |
| Histórico também do que os telefones escrevem                   | sim      | sim        | não                                        |
| Rede própria entre casas (pacote em 918 MHz com repetição)      | sim      | nativo     | não                                        |
| Canal LoRa com chave própria (privacidade parcial)              | sim      | sim        | não                                        |
| Só com `!lora` (`!bridge prefix on`)                            | sim      | sim        | não                                        |
| Recado da casa para quem chega (`!ponte recado`)                | sim      | sim        | não                                        |
| Rádios ao alcance (`!ponte radios`)                             | sim      | sim        | não                                        |
| Bateria (`!ponte bateria`) e aviso de bateria baixa             | sim      | sim        | não                                        |
| Pedido de socorro com posição (`!ponte sos`)                    | sim      | sim        | não                                        |
| Teste de alcance entre pontes (`!ponte teste`)                  | sim      | sim        | não                                        |
| Hora pelo GPS, quando nada mais deu a hora                      | sim      | sim        | não                                        |

Na coluna do Meshtastic, "nativo" quer dizer que todo rádio Meshtastic já repassa mensagens. No
Saitama (T-Deck Plus e Cardputer ADV), o menu de ajustes tem as linhas **Bitchat** (modo) e
**Bitchat saver** (modo econômico).

## Em seguida

1. **Ensaio em aparelho** do que está pronto, a começar pelo histórico e pela ligação entre casas.
2. **Túnel privado do Meshtastic**, que já existe em laboratório: ensaiar com dois rádios e dois
   telefones. Dele dependem as mensagens privadas e o pagamento pelo LoRa.
3. **Porte do túnel para o MeshCore**, só depois do item 2.

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

Ensaio em aparelho. Só duas versões antigas rodaram em rádio, no Heltec V3. Tudo o mais foi
simulado e compilado. Cada item novo sobre base não ensaiada torna mais difícil achar a causa de
um defeito.
