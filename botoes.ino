// =====================================================================
//  botoes.ino  —  Botões físicos, menu, relógio ajustável e Wi-Fi
// =====================================================================
//  3 botões (INPUT_PULLUP, apertar = liga no GND):
//    BTN ESQ   (25) -> modo anterior   (ou, no menu, item/valor anterior)
//    BTN MENU  (26) -> abre o MENU      (ou, no menu, seleciona/avança campo)
//    BTN DIR   (27) -> próximo modo     (ou, no menu, item/valor seguinte)
//
//  MENU: Ajustar Hora · Ajustar Data · Wi-Fi liga/desliga · Voltar.
//  Relógio: usa o relógio interno do ESP32 (time.h). Sem RTC com bateria,
//  ele zera ao perder energia — por isso o ajuste pelo menu.
// =====================================================================

// --- Pinos dos botões (ajuste conforme a sua PCB) ---
const uint8_t PIN_BTN_ESQ  = 25;
const uint8_t PIN_BTN_MENU = 26;
const uint8_t PIN_BTN_DIR  = 27;

// --- Pinos reservados pro MicroSD (VSPI) — usados só na Fase 3 ---
//  SCK=18  MISO=19  MOSI=23  CS=5   (deixados aqui como lembrete)

const int MENU_ITENS = 4;   // Hora, Data, Wi-Fi, Voltar

// --- Debounce simples por botão ---
//  OBS: passamos o ÍNDICE (não a struct por referência) pra função botaoBorda.
//  Tipo definido no sketch (struct) na assinatura quebra o auto-prototype do
//  Arduino ("'Botao' was not declared"); com uint8_t o protótipo compila.
struct Botao { uint8_t pin; bool estavel; bool ultimaLeitura; unsigned long tMudou; };
Botao btns[3] = {
  {PIN_BTN_ESQ,  false, false, 0},   // [0] = ESQ
  {PIN_BTN_MENU, false, false, 0},   // [1] = MENU
  {PIN_BTN_DIR,  false, false, 0},   // [2] = DIR
};
#define B_ESQ  0
#define B_MENU 1
#define B_DIR  2
const unsigned long DEBOUNCE_MS = 30;

// Retorna true SÓ no instante em que o botão é apertado (borda de descida).
bool botaoBorda(uint8_t i) {
  Botao &b = btns[i];
  bool leitura = (digitalRead(b.pin) == LOW);   // INPUT_PULLUP: LOW = apertado
  bool borda = false;
  if (leitura != b.ultimaLeitura) { b.tMudou = millis(); b.ultimaLeitura = leitura; }
  if (millis() - b.tMudou > DEBOUNCE_MS && leitura != b.estavel) {
    b.estavel = leitura;
    if (b.estavel) borda = true;                // só conta ao apertar
  }
  return borda;
}


// ---------------------------------------------------------------------
//  RELÓGIO  (software, via time.h do ESP32)
// ---------------------------------------------------------------------
void relogioSetup() {
  setenv("TZ", "UTC0", 1); tzset();             // sem fuso/horário de verão
  struct tm t = {0};                            // hora padrão até o usuário ajustar
  t.tm_year = 2026 - 1900; t.tm_mon = 1 - 1; t.tm_mday = 1;
  t.tm_hour = 12; t.tm_min = 0; t.tm_sec = 0;
  time_t epoch = mktime(&t);
  struct timeval tv; tv.tv_sec = epoch; tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
}

void relogioAgora(struct tm *t) {
  time_t now = time(nullptr);
  localtime_r(&now, t);
}

void relogioDefinirHora(int h, int m) {
  struct tm t; relogioAgora(&t);
  t.tm_hour = h; t.tm_min = m; t.tm_sec = 0;
  time_t e = mktime(&t);
  struct timeval tv; tv.tv_sec = e; tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
}

void relogioDefinirData(int d, int mo, int y) {
  struct tm t; relogioAgora(&t);
  t.tm_mday = d; t.tm_mon = mo - 1; t.tm_year = y - 1900;
  time_t e = mktime(&t);
  struct timeval tv; tv.tv_sec = e; tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
}


// ---------------------------------------------------------------------
//  WI-FI sob demanda  (BLE fica sempre ligado; Wi-Fi só quando o menu pede)
// ---------------------------------------------------------------------
void wifiLigar() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);
  server.begin();
  wifiOn = true;
  Serial.print(F(">>> Wi-Fi ON — http://"));
  Serial.println(WiFi.softAPIP());
}

void wifiDesligar() {
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  wifiOn = false;
  Serial.println(F(">>> Wi-Fi OFF"));
}


// ---------------------------------------------------------------------
//  SETUP dos botões
// ---------------------------------------------------------------------
void botoesSetup() {
  pinMode(PIN_BTN_ESQ,  INPUT_PULLUP);
  pinMode(PIN_BTN_MENU, INPUT_PULLUP);
  pinMode(PIN_BTN_DIR,  INPUT_PULLUP);
}


// ---------------------------------------------------------------------
//  Mantém um valor dentro de [lo, hi] com wrap-around.
// ---------------------------------------------------------------------
static int wrapInt(int v, int lo, int hi) {
  int n = hi - lo + 1;
  return lo + (((v - lo) % n) + n) % n;
}

// Carrega os campos de edição a partir do relógio atual.
static void carregarHora() { struct tm t; relogioAgora(&t); edH = t.tm_hour; edMin = t.tm_min; }
static void carregarData() { struct tm t; relogioAgora(&t); edDia = t.tm_mday; edMes = t.tm_mon + 1; edAno = t.tm_year + 1900; }


// ---------------------------------------------------------------------
//  Tratamento do menu (chamado quando emMenu == true)
// ---------------------------------------------------------------------
static void menuTratar(bool esq, bool menu, bool dir) {
  if (editando) {
    int delta = dir ? +1 : (esq ? -1 : 0);
    if (editTipo == 0) {                         // --- Hora ---
      if (editCampo == 0) edH   = wrapInt(edH   + delta, 0, 23);
      else                edMin = wrapInt(edMin + delta, 0, 59);
      if (menu) {
        editCampo++;
        if (editCampo > 1) { relogioDefinirHora(edH, edMin); editando = false; }
      }
    } else {                                     // --- Data ---
      if      (editCampo == 0) edDia = wrapInt(edDia + delta, 1, 31);
      else if (editCampo == 1) edMes = wrapInt(edMes + delta, 1, 12);
      else                     edAno = wrapInt(edAno + delta, 2000, 2099);
      if (menu) {
        editCampo++;
        if (editCampo > 2) { relogioDefinirData(edDia, edMes, edAno); editando = false; }
      }
    }
    return;
  }

  // --- Navegando pela lista do menu ---
  if (esq)      menuSel = wrapInt(menuSel - 1, 0, MENU_ITENS - 1);
  else if (dir) menuSel = wrapInt(menuSel + 1, 0, MENU_ITENS - 1);
  else if (menu) {
    switch (menuSel) {
      case 0: editando = true; editTipo = 0; editCampo = 0; carregarHora(); break;  // Hora
      case 1: editando = true; editTipo = 1; editCampo = 0; carregarData(); break;  // Data
      case 2: if (wifiOn) wifiDesligar(); else wifiLigar(); break;                  // Wi-Fi
      case 3: emMenu = false; break;                                               // Voltar
    }
  }
}


// ---------------------------------------------------------------------
//  UPDATE dos botões — chamado a cada volta do loop().
// ---------------------------------------------------------------------
void botoesUpdate() {
  bool esq  = botaoBorda(B_ESQ);
  bool menu = botaoBorda(B_MENU);
  bool dir  = botaoBorda(B_DIR);
  if (!esq && !menu && !dir) return;             // nada apertado

  if (emMenu) {
    menuTratar(esq, menu, dir);
  } else {
    if (menu)      { emMenu = true; menuSel = 0; editando = false; }     // abre o menu
    else if (esq)  { modoAtual = (DisplayMode)wrapInt(modoAtual - 1, 0, MODE_COUNT - 1); }
    else if (dir)  { modoAtual = (DisplayMode)wrapInt(modoAtual + 1, 0, MODE_COUNT - 1); }
  }

  displayForcarRedesenho();                       // redesenha já (não espera o throttle)
}
