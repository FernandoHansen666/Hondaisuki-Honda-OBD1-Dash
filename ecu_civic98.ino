// =====================================================================
//  ecu_civic98.ino  —  TUDO ESPECÍFICO DA ECU (Honda Civic 98 / OBD1)
// =====================================================================
//  >>> Para usar em outro Honda, edite SÓ ESTE ARQUIVO. <<<
// =====================================================================


// ---------------------------------------------------------------------
//  COMANDOS DO PROTOCOLO
// ---------------------------------------------------------------------
//  Frame de requisição: {0x20, 0x05, <endereço>, 0x0C, <checksum>}
//  checksum = (0x100 - (0x20 + 0x05 + endereço + 0x0C)) & 0xFF

uint8_t cmdWakeup[]  = {0x68, 0x6A, 0xC0, 0xAF, 0xBF, 0xB3, 0xB2, 0xC1, 0xDB, 0xB3, 0xE9};
uint8_t cmdSensors[] = {0x20, 0x05, 0x10, 0x0C, 0xBF};  // temperaturas, MAP, TPS, bateria...
uint8_t cmdStatus[]  = {0x20, 0x05, 0x20, 0x0C, 0xAF};  // ignição, CEL, carga...
uint8_t cmdProbe[]   = {0x20, 0x05, 0x00, 0x0C, 0xCF};  // sonda do RPM (endereço 0x00)

// Bloco de ERROS (DTC): lido em 3 partes só quando a CEL acende, cobrindo
// RAM 0x40..0x63 = códigos 0..71 (cada código = 1 nibble; ver parseResponse).
uint8_t cmdErr1[]    = {0x20, 0x05, 0x40, 0x0C, 0x8F};  // RAM 0x40..0x4B (cód. 0-23)
uint8_t cmdErr2[]    = {0x20, 0x05, 0x4C, 0x0C, 0x83};  // RAM 0x4C..0x57 (cód. 24-47)
uint8_t cmdErr3[]    = {0x20, 0x05, 0x58, 0x0C, 0x77};  // RAM 0x58..0x63 (cód. 48-71)

// Apaga os códigos / reseta a ECU (clicando na CEL). Resposta: 01 03 FC.
uint8_t cmdClearDtc[] = {0x21, 0x04, 0x03, 0xD8};

// FREEZE FRAME: cópia dos sensores congelada no momento da falha (RAM 0x61..0x6C).
// Lido junto com os erros quando a CEL acende.
uint8_t cmdFreeze[]  = {0x20, 0x05, 0x61, 0x0C, 0x6E};

// Blocos secundários "ao vivo" (lidos no 4º slot do ciclo, em rodízio):
uint8_t cmdFlags[]   = {0x20, 0x05, 0x0C, 0x0C, 0xC3};  // RAM 0x0C VTEC-E / ventoinha / econo
uint8_t cmdKnock[]   = {0x20, 0x05, 0x3C, 0x0C, 0x93};  // RAM 0x3C knock (detonação ao vivo)
uint8_t cmdEcuId[]   = {0x20, 0x05, 0x76, 0x0C, 0x59};  // RAM 0x76 ID da ECU (lido 1x)

// ====================================================================
//  VELOCIDADE (VSS) — RAM 0x02 (mapa da P2E)
// ====================================================================
//  RAM 0x02 vem no probe do RPM (endereço 0x00): data[9].
//  (data[7]=RAM0x00, data[8]=RAM0x01 = RPM; data[9]=RAM0x02 = VSS)
//  Provavelmente já é km/h direto; se não bater, ajuste VSS_SCALE.
float VSS_SCALE = 1.0f;
int   vssRaw    = 0;     // valor bruto de RAM 0x02 (p/ calibrar a escala)

constexpr size_t CMD_WAKEUP_LEN  = sizeof(cmdWakeup);
constexpr size_t CMD_REQUEST_LEN = 5;

// Identificador do bloco na resposta (byte data[2])
constexpr uint8_t BLOCK_PROBE   = 0x00;  // RPM
constexpr uint8_t BLOCK_SENSORS = 0x10;
constexpr uint8_t BLOCK_STATUS  = 0x20;
constexpr uint8_t BLOCK_ERR1    = 0x40;  // códigos de erro (DTC) — 3 partes
constexpr uint8_t BLOCK_ERR2    = 0x4C;
constexpr uint8_t BLOCK_ERR3    = 0x58;
constexpr uint8_t BLOCK_FREEZE  = 0x61;  // freeze frame (sensores no momento da falha)
constexpr uint8_t BLOCK_FLAGS   = 0x0C;  // VTEC-E / ventoinha / econo
constexpr uint8_t BLOCK_KNOCK   = 0x3C;  // knock (detonação ao vivo)
constexpr uint8_t BLOCK_ECUID   = 0x76;  // ID da ECU (lido 1x)

uint8_t errBytes[36] = {0};  // RAM 0x40..0x63 coletado (decodificado por nibble)

// Estado do 4º slot do ciclo (blocos secundários ao vivo)
bool ecuIdRead   = false;    // ECU ID já requisitado (tenta 1x)
bool extraToggle = true;     // alterna flags(0x0C) / knock(0x3C) no 4º slot


// ---------------------------------------------------------------------
//  CICLO DE REQUISIÇÕES
// ---------------------------------------------------------------------
//  Envia o wakeup inicial (chamado quando a conexão BLE é estabelecida).
void ecuSendWakeup() {
  pWriteChar->writeValue(cmdWakeup, CMD_WAKEUP_LEN, false);
}

//  Envia a próxima requisição do ciclo: PROBE(rpm+vss) -> SENS -> STATUS.
void ecuPoll() {
  // Apagar códigos / resetar a ECU (clicando na CEL) — envia uma vez.
  if (clearPending) {
    clearPending = false;
    pWriteChar->writeValue(cmdClearDtc, sizeof(cmdClearDtc), false);
    return;
  }

  // Quando a CEL acende, lê (UMA vez) os 3 blocos de erro + o freeze frame.
  if (errReadStep < 4) {
    uint8_t* c = (errReadStep == 0) ? cmdErr1
               : (errReadStep == 1) ? cmdErr2
               : (errReadStep == 2) ? cmdErr3 : cmdFreeze;
    errReadStep++;
    pWriteChar->writeValue(c, CMD_REQUEST_LEN, false);
    return;
  }

  switch (sensCount) {
    case 0:  pWriteChar->writeValue(cmdProbe,   CMD_REQUEST_LEN, false); sensCount = 1; break; // RPM + vel + switches
    case 1:  pWriteChar->writeValue(cmdSensors, CMD_REQUEST_LEN, false); sensCount = 2; break; // temps, MAP, TPS...
    case 2:  pWriteChar->writeValue(cmdStatus,  CMD_REQUEST_LEN, false); sensCount = 3; break; // ignição, CEL...
    default: // 4º slot (rodízio): ECU ID 1x, depois alterna flags(0x0C) / knock(0x3C)
      if (!ecuIdRead)       { pWriteChar->writeValue(cmdEcuId, CMD_REQUEST_LEN, false); ecuIdRead = true; }
      else if (extraToggle) { pWriteChar->writeValue(cmdFlags, CMD_REQUEST_LEN, false); extraToggle = false; }
      else                  { pWriteChar->writeValue(cmdKnock, CMD_REQUEST_LEN, false); extraToggle = true; }
      sensCount = 0; break;
  }
}


// ---------------------------------------------------------------------
//  CALIBRAÇÕES / CONVERSÕES
// ---------------------------------------------------------------------
//  Converte a leitura bruta de um sensor de temperatura Honda (NTC) em °C.
//  Mesma curva usada para ECT (água) e IAT (ar).
float hondaTempC(uint8_t raw) {
  float f = raw;
  return 155.04149
         - f * 3.0414878
         + pow(f, 2) * 0.03952185
         - pow(f, 3) * 0.00029383913
         + pow(f, 4) * 0.0000010792568
         - pow(f, 5) * 0.0000000015618437;
}


// ---------------------------------------------------------------------
//  DEBUG
// ---------------------------------------------------------------------
//  Imprime o pacote bruto no Serial.
void dumpFrame(uint8_t* data, size_t len) {
  const char* tag = (data[2] == BLOCK_SENSORS) ? "SENS"
                  : (data[2] == BLOCK_STATUS)  ? "STAT"
                  : (data[2] == BLOCK_PROBE)   ? "RPM "
                  : (data[2] == BLOCK_FLAGS)   ? "FLAG"
                  : (data[2] == BLOCK_KNOCK)   ? "KNK "
                  : (data[2] == BLOCK_ECUID)   ? "ECID" : "ERR ";
  Serial.printf("[%s] ", tag);
  for (size_t i = 0; i < len; i++) Serial.printf("%02X ", data[i]);
  Serial.println();
}

//  Converte um frame bruto numa string hex ("20 05 20 ...").
String frameToHex(uint8_t* data, size_t len) {
  String s = "";
  char b[4];
  for (size_t i = 0; i < len; i++) { sprintf(b, "%02X ", data[i]); s += b; }
  return s;
}


// ---------------------------------------------------------------------
//  DECODIFICAÇÃO DOS BLOCOS  (TUDO AGRUPADO AQUI — edite os sensores aqui)
// ---------------------------------------------------------------------
//  Cada bloco é uma sonda que devolve 12 bytes de RAM a partir do endereço:
//      data[7 + k] = RAM(endereço + k)
//  Endereços conforme o mapa Honda OBD1 (P72/P2E). Para ajustar ou adicionar
//  um sensor, mexa só na linha do bloco correspondente abaixo.
void parseResponse(uint8_t* data, size_t len) {
  if (len < 20) return;
  uint8_t block = data[2];
  dumpFrame(data, len);

  // ===== PROBE (0x00) — RAM 0x00..0x0B =====
  if (block == BLOCK_PROBE) {
    rpm        = ((data[7] << 8) | data[8]) / 4.0f;   // 0x00:01 RPM (raw / 4 — valor da doc)
    vssRaw     = data[9];                             // 0x02    bruto (p/ calibrar VSS_SCALE)
    speed      = (int)(vssRaw * VSS_SCALE + 0.5f);    // 0x02    Velocidade (km/h)
    celOn      = (data[18] & 0x20) != 0;              // 0x0B b5 Lâmpada CEL

    // --- Interruptores (já vêm no probe: 0x08=data[15], 0x09=data[16], 0x0A=data[17]) ---
    uint8_t sw08 = data[15];                          // 0x08
    swStart = sw08 & 0x01;  swAC    = sw08 & 0x02;    // b0 partida · b1 compressor A/C
    swPAS   = sw08 & 0x04;  swBrake = sw08 & 0x08;    // b2 direção (PAS) · b3 freio
    swVtecP = sw08 & 0x80;                            // b7 pressostato VTEC
    swSCS   = data[16] & 0x08;                        // 0x09 b3 SCS (autodiagnóstico)
    swVtecC = data[17] & 0x04;                        // 0x0A b2 controle VTEC
    // --- Relés (0x0B=data[18]; b5 é a CEL, já lida acima) ---
    uint8_t r0B = data[18];                           // 0x0B
    relMain = r0B & 0x01;  relACcl = r0B & 0x02;  relO2H = r0B & 0x04;  // b0 relé princ · b1 embr A/C · b2 aquec sonda

    // Quando a CEL ACENDE (borda de subida), lê erros + freeze frame 1x.
    if (celOn && !celPrev) { errReadStep = 0; for (int i = 0; i < 36; i++) errBytes[i] = 0; }
    if (!celOn) { errCodes = ""; errHex = ""; freezeFrame = ""; }   // apagou -> limpa
    celPrev = celOn;
  }

  // ===== SENSORES (0x10) — RAM 0x10..0x1B =====
  if (block == BLOCK_SENSORS) {
    lastSensorsHex = frameToHex(data, len);
    ect        = hondaTempC(data[7]);                 // 0x10    Temp. água (°C)
    iat        = hondaTempC(data[8]);                 // 0x11    Temp. ar (°C)
    mapVal     = data[9] * 0.716f - 5;                // 0x12    MAP (kPa)
    baro       = data[10] * 0.716f - 5;               // 0x13    Baro (kPa)
    tps        = (data[11] - 24) / 2.0f;              // 0x14    TPS (%)
    o2         = data[12] / 51.3f;                    // 0x15    O2 (V) — confirmado (0,2–0,74V)
    bat        = data[14] * 0.113f - 1.90f;           // 0x17    Bateria (V) — aferida no multímetro
    alternator = data[15] / 2.55f;                    // 0x18    Alternador (%)
    // eld     = 77.06f - data[16] / 2.5371f;         // 0x19 ELD (A) — Ver um dia como resolver kkk
  }

  // ===== STATUS (0x20) — RAM 0x20..0x2B =====
  if (block == BLOCK_STATUS) {
    lastStatusHex = frameToHex(data, len);
    stft       = (data[7] / 128.0f - 1.0f) * 100.0f;     // 0x20    Short Term Fuel Trim (%)
    ltft       = (data[9] / 128.0f - 1.0f) * 100.0f;     // 0x22    Long Term Fuel Trim (%)
    injTime    = ((data[11] << 8) | data[12]) / 250.0f;  // 0x24:25 Injeção (ms)
    ignition   = data[13] * 0.5f - 64;                   // 0x26    Avanço de ignição (graus)
    knockLimit = (data[14] - 128) / 2.0f;                // 0x27    Knock limiting (graus)
    iacv       = data[15] / 2.55f;                       // 0x28    IACV (%)
  }

  // ===== ERROS/DTC (0x40/0x4C/0x58) — lido 1x quando a CEL acende =====
  // Encoding por NIBBLE (doc JDM P30): código N está no byte 0x40 + N/2,
  // nibble baixo (N par) ou alto (N ímpar). Nibble != 0 => código presente.
  if (block == BLOCK_ERR1 || block == BLOCK_ERR2 || block == BLOCK_ERR3) {
    uint8_t off = block - 0x40;                       // 0, 12 ou 24
    for (int k = 0; k < 12; k++) errBytes[off + k] = data[7 + k];

    errCodes = "";
    for (int n = 0; n < 64; n++) {                    // 32 bytes * 2 nibbles = 64 códigos
      uint8_t nib = (n & 1) ? (errBytes[n >> 1] >> 4) : (errBytes[n >> 1] & 0x0F);
      if (nib) { if (errCodes.length()) errCodes += ","; errCodes += String(n); }
    }
    if (errCodes.length() == 0) errCodes = "nenhum";

    // bruto coletado (RAM 0x40..0x5F) p/ conferência na /alldata
    errHex = "";
    char b[4];
    for (int i = 0; i < 32; i++) { sprintf(b, "%02X ", errBytes[i]); errHex += b; }
  }

  // ===== FREEZE FRAME (0x61) — sensores no momento da falha (RAM 0x61..0x6C) =====
  // data[7]=0x61 VSS2, data[8:9]=0x62:63 RPM2, data[10]=0x64 ECT2, data[11]=0x65 IAT2,
  // data[12]=0x66 MAP2, data[14]=0x68 TPS2.
  if (block == BLOCK_FREEZE) {
    int fRpm = ((data[8] << 8) | data[9]) / 3.8f;
    int fVss = data[7];
    int fEct = (int)hondaTempC(data[10]);
    int fIat = (int)hondaTempC(data[11]);
    int fMap = (int)(data[12] * 0.716f - 5);
    int fTps = (int)((data[14] - 24) / 2.0f);
    freezeFrame = "RPM " + String(fRpm) + " | Vel " + String(fVss) + " km/h | ECT " +
                  String(fEct) + "C | IAT " + String(fIat) + "C | MAP " + String(fMap) +
                  " kPa | TPS " + String(fTps) + "%";
  }

  // ===== FLAGS (0x0C) — VTEC-E / ventoinha / econo (1 byte: data[7]) =====
  if (block == BLOCK_FLAGS) {
    uint8_t f = data[7];                              // RAM 0x0C
    flFan   = f & 0x02;                               // b1 ventoinha do radiador
    flVtecE = f & 0x08;                               // b3 VTEC-E
    flEcono = f & 0x80;                               // b7 modo econômico
  }

  // ===== KNOCK (0x3C) — detonação ao vivo (data[7]) =====
  if (block == BLOCK_KNOCK) {
    knock = data[7] / 55.0f;                          // RAM 0x3C
  }

  // ===== ECU ID (0x76) — identificação da ECU (12 bytes em hex, lido 1x) =====
  if (block == BLOCK_ECUID) {
    ecuId = frameToHex(&data[7], 12);                 // RAM 0x76..0x81
  }

  waitingResponse = false;
}


// ---------------------------------------------------------------------
//  RECEPÇÃO BLE
// ---------------------------------------------------------------------
//  Chamado a cada notificação BLE recebida da ECU.
void notifyCallback(NimBLERemoteCharacteristic* p, uint8_t* data, size_t len, bool isNotify) {

  // Resposta ao wakeup (11 bytes começando em 0x68)
  if (len == 11 && data[0] == 0x68) {
    ecuReady = true;
    return;
  }

  // Frame Honda válido tem exatamente 20 bytes (o BLE entrega lixo embolado)
  if (len != 20) return;

  // Estrutura fixa de TODO frame válido: header 20 05, tamanho 0C em data[3]
  // e o marcador 00 0F em data[5:6]. Isso rejeita o lixo do BLE que passaria
  // pelo checksum fraco (soma=0) e causava a oscilação dos valores.
  if (data[0] != 0x20 || data[1] != 0x05) return;
  if (data[3] != 0x0C) return;
  if (data[5] != 0x00 || data[6] != 0x0F) return;

  uint8_t block = data[2];

  // Só aceitamos os blocos que pedimos (PROBE/SENS/STATUS + 3 de ERRO + FREEZE)
  if (block != BLOCK_PROBE && block != BLOCK_SENSORS && block != BLOCK_STATUS &&
      block != BLOCK_ERR1 && block != BLOCK_ERR2 && block != BLOCK_ERR3 &&
      block != BLOCK_FREEZE && block != BLOCK_FLAGS && block != BLOCK_KNOCK &&
      block != BLOCK_ECUID) return;

  // Checksum: a soma dos 20 bytes precisa fechar em 0 (mod 256).
  uint8_t sum = 0;
  for (size_t i = 0; i < len; i++) sum += data[i];
  if (sum != 0) return;

  parseResponse(data, len);   // decodifica o bloco (decodes todos agrupados lá)
}
