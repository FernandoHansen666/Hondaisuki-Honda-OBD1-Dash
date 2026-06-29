// =====================================================================
//  Hondaisuki_Main  —  Computador de bordo (Honda Civic 98 / ECU P2E OBD1)
// =====================================================================

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <DIYables_OLED_SSD1309.h>
#include <Fonts/Org_01.h>          // fonte do relógio (pixel/retrô)
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <time.h>                  // relógio interno (HH:MM + data)
#include <sys/time.h>             // settimeofday()
#include "modes.h"                 // anim_Normal/Tedio/Tensao/Race + _LEN
#include "animacoes.h"             // RaceAnim1 (sprite da entrada do race)


// =====================================================================
//  DISPLAY  (OLED SSD1309 128x64 via I2C)
// =====================================================================
#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1
#define SCREEN_ADDRESS 0x3C

DIYables_OLED_SSD1309 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool displayOk = false;            // OLED inicializou? (se não, segue sem ele)


// =====================================================================
//  MODOS / MENU / WI-FI  (estado da interface — mexido pelo botoes.ino)
// =====================================================================
//  3 modos do display (ver DOCUMENTAÇÃO Dash.pdf §2):
enum DisplayMode { MODO_HIBRIDO, MODO_TELEMETRIA, MODO_FUN, MODE_COUNT };
DisplayMode modoAtual = MODO_HIBRIDO;

// Menu (aberto pelo botão do meio):
bool emMenu   = false;   // está dentro do menu?
int  menuSel  = 0;       // item destacado na lista
bool editando = false;   // está ajustando um valor (hora/data)?
int  editTipo = 0;       // 0 = hora, 1 = data
int  editCampo= 0;       // campo em edição (hora: 0=h 1=min; data: 0=dia 1=mes 2=ano)
int  edH = 12, edMin = 0;                 // buffers de edição da hora
int  edDia = 1, edMes = 1, edAno = 2026;  // buffers de edição da data

// Wi-Fi sob demanda (BLE fica sempre ligado; Wi-Fi só quando o menu pede):
bool wifiOn = false;


// =====================================================================
//  CONEXÃO  (Wi-Fi e BLE — não depende do modelo da ECU)
// =====================================================================
const char* AP_SSID = "Hondaisuki_DASH";              // Wi-Fi criado pelo ESP32

const std::string OBD_MAC = "98:da:10:03:4b:35"; // MAC do adaptador BLE
static NimBLEUUID SERVICE_UUID("FFE0");
static NimBLEUUID CHAR_NOTIFY_UUID("FFE1");      // ECU -> ESP (notify)
static NimBLEUUID CHAR_WRITE_UUID("FFE2");       // ESP -> ECU (write)

constexpr uint32_t      BLE_PASSKEY           = 1234;
constexpr unsigned long RESPONSE_TIMEOUT_MS   = 500;   // espera máx. por resposta
constexpr unsigned long RECONNECT_INTERVAL_MS = 5000;  // intervalo entre tentativas

WebServer server(80);


// =====================================================================
//  GLOBAIS DE SENSORES  (FONTE ÚNICA — preenchidas pela ECU, lidas por todos)
// =====================================================================
// --- Sensores ---
float rpm    = 0;   // rotação (RPM)
int   speed  = 0;   // velocidade (km/h, VSS RAM 0x02)
float mapVal = 0;   // pressão coletor (kPa)
float baro   = 0;   // pressão barométrica (kPa)
float tps    = 0;   // borboleta (%)
float ect    = 0;   // temp. água (°C)
float iat    = 0;   // temp. ar (°C)
float bat    = 0;   // tensão bateria (V)
float o2     = 0;   // sonda lambda (V)

// --- Status do motor / combustível ---
float injTime    = 0;   // tempo de injeção (ms)
float ignition   = 0;   // avanço de ignição (graus)
float knockLimit = 0;   // knock limiting (graus)
float iacv       = 0;   // posição da IACV (%)
float stft       = 0;   // short term fuel trim (%)
float ltft       = 0;   // long term fuel trim (%)
float alternator = 0;   // carga do alternador (%)
float eld        = 0;   // ELD - carga elétrica (A)
float knock      = 0;   // detonação ao vivo (RAM 0x3C, x/55)
bool  celOn      = false;

// --- Interruptores (RAM 0x08-0x0A, vêm no probe) ---
bool swStart = false, swAC = false, swPAS = false, swBrake = false, swVtecP = false;
bool swSCS   = false, swVtecC = false;
// --- Relés (RAM 0x0B) ---
bool relMain = false, relACcl = false, relO2H = false;
// --- Flags VTEC/econo/ventoinha (RAM 0x0C) ---
bool flFan = false, flVtecE = false, flEcono = false;
// --- ID da ECU (RAM 0x76, lido 1x) ---
String ecuId = "";

// --- CEL / códigos de erro (DTC) ---
bool   celPrev      = false;   // estado anterior do CEL (detecta borda de subida)
int    errReadStep  = 4;       // 0..3 = lendo 3 blocos de erro + freeze; 4 = ocioso
bool   clearPending = false;   // apagar códigos / reset (setado ao clicar na CEL)
String errCodes     = "";      // códigos decodificados (ex.: "9,43") ou "nenhum"
String errHex       = "";      // RAM 0x40..0x5F bruto (p/ conferência)
String freezeFrame  = "";      // sensores no momento da falha (RAM 0x61..0x6C)

// --- Captura de logs (caça de bytes pela web) ---
String    lastStatusHex  = "";
String    lastSensorsHex = "";
const int LOG_MAX = 10;
String    logBuf[LOG_MAX];
int       logCount = 0;
int       logHead  = 0;

// --- Comunicação BLE/ECU ---
NimBLEClient*               pBleClient = nullptr;   // cliente único reaproveitado
NimBLERemoteCharacteristic* pWriteChar = nullptr;
bool connected = false;
bool ecuReady  = false;

bool          waitingResponse = false;
unsigned long lastRequestTime = 0;
int           sensCount       = 0;   // índice do ciclo de requisições (ecuPoll)


// =====================================================================
//  PAREAMENTO BLE  (callbacks de segurança exigidos pelo NimBLE)
// =====================================================================
//  Fica antes de setup() porque o Arduino não gera protótipos de classes.
class MySecurityCallbacks : public NimBLESecurityCallbacks {
  uint32_t onPassKeyRequest()                 { return BLE_PASSKEY; }
  void     onPassKeyNotify(uint32_t pass_key) { Serial.printf("PassKey Notify: %u\n", pass_key); }
  bool     onConfirmPIN(uint32_t pin)         { return true; }
  bool     onSecurityRequest()                { return true; }
  void     onAuthenticationComplete(ble_gap_conn_desc* desc) {
    Serial.println(desc->sec_state.encrypted ? ">>> PAREAMENTO E CRIPTOGRAFIA OK"
                                             : ">>> FALHA NA AUTENTICAÇÃO");
  }
};

// Callback de DESCONEXÃO: o EMI da aceleração derruba o BLE; aqui voltamos
// pro modo de reconexão em vez de mandar comando pra link morto.
class MyClientCallbacks : public NimBLEClientCallbacks {
  void onDisconnect(NimBLEClient* pc) {
    connected  = false;
    ecuReady   = false;
    pWriteChar = nullptr;
    Serial.println(">>> BLE DESCONECTOU (vou reconectar)");
  }
};
MyClientCallbacks clientCB;


// =====================================================================
//  CONEXÃO BLE
// =====================================================================
//  Tenta conectar à ECU, assinar as notificações e mandar o wakeup.
//  Reaproveita UM único cliente (criar vários trava o NimBLE).
void connectECU() {
  if (pBleClient == nullptr) {
    pBleClient = NimBLEDevice::createClient();
    if (pBleClient == nullptr) return;                 // sem slot livre
    pBleClient->setClientCallbacks(&clientCB, false);
  }

  if (!pBleClient->connect(NimBLEAddress(OBD_MAC))) return;

  NimBLERemoteService* pSvc = pBleClient->getService(SERVICE_UUID);
  if (!pSvc) { pBleClient->disconnect(); return; }

  NimBLERemoteCharacteristic* pNotify = pSvc->getCharacteristic(CHAR_NOTIFY_UUID);
  if (!pNotify) { pBleClient->disconnect(); return; }
  pNotify->subscribe(true, notifyCallback);            // definido em ecu_civic98.ino

  pWriteChar = pSvc->getCharacteristic(CHAR_WRITE_UUID);
  if (!pWriteChar) { pBleClient->disconnect(); return; }

  connected = true;
  ecuSendWakeup();                                     // definido em ecu_civic98.ino
}


// =====================================================================
//  CICLO DA ECU  (conexão + reconexão + polling)
// =====================================================================
//  Chamado a CADA volta do loop (mantém a ECU "fresca"). Os returns aqui
//  saem só desta função — o loop continua e ainda atualiza o display.
void ecuConnectionTick() {
  // 1) Garante conexão com a ECU
  if (!connected) {
    static unsigned long lastTry = 0;
    if (millis() - lastTry > RECONNECT_INTERVAL_MS) {
      lastTry = millis();
      connectECU();
    }
    return;
  }

  // 2) Aguarda a ECU acordar (responder ao wakeup)
  if (!ecuReady) return;

  // 3) Ciclo de requisições
  unsigned long now = millis();
  if (waitingResponse && (now - lastRequestTime > RESPONSE_TIMEOUT_MS)) {
    waitingResponse = false;                           // timeout: libera o slot
  }
  if (!waitingResponse && pWriteChar) {
    ecuPoll();                                         // definido em ecu_civic98.ino
    waitingResponse = true;
    lastRequestTime = now;
  }
}


// =====================================================================
//  SETUP
// =====================================================================
void setup() {
  Serial.begin(115200);

  displaySetup();             // OLED (não trava se falhar)
  botoesSetup();              // 3 botões (INPUT_PULLUP)
  relogioSetup();             // relógio interno (hora padrão até ajustar no menu)

  // Rotas do servidor web (o server só sobe quando o Wi-Fi é ligado no menu).
  server.on("/",         handleRoot);          // dashboard principal
  server.on("/data",     handleData);          // JSON dos sensores
  server.on("/log",      HTTP_POST, handleTestLog);
  server.on("/logs",     handleLogs);          // logs copiáveis
  server.on("/alldata",  handleAllDataPage);   // página de sensores completa
  server.on("/clearcel", handleClearCel);      // apaga os códigos / reseta a ECU
  // server.begin() é chamado por wifiLigar() (botoes.ino) quando o usuário liga.

  // BLE (sempre ligado — é o link com a ECU)
  NimBLEDevice::init("Civic_DASH");
  NimBLEDevice::setSecurityCallbacks(new MySecurityCallbacks());
}


// =====================================================================
//  LOOP
// =====================================================================
void loop() {
  if (wifiOn) server.handleClient();   // atende a web (só se ligada no menu)
  botoesUpdate();             // lê os botões (troca de modo / menu)
  ecuConnectionTick();        // conecta/lê a ECU (preenche as globais)
  displayUpdate();            // desenha a tela (throttled, lê as globais)
}
