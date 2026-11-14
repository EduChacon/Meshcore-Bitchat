# MeshCore + Bitchat v1.18.0-bitchat0.2

Firmware _companion_ do MeshCore 1.18.0 com ponte embutida para o aplicativo Bitchat.
**Versão experimental.**

## Estado

- **Heltec V3:** o autor instalou a versão anterior (`bitchat0.1`), seguiu o roteiro de primeiro
  uso e relatou que tudo funcionou. Não há medições de alcance, consumo ou duração.
- **Demais aparelhos da [lista](https://github.com/EduChacon/Meshcore-Bitchat/blob/main/docs/bitchat/DEVICES.md):**
  só compilados; nenhum foi ligado.
- **Esta versão (`bitchat0.2`)** acrescenta à anterior a página da ponte na tela do rádio e os
  arquivos dos outros aparelhos. Ela passou nos testes em computador e compila; **não foi ligada
  em aparelho**, nem no Heltec V3.

## O que faz

- Texto público do `#mesh` do Bitchat sai no canal `#bitchat` do MeshCore como `apelido: texto`.
- Texto do canal `#bitchat` chega aos telefones Bitchat próximos como `[MC nome] texto`.
- O aplicativo MeshCore e o Bitchat usam o rádio ao mesmo tempo.
- A ponte liga sozinha e cria o canal na primeira partida.
- Comandos `!bridge` no aplicativo MeshCore e página **Bitchat bridge** na tela do rádio.

## Arquivos

Dois por aparelho; **confira o nome do seu aparelho antes de gravar**.

- `<aparelho>_bitchat-merged.bin`: rádio novo ou vindo de outro firmware. Apagar a memória e gravar
  no endereço `0x0`.
- `<aparelho>_bitchat.bin`: rádio que já roda o MeshCore _companion_ Bluetooth. Gravar no endereço
  `0x10000`; os dados do rádio são mantidos.
- `SHA256SUMS`: somas de conferência.

Instalação, comandos e limites:
[guia](https://github.com/EduChacon/Meshcore-Bitchat/blob/main/docs/bitchat/README.md).

## Avisos

- Depois de gravar, pode ser preciso remover o rádio dos aparelhos Bluetooth pareados do telefone
  e parear de novo.
- A frequência e a predefinição de rádio são ajustadas no aplicativo MeshCore, como no original.
- O canal `#bitchat` é aberto: qualquer pessoa entra nele pelo nome.
- nRF52 (T1000-E, RAK4631 etc.) e ESP32-C6 não têm arquivo nesta versão.
