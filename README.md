# LUCAS-TAG — alerta de separação (Fase 1: BLE + tela)

Firmware ESP-IDF para a **Waveshare ESP32-C6-LCD-1.47**. A tag fica com a criança. Os celulares
dos responsáveis mandam um "heartbeat" por Bluetooth LE. Se o sinal some, a tela passa a
pedir ajuda a quem encontrar a criança e mostra um QR code.

| Etapa | Conteúdo | Status |
|---|---|---|
| 1 | Estrutura do projeto, tela ST7789 + LVGL, fontes Latin-1, telas da UI em modo de teste | ✅ compilado e gravado |
| 2 | LED WS2812: respiração no normal, sirene de PERDIDO, cores de proximidade | ✅ compilado e gravado |
| 3 | NimBLE: anúncio `LUCAS-TAG`, serviço GATT, pareamento com passkey na tela, proximidade por RSSI | ✅ compilado e gravado |
| 4 | Máquina de estados (armar, PERDIDO, chegando, encontrado) | ✅ compilado e gravado |
| 5 | Desarme automático ao ver o Wi-Fi de casa | a fazer |

## Hardware

| Função | GPIO |
|---|---|
| LCD MOSI / SCLK / CS / DC / RST / BL | 6 / 7 / 14 / 15 / 21 / 22 |
| LED RGB WS2812 | 8 |
| Botão BOOT (único botão livre para o firmware) | 9 |

O LCD é um ST7789 de 172×320, sem touch e com cantos arredondados. A área visível é deslocada
34 px em X. A placa testada tem um ESP32-C6FH8 (8 MB de flash). O projeto está configurado
para 4 MB, o que funciona em qualquer versão da placa.

## Compilar e gravar

Use o ambiente do **ESP-IDF v6.1 instalado pelo EIM**. Não use o Python nem o CMake do sistema.
No PowerShell:

```powershell
. C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1   # define IDF_PATH e IDF_TOOLS_PATH
cd C:\AIProjects\RtrLKs
idf.py build
idf.py -p COM3 flash monitor     # Ctrl+] sai do monitor
```

Na primeira compilação, o gerenciador de componentes baixa `lvgl/lvgl` (9.6),
`espressif/esp_lvgl_port` (2.9) e `espressif/led_strip` (3.1). Se mudar o `sdkconfig.defaults`,
apague o `sdkconfig` para que os novos padrões sejam aplicados.

## Configurações (menuconfig)

O menuconfig é um menu de texto do ESP-IDF. Abra um **PowerShell** (fora do VS Code ou no
terminal integrado) e rode:

```powershell
. C:\Espressif	ools\Microsoft.v6.1.PowerShell_profile.ps1
cd C:\AIProjects\RtrLKs
idf.py menuconfig
```

Navegue com as setas, **Enter** entra num submenu, **Esc** volta e **Espaço** liga/desliga.
Para mudar um número ou texto, selecione a opção e tecle **Enter**. Ao sair, confirme
**Save** (ou tecle **S**). Depois compile e grave de novo (`idf.py -p COM3 flash`).
Com a extensão ESP-IDF do VS Code, o mesmo menu aparece em forma gráfica pelo comando
*ESP-IDF: SDK Configuration Editor*.

Opções do projeto, em **LUCAS-TAG**:

| Opção | Padrão | Para quê |
|---|---|---|
| Modo de teste da tela | desligado | Percorre todas as telas e cores do LED em ciclo, sem Bluetooth |
| Diagnóstico ao ligar | desligado | Procura touch e IMU da versão Touch da placa |
| Segundos sem heartbeat até PERDIDO | 20 | |
| `LUCAS_QR_URL` | `https://example.com/achei` | URL do QR code na tela de PERDIDO |
| Tela → clock SPI | 40 MHz | Reduza se aparecer ruído na imagem |
| Tela → deslocamento X/Y | 34 / 0 | Ajuste se faltar ou sobrar uma borda |
| Tela → inverter cores | ligado | Painéis IPS ST7789 normalmente precisam |
| Tela → ordem BGR | desligado | Ligue se vermelho e azul aparecerem trocados |
| LED → ordem GRB | desligado | Ligue se vermelho e verde aparecerem trocados (esta placa usa RGB) |
| LED → brilho normal | 16 (de 255) | Brilho máximo da respiração verde; 0 deixa o LED apagado |
| LED → respiração normal | 4000 ms | Duração de um ciclo acende/apaga no modo normal |
| LED → brilho ativo | 64 (de 255) | Pareamento e cores de proximidade (chegando) |
| LED → brilho da sirene | 160 (de 255) | Vermelho/azul de PERDIDO |
| LED → tempo de cada cor da sirene | 200 ms | Velocidade da sirene |
| Bluetooth → nome anunciado | `LUCAS-TAG` | |
| Bluetooth → janela de pareamento | 60 s | Tempo liberado após segurar o BOOT |
| Bluetooth → RSSI dos níveis | −85 / −75 / −65 / −55 dBm | Mínimo para aproximando / próximo / muito perto / sinal máximo |

A lista de SSIDs de casa entra aqui na Etapa 5.

## LED

| LED | Estado |
|---|---|
| Apagado | **Desarmado:** a tag não está vigiando (antes do primeiro heartbeat, ou em casa no Wi-Fi) |
| Verde bem fraco acendendo e apagando devagar | **Normal:** armado, com celular pareado por perto |
| Vermelho/azul alternando (sirene) | **PERDIDO:** ninguém por perto |
| Muda suavemente da cor do nível atual para a do anterior e volta, mais rápido quanto mais perto | **Chegando:** vermelho (longe) → laranja → amarelo → verde → azul (sinal máximo) |
| Roxo acendendo e apagando devagar | **Pareamento:** passkey na tela |

A proximidade é a do celular pareado mais próximo, medida pela própria tag pelo RSSI da
conexão Bluetooth. No modo normal e no pareamento, o LED acende e apaga
suavemente. No modo *chegando*, o LED fica sempre aceso e vai mudando suavemente da cor do nível atual
para a do nível anterior e de volta, o que mostra se a pessoa está se aproximando ou se afastando. A alternância
acelera como um sensor de ré: de 1,4 s por ciclo (longe) até 0,6 s (sinal máximo). Ao
entrar no modo, a cor fica fixa até o nível mudar pela primeira vez. Se o sinal pular
vários níveis, a cor "anterior" é a do nível vizinho, então o LED nunca mistura
vermelho com azul, que é a sirene de PERDIDO. O RSSI é suavizado (média móvel), tem
histerese de 3 dB entre níveis, e cada nível novo só vale depois de se manter por ~3 s.

## Teste da tela e do LED

Com o **modo de teste da tela** ligado no menuconfig (o Bluetooth fica desligado nesse modo), a placa repete este ciclo (o log serial mostra cada passo):

1. **Padrão de teste** (8 s), com o LED em verde fraco respirando. Confira:
   - **Moldura branca de 1 px**, com cantos arredondados (raio de 30 px) como os do painel,
     visível nas quatro bordas. Se faltar uma borda ou aparecer uma faixa de lixo, ajuste o
     deslocamento X/Y.
   - **Barras "Verm." / "Verde" / "Azul"** nas cores certas. Se vermelho e azul estiverem
     trocados, ligue a ordem BGR.
   - **Degradê** indo do preto (à esquerda) ao branco (à direita). Se estiver invertido,
     desligue a inversão de cores.
   - **Acentos** corretos: ç ã õ é ê í ó ú à ü ñ ¿ ¡ ° º.
2. **PERDIDO** (8 s), com o LED em sirene vermelho/azul. A tela tem fundo preto,
   "CRIANÇA PERDIDA" em amarelo, o QR em caixa branca com 5 px de margem e "Escaneie para
   avisar aos pais". Só o QR fica branco, para a câmera focar. Aponte o celular para o QR:
   ele deve abrir a `LUCAS_QR_URL`.
3. **Chegando** (5 níveis × 4 s). A tela mostra "Mãe está chegando:" em branco, a barra de
   proximidade com o nome do nível e, em amarelo, o símbolo ⚠ com "FIQUE PARADO". O nome
   alterna entre Mãe, Pai, Babá e Parente a cada ciclo. A cada nível o LED transita suavemente
   entre a cor nova e a anterior, cada vez mais rápido: vermelho → laranja → amarelo → verde → azul.
4. **Pareamento** (4 s): passkey de exemplo `123456`, com o LED em roxo respirando.
5. **Tudo apagado** (2 s): tela e LED desligados.

## Fontes

As fontes embutidas do LVGL cobrem só ASCII. Por isso o projeto gera as suas, em
`main/fonts/`, com ASCII e Latin-1 (U+0020–007E e U+00A0–00FF), a partir da Roboto variável
do Google Fonts:

- **Frases importantes** ("CRIANÇA PERDIDA", "… está chegando:", "FIQUE PARADO"): peso 900 e
  largura 75, em 30 e 40 px.
- **Frases complementares:** peso 400 e largura 100, em 14, 16 e 22 px.
- **Passkey:** dígitos com peso 700, em 40 px.
- **Símbolo de atenção** (⚠, `LV_SYMBOL_WARNING`): um único glifo da FontAwesome 5 Free, em 36 px.

O gerador é um script Python próprio, que dispensa o Node.js do `lv_font_conv`. Ele escolhe o
peso com `--wght` (100–900) e a largura com `--wdth` (75–100):

```sh
pip install pillow
sh tools/gen_fonts.sh          # ou PYTHON=/caminho/python sh tools/gen_fonts.sh
```

Licenças: Roboto sob SIL OFL (`tools/fonts/OFL.txt`); FontAwesome Free com ícones sob
CC BY 4.0 e fonte sob OFL (`tools/fonts/FontAwesome5-LICENSE.txt`).

## Testar com o nRF Connect (Android)

### Estados

| Estado | Valor | Tela | LED | Como entra |
|---|---|---|---|---|
| Desarmado | `00` | apagada | apagado | Ao ligar, ou com o comando `DESARMAR` |
| Armado | `01` | apagada | verde fraco respirando | Primeiro heartbeat; ou `ENCONTRADO` |
| PERDIDO | `02` | "CRIANÇA PERDIDA" + QR | sirene vermelho/azul | 20 s sem heartbeat (armado ou chegando) |
| Chegando | `03` | "<nome> está chegando:" + barra + "FIQUE PARADO" | transição entre as cores de proximidade | Em PERDIDO: heartbeat de um celular pareado ou escrita em *Chegando* |

Só o comando `ENCONTRADO` tira a tag de PERDIDO ou de CHEGANDO e a devolve a ARMADO. Um
heartbeat em PERDIDO leva a CHEGANDO, não a ARMADO.

### Serviço GATT

Serviço `a7c30001-1b8e-4f2a-9d6e-3c5b7e9f0a12`:

| Característica | UUID | Propriedades | Formato |
|---|---|---|---|
| Heartbeat | `a7c30002-…` | Write (pareado) | Texto UTF-8: nome do responsável, ex. `Mãe`, `Pai`, `Babá` |
| Chegando | `a7c30003-…` | Write (pareado) | Texto UTF-8: nome de quem está indo buscar, ex. `Pai` |
| Comando | `a7c30004-…` | Write (pareado) | Texto `ENCONTRADO` ou `DESARMAR` |
| Estado | `a7c30005-…` | Read + Notify (pareado) | 1 byte, valores da tabela de estados |

(`…` = o mesmo restante do UUID do serviço.) "Pareado" quer dizer conexão criptografada e
autenticada por passkey. Sem isso, leituras e escritas são recusadas com *Insufficient
Authentication*. Textos têm no máximo 32 bytes.

O nível de proximidade não é enviado pelo celular: a tag mede o RSSI de cada celular pareado
conectado e usa o mais próximo, tanto para o LED quanto para a barra da tela "chegando". O
nome que aparece na tela é exatamente o texto enviado. Para ler "A mãe está chegando", basta
o app enviar `A mãe`.

### Quem pode parear

- **O primeiro celular** pareia direto: com nenhum vínculo salvo, a tag aceita o pareamento.
- **Os seguintes** só pareiam depois de alguém **segurar o botão BOOT por 3 s**. A tela mostra
  "Pareamento" e o LED respira em roxo por 60 s (ajustável no menuconfig). Fora dessa janela,
  pedidos de pareamento são recusados e a conexão é encerrada.
- Até 3 celulares conectados ao mesmo tempo e 8 vínculos salvos.

### Passo a passo

1. Instale o **nRF Connect for Mobile** (Nordic Semiconductor) pela Play Store.
2. **Scanner** → procure **LUCAS-TAG** → **CONNECT**.
3. **Parear:** em **Client**, abra o menu ⋮ → **Bond**, ou simplesmente tente escrever numa
   característica: o Android pede o código. A tag mostra a tela de **Pareamento** com
   6 dígitos, e o LED respira em roxo. Digite esse código no celular. Se não for o primeiro
   celular, segure o BOOT por 3 s antes.
4. **Assinar o estado:** na característica *Estado*, toque no ícone de setas triplas
   (notify). O valor `00` (desarmado) deve aparecer.
5. **Armar:** na característica *Heartbeat*, toque na seta para cima (write), escolha
   **TEXT (UTF-8)**, digite `Mãe` e envie. O estado vai para `01` e o LED passa a respirar
   em verde fraco. Para manter armado, repita a escrita a cada ~5 s (veja o passo 9).
6. **Simular separação:** pare de enviar heartbeat (ou desconecte). Depois de 20 s o estado
   vai para `02`, a tela mostra **PERDIDO** com o QR code e o LED entra em sirene.
7. **Reencontro:** escreva `Pai` em *Chegando* (ou volte a mandar heartbeat). A tela muda para
   "Pai está chegando:" e "FIQUE PARADO", e o estado vai para `03`. Aproxime e afaste o
   celular para ver a barra e a cor do LED mudarem. Cada nível novo leva ~3 s para valer.
   Continue mandando heartbeat: sem ele, a tag volta para PERDIDO depois de 20 s.
8. **Encontrado:** escreva `ENCONTRADO` em *Comando*. A tela apaga e a tag volta a `01`.
9. **Heartbeat automático:** no nRF Connect, abra **Macros** → gravar → escreva o heartbeat
   uma vez → parar. Depois edite a macro e coloque um *sleep* de 5 s em loop.
10. **Desarmar:** escreva `DESARMAR` em *Comando*. Estado `00`, tela e LED apagados.

O log serial (`idf.py -p COM3 monitor`) mostra cada evento: conexões, pareamento, heartbeats,
mudanças de estado e de nível de proximidade com o RSSI médio. Use-o para calibrar os limites
de RSSI no menuconfig.

### Desarme pelo Wi-Fi de casa (Etapa 5)

Quando a tag detectar um dos SSIDs configurados no menuconfig, ela volta a "desarmado"
(estado `00`, LED apagado). Assim não dispara alarme dentro de casa.
