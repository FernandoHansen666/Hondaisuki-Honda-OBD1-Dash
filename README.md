# 速 Hondaisuki ・ Honda OBD1 Dash ・ ホンダ好き

> **Hondaisuki** — *"amar Honda demais"* (trocadilho com 大好き, *daisuki*).
> Um computador de bordo open-source para Hondas antigos **OBD1**, com estética
> **anime pixel art 16-bit** e uma personalidade própria: a **Isuki**.

<p align="center">
  <img alt="Plataforma" src="https://img.shields.io/badge/MCU-ESP32-000?logo=espressif&logoColor=white">
  <img alt="Display" src="https://img.shields.io/badge/OLED-SSD1309%20128x64-blue">
  <img alt="ECU" src="https://img.shields.io/badge/ECU-Honda%20OBD1%20(P2E)-red">
  <img alt="Link" src="https://img.shields.io/badge/KanjoLink-BLE%20%2F%20UART%2038400-9cf">
  <img alt="Licença" src="https://img.shields.io/badge/licen%C3%A7a-MIT-green">
</p>

---

## 🏮 O que é

**Hondaisuki** é um ecossistema de diagnóstico automotivo para entusiastas de
Hondas OBD1 (Civic, Integra, etc. da era 92–00). Ele lê os dados brutos da ECU
e os transforma em um painel vivo, no estilo dos jogos e animes dos anos 90.

O projeto é dividido em duas partes integradas:

### ⚡ KanjoLink — *o hardware*
O módulo físico + biblioteca de comunicação. Conecta-se à porta de diagnóstico
serial (**CN2**) da ECU OBD1 da Honda e traduz os dados brutos dos sensores
(RPM, TPS, MAP, temperaturas, etc.) via **UART serial (38400 bps)** ou
**Bluetooth (BLE)**.

> O nome homenageia os **Kanjozoku** (環状族) — os lendários corredores de Civic
> da Osaka Kanjo Loop.

### 🎴 Hondaisuki — *o software*
A interface gráfica que roda no ESP32 + OLED. Recebe os dados do KanjoLink e os
exibe com estética **anime pixel art 16-bit**, alto contraste, imitando os
painéis e telas dos anos 90.

### 💗 Isuki — *a alma do painel*
A **Isuki** é a personagem em pixel art que dá personalidade ao computador de
bordo. Ela **muda de expressão e reage em tempo real** ao comportamento do
motor — do tédio na marcha lenta à tensão e ao "race mode" nas altas.

<p align="center">
  <img alt="Isuki — a alma do painel" src="Imagens/isuki.png" width="440">
</p>

| Faixa de RPM | Estado da Isuki |
|---|---|
| até ~2100 | 😪 **Tédio** (marcha lenta / cruzeiro tranquilo) |
| 2100 – 3300 | 🙂 **Normal** |
| 3300 – 4000 | 😬 **Tensão** |
| acima de 4000 | 🔥 **Race Mode** (com animação de entrada "RACE MODE") |

---

## 🧩 Arquitetura

O firmware roda em **ESP32** e segue uma regra de ouro:
**o display só LÊ as globais de sensores; quem as preenche é a camada da ECU.**
Trocar de carro = editar apenas o módulo da ECU.

```
        Hondaisuki_Main.ino  (orquestrador: setup()/loop() únicos)
                  │
   ┌──────────────┼──────────────┬─────────────────┐
   ▼              ▼              ▼                 ▼
botoes.ino   ecu_civic98.ino  display.ino   web_dashboard.ino
(botões/menu) (KanjoLink/ECU)  (OLED + Isuki) (painel web debug)
   │              │              │                 │
   └──────────────┴──────┬───────┴─────────────────┘
                         ▼
            GLOBAIS DE SENSORES (fonte única)
            rpm · speed · ect · iat · map · tps · o2 · bat · …
```

| Arquivo | Papel |
|---|---|
| `Hondaisuki_Main.ino` | Orquestrador: globais (fonte única), conexão BLE/Wi-Fi, `setup()`/`loop()` |
| `ecu_civic98.ino` | **KanjoLink**: protocolo e calibrações da ECU Honda OBD1 |
| `display.ino` | Renderização do OLED — a Isuki, os 3 modos e o menu |
| `botoes.ino` | 3 botões, menu, relógio ajustável, Wi-Fi sob demanda |
| `web_dashboard.ino` | Painel web de diagnóstico (gauges, DTC, freeze-frame) |
| `modes.h` / `animacoes.h` | Bitmaps das expressões da Isuki + sprite do race |

---

## 🖥️ Os 3 modos do painel

Trocados pelos botões (◀ / ▶):

1. **Híbrido** — a Isuki reagindo ao RPM + painel (RPM · Velocidade · ECT) + relógio.
2. **Telemetria** — sem anime; 14 sensores em 2 colunas
   (RPM · VEL · ECT · TPS · MAP · BAT · O2 · IAT · IGN · INJ · IAC · ALT · STF · LTF).
3. **Fun** — a Isuki em tela cheia, reativa, **sem texto**, com a entrada "RACE MODE".

---

## 📊 Dados lidos da ECU

Leitura real via KanjoLink (sem simulação):

- **Motor:** RPM, velocidade (VSS), MAP, Baro, TPS, ECT (água), IAT (ar), O2/lambda, tensão da bateria.
- **Combustível/ignição:** tempo de injeção, avanço de ignição, knock limit, IACV, fuel trims (STFT/LTFT), alternador, detonação ao vivo.
- **Estado:** interruptores (partida, A/C, freio, VTEC, SCS…), relés, flags de ventoinha/VTEC/econo, ID da ECU.
- **Diagnóstico:** lâmpada CEL, leitura e decodificação de **DTC** (códigos Honda OBD1), **freeze-frame** e **apagar códigos / reset** da ECU.

---

## 🔌 Hardware

| Item | Pino / valor |
|---|---|
| MCU | ESP32 |
| OLED SSD1309 128×64 (I2C) | SDA 21 · SCL 22 · addr `0x3C` |
| Botão ◀ (modo anterior) | GPIO 25 (INPUT_PULLUP) |
| Botão ⏷ (menu) | GPIO 26 (INPUT_PULLUP) |
| Botão ▶ (próximo modo) | GPIO 27 (INPUT_PULLUP) |
| KanjoLink | Adaptador OBD↔BLE (UART 38400 / BLE) na CN2 da ECU |
| MicroSD | VSPI — reservado para fase futura |

> Botões ligam no GND (apertado = nível baixo), debounce ~30 ms por software.
> Pinagem dos botões ainda a confirmar no veículo.

---

## 🌐 Painel web (diagnóstico)

Wi-Fi **sob demanda** (ligado pelo menu): cria o AP `Civic_DASH` e sobe um
servidor web com dashboard de gauges, JSON dos sensores (`/data`, `/alldata`),
captura de logs e a lâmpada CEL clicável com freeze-frame e limpeza de códigos.
O BLE continua sempre ligado (é o link com a ECU).

---

## 🛠️ Como compilar

1. **Arduino IDE** com suporte a placas **ESP32**.
2. Bibliotecas: `Adafruit GFX`, `DIYables_OLED_SSD1309`, `NimBLEDevice`,
   `ArduinoJson` (`WiFi` / `WebServer` / `Wire` já vêm no core ESP32).
3. Abra `Hondaisuki_Main.ino` — o Arduino concatena todas as abas `.ino` da pasta.
4. **Partition Scheme:** use **"Huge APP"** (NimBLE + Wi-Fi + WebServer + GFX são pesados).
5. Compile e grave.

---

## 🗺️ Roadmap

- [x] Fusão UI + ECU (dados reais, sem simulação)
- [x] 3 modos + 3 botões + menu
- [x] Relógio ajustável · Wi-Fi sob demanda · painel web de diagnóstico
- [ ] MicroSD + persistência de config (`config.txt` + fallback `Preferences`)
- [ ] First-time setup (boot sem config → AP "Acesse a Web")
- [ ] Web service novo (config dos slots/ordem dos modos via navegador)
- [ ] Datalogger CSV + gráficos temporais
- [ ] Conexão serial K-line como alternativa ao BLE

---

## ⚠️ Status

Projeto em desenvolvimento por entusiastas. Firmware ainda em **validação no
veículo** (pinos dos botões, escala do VSS, endereço da IACV e algumas flags
ainda a conferir no carro). **Não há RTC** — o relógio zera ao desligar a chave.

---

## 📜 Licença

**MIT** com uma cláusula de nome — use, modifique e compartilhe livremente. 🏁

> **Cláusula de nome (marca):** o nome **HONDAISUKI** não pode ser alterado nem
> substituído. Versões modificadas (forks) **devem manter o nome base
> "Hondaisuki"** e identificar a sua versão no formato:
>
> ```
> Hondaisuki <xxxxx> version
> ```
>
> Exemplo: `Hondaisuki xxxxx version`. Ou seja: você pode adaptar o projeto à
> vontade, desde que o nome **Hondaisuki** continue visível e a sua variação
> seja identificada como uma *version*.

Veja o arquivo [`LICENSE`](LICENSE) para o texto completo.

> Feito com 大好き para a comunidade Honda OBD1.
