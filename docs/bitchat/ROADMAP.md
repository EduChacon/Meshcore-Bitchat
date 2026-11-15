# Roteiro da ponte Bitchat

Estado em 2026-11-15. Vale para as três frentes: [MeshCore](https://github.com/EduChacon/Meshcore-Bitchat),
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
| Histórico: texto do LoRa guardado para quem chega depois        | sim      | a fazer    | não                                        |

## Em seguida

1. **Histórico no Meshtastic.** O mesmo recurso, no outro projeto.
2. **`!bridge who`.** Responde, no Bitchat, com os nomes ouvidos no LoRa nas últimas horas. Hoje o
   `/who` do Bitchat só mostra quem está na malha Bluetooth.
3. **Histórico, segunda parte.** Guardar também o que os telefones da casa escreveram, com o anúncio
   de cada autor, para quem chega e não encontra mais ninguém. Depende de ver, em aparelho, como o
   aplicativo trata anúncios antigos.
4. **Rede própria entre casas no MeshCore.** Pacote à parte em 918,000 MHz com a repetição ligada,
   para lugares sem repetidor. Fora dele, as casas dependem de repetidores ou de alcance direto.

## Em estudo, antes de programar

5. **Mensagens privadas, grupos e pagamento.** No Bitchat são cifrados de ponta a ponta e hoje não
   atravessam o LoRa. A ideia é a ponte transportar os pacotes cifrados sem conseguir lê-los. Custos
   conhecidos: cada pacote ocupa duas ou mais transmissões, o primeiro contato exige várias
   mensagens, e quem escuta ainda vê quem fala e quando. O pagamento (`/pay`, token Cashu) só é
   seguro em conversa privada, por isso depende deste item.
6. **"Ponte mesh" nativa do Bitchat.** O aplicativo tem um mecanismo próprio para ligar grupos
   próximos pela internet, com o nome do autor e um símbolo de ponte. Usá-lo pelo LoRa daria uma
   apresentação melhor do que `[MC nome]`. Dúvidas: tamanho das mensagens assinadas, a área de cerca
   de 1 km para a qual o recurso foi feito e a opção que cada pessoa teria de ligar no telefone.

Os itens 5 e 6 pedem um desenho escrito, com tamanhos e tempos de transmissão, aprovado antes de
qualquer código.

## Fora do alcance

- Canais locais do Bitchat (dependem de internet).
- Voz, arquivos e imagens: não cabem no LoRa.
- `/block`, `/clear`, modo pânico: são do telefone.

## O que falta acima de tudo

Ensaio em aparelho. Só duas versões antigas rodaram em rádio, no Heltec V3. Tudo o mais foi
simulado e compilado. Cada item novo sobre base não ensaiada torna mais difícil achar a causa de
um defeito.
