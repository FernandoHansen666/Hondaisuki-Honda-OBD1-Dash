// =====================================================================
//  display.ino  —  Renderização do OLED (3 modos + menu)
// =====================================================================
//  Esta camada SÓ LÊ as globais (sensores + estado de modo/menu). Quem
//  preenche os sensores é a ECU; quem mexe no modo/menu é o botoes.ino.
//
//  Modos (DisplayMode, definido no Hondaisuki_Main.ino):
//    MODO_HIBRIDO    -> isuki + painel (RPM/Vel/ECT) + relógio   (uso diário)
//    MODO_TELEMETRIA -> sem isuki, lista densa de sensores       (engenharia)
//    MODO_FUN        -> isuki em tela cheia reagindo ao RPM       (diversão)
//
//  Funções públicas:
//    displaySetup(), displayUpdate(), displayForcarRedesenho()
// =====================================================================

#define DATA_X 80   // coluna onde os dados do Modo Híbrido começam

// --- Throttle compartilhado do desenho (só 1 modo roda por vez) ---
static unsigned long ultimoDraw = 0;
static int           frameAnim  = 0;

// --- Animação de ENTRADA do race (Modo Fun) — máquina de estados NÃO-bloqueante ---
enum RaceState { RACE_NONE, RACE_SCROLL, RACE_TEXT };
static RaceState     raceState      = RACE_NONE;
static int           raceX          = SCREEN_WIDTH;
static unsigned long raceTextStart  = 0;
static unsigned long ultimoRaceAnim = 0;
static bool          emRace         = false;   // estava >4000 no ciclo anterior?


// ---------------------------------------------------------------------
//  SETUP do display (não trava o sistema se o OLED falhar)
// ---------------------------------------------------------------------
void displaySetup() {
  if (!display.begin(SSD1309_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1309 nao inicializou (sigo sem display)"));
    displayOk = false;
    return;
  }
  displayOk = true;
  display.clearDisplay();
  display.display();
}

// Força o próximo displayUpdate() a redesenhar imediatamente (sem esperar o
// throttle). Chamado pelo botoes.ino quando muda de modo / mexe no menu.
void displayForcarRedesenho() { ultimoDraw = 0; }


// ---------------------------------------------------------------------
//  RELÓGIO  (canto inferior direito; fonte Org_01) — usado no Modo Híbrido
// ---------------------------------------------------------------------
void desenharRelogio() {
  char buf[8];
  struct tm t; relogioAgora(&t);
  snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);

  display.setFont(&Org_01);
  display.setTextSize(2, 3);
  display.fillRect(82, 47, 50, 17, 0);
  display.setCursor(85, 61);
  display.print(buf);
  display.setFont();        // volta à fonte padrão
  display.setTextSize(1);
}


// ---------------------------------------------------------------------
//  Helpers de texto (Modo Híbrido)
// ---------------------------------------------------------------------
void desenharEsquerda(const char *texto, int16_t x, int16_t y, uint8_t tamanho) {
  display.setTextSize(tamanho);
  int16_t bx, by; uint16_t w, h;
  display.getTextBounds(texto, x, y, &bx, &by, &w, &h);
  display.fillRect(x, y, w, h, 0);
  display.setCursor(x, y);
  display.print(texto);
}

void desenharVelocidade(int16_t x, int16_t y, int veloc) {
  char num[8];
  snprintf(num, sizeof(num), "%d", veloc);
  const char *uni = "Km";
  int16_t bx, by; uint16_t nw, nh, uw, uh;

  display.setTextSize(2); display.getTextBounds(num, 0, 0, &bx, &by, &nw, &nh);
  display.setTextSize(1); display.getTextBounds(uni, 0, 0, &bx, &by, &uw, &uh);

  display.fillRect(x, y, nw + uw, 16, 0);
  display.setTextSize(2); display.setCursor(x, y);          display.print(num);
  display.setTextSize(1); display.setCursor(x + nw, y + 8); display.print(uni);
}

// Painel de dados reais (RPM/Vel/ECT) + relógio — usado no Modo Híbrido.
void desenharPainel() {
  char buf[16];
  display.setTextColor(SSD1309_PIXEL_ON);

  snprintf(buf, sizeof(buf), "RPM:%d", (int)(rpm + 0.5f));
  desenharEsquerda(buf, DATA_X, 3, 1);

  desenharVelocidade(DATA_X, 14, speed);

  snprintf(buf, sizeof(buf), "ECT:%dc", (int)ect);
  desenharEsquerda(buf, DATA_X, 33, 1);

  desenharRelogio();

  if (!ecuReady) display.fillCircle(125, 3, 2, SSD1309_PIXEL_ON);  // BLE sem link
}


// ---------------------------------------------------------------------
//  Escolhe o bitmap da "cara" conforme a faixa de RPM (usado em 2 modos).
//    > 4000 race · 3300-4000 tenso · 2100-3300 normal · abaixo tédio
// ---------------------------------------------------------------------
const unsigned char *frameAnimePorRPM(int valor, int idx) {
  const unsigned char *const *frames; int total;
  if (valor > 4000)       { frames = anim_Race;   total = anim_Race_LEN;   }
  else if (valor >= 3300) { frames = anim_Tensao; total = anim_Tensao_LEN; }
  else if (valor >= 2100) { frames = anim_Normal; total = anim_Normal_LEN; }
  else                    { frames = anim_Tedio;  total = anim_Tedio_LEN;  }
  return (const unsigned char *)pgm_read_ptr(&frames[idx % total]);
}


// =====================================================================
//  MODO HÍBRIDO  —  isuki + painel + relógio
// =====================================================================
void desenharModoHibrido() {
  int rpmInt = (int)(rpm + 0.5f);
  display.clearDisplay();
  display.drawBitmap(0, 0, frameAnimePorRPM(rpmInt, frameAnim), 128, 64, SSD1309_PIXEL_ON);
  desenharPainel();
  display.display();
  frameAnim++;
}


// =====================================================================
//  MODO TELEMETRIA  —  sem isuki, lista densa de sensores
//  (por enquanto um conjunto fixo; na Fase 4 a ordem vem da config web)
// =====================================================================
static void telLinha(int x, int y, const char *rot, const String &val) {
  display.setCursor(x, y);
  display.print(rot); display.print(':'); display.print(val);
}

void desenharModoTelemetria() {
  display.clearDisplay();
  display.setFont();
  display.setTextSize(1);
  display.setTextColor(SSD1309_PIXEL_ON);

  const int ys[7] = {0, 9, 18, 27, 36, 45, 54};

  // Coluna esquerda (x=0)
  telLinha(0, ys[0], "RPM", String((int)(rpm + 0.5f)));
  telLinha(0, ys[1], "VEL", String(speed));
  telLinha(0, ys[2], "ECT", String(ect, 0));
  telLinha(0, ys[3], "TPS", String(tps, 0));
  telLinha(0, ys[4], "MAP", String(mapVal, 0));
  telLinha(0, ys[5], "BAT", String(bat, 1));
  telLinha(0, ys[6], "O2",  String(o2, 2));

  // Coluna direita (x=66)
  telLinha(66, ys[0], "IAT", String(iat, 0));
  telLinha(66, ys[1], "IGN", String(ignition, 0));
  telLinha(66, ys[2], "INJ", String(injTime, 1));
  telLinha(66, ys[3], "IAC", String(iacv, 0));
  telLinha(66, ys[4], "ALT", String(alternator, 0));
  telLinha(66, ys[5], "STF", String(stft, 0));
  telLinha(66, ys[6], "LTF", String(ltft, 0));

  if (celOn)     display.fillCircle(124, 3, 3, SSD1309_PIXEL_ON);   // CEL acesa
  if (!ecuReady) display.drawCircle(124, 3, 3, SSD1309_PIXEL_ON);   // BLE sem link

  display.display();
}


// =====================================================================
//  MODO FUN  —  isuki em tela cheia + animação de entrada do race
// =====================================================================
// Detecta a transição p/ >4000 RPM e dispara a animação de entrada (com cooldown).
static void funCheckTrigger(unsigned long now) {
  const unsigned long RACE_COOLDOWN = 8000;
  int rpmInt = (int)(rpm + 0.5f);
  if (rpmInt > 4000 && !emRace && raceState == RACE_NONE &&
      now - ultimoRaceAnim >= RACE_COOLDOWN) {
    raceState = RACE_SCROLL;
    raceX     = SCREEN_WIDTH;
  }
  emRace = (rpmInt > 4000);
}

// Avança a animação de entrada em passos finos (15 ms), sem travar o loop/BLE.
static void funRaceTick(unsigned long now) {
  static unsigned long ultimoPasso = 0;

  if (raceState == RACE_SCROLL) {
    if (now - ultimoPasso < 15) return;
    ultimoPasso = now;
    int velocidade = (raceX < 50 && raceX > -30) ? 2 : 8;   // lento no centro
    display.clearDisplay();
    display.drawBitmap(raceX, 0, RaceAnim1, 128, 64, SSD1309_PIXEL_ON);
    display.display();
    raceX -= velocidade;
    if (raceX < -60) {                       // saiu da tela -> texto "RACE MODE"
      display.clearDisplay();
      display.setFont(); display.setTextColor(SSD1309_PIXEL_ON); display.setTextSize(2);
      display.setCursor(75, 15); display.println("RACE");
      display.setCursor(75, 34); display.println("MODE");
      display.display();
      raceState = RACE_TEXT; raceTextStart = now;
    }
  } else if (raceState == RACE_TEXT) {
    if (now - raceTextStart >= 1600) {       // segura ~1,6 s, depois libera
      raceState = RACE_NONE; ultimoRaceAnim = now; ultimoDraw = 0;
    }
  }
}

// Quadro normal do Fun: só a "cara" em tela cheia, sem texto de dados.
void desenharModoFun() {
  int rpmInt = (int)(rpm + 0.5f);
  display.clearDisplay();
  display.drawBitmap(0, 0, frameAnimePorRPM(rpmInt, frameAnim), 128, 64, SSD1309_PIXEL_ON);
  if (!ecuReady) display.fillCircle(125, 3, 2, SSD1309_PIXEL_ON);
  display.display();
  frameAnim++;
}


// =====================================================================
//  MENU  —  lista de opções e telas de ajuste
// =====================================================================
static void desenharMenuLista() {
  display.clearDisplay();
  display.setFont();
  display.setTextSize(1);
  display.setTextColor(SSD1309_PIXEL_ON);

  display.setCursor(0, 0);
  display.print(F("== MENU =="));

  const char *itens[MENU_ITENS] = {
    "Ajustar Hora",
    "Ajustar Data",
    wifiOn ? "Wi-Fi: LIGADO" : "Wi-Fi: deslig.",
    "Voltar"
  };
  for (int i = 0; i < MENU_ITENS; i++) {
    int y = 14 + i * 11;
    display.setCursor(0, y);
    display.print(i == menuSel ? ">" : " ");
    display.print(itens[i]);
  }

  if (wifiOn) {
    display.setCursor(0, 58);
    display.print(WiFi.softAPIP());
  }
  display.display();
}

static void desenharMenuEdit() {
  display.clearDisplay();
  display.setFont();
  display.setTextColor(SSD1309_PIXEL_ON);

  char val[12];
  int caret[3]; int nCampos;

  display.setTextSize(1);
  display.setCursor(0, 0);
  if (editTipo == 0) {                              // Hora HH:MM
    display.print(F("AJUSTAR HORA"));
    snprintf(val, sizeof(val), "%02d:%02d", edH, edMin);
    caret[0] = 20; caret[1] = 56; nCampos = 2;      // x sob HH e MM (size 2)
    display.setTextSize(2); display.setCursor(20, 24); display.print(val);
  } else {                                          // Data DD/MM/AAAA
    display.print(F("AJUSTAR DATA"));
    snprintf(val, sizeof(val), "%02d/%02d/%04d", edDia, edMes, edAno);
    caret[0] = 4; caret[1] = 40; caret[2] = 76; nCampos = 3;
    display.setTextSize(2); display.setCursor(4, 24); display.print(val);
  }

  // Setinha sob o campo em edição
  display.setTextSize(1);
  display.setCursor(caret[editCampo < nCampos ? editCampo : 0], 44);
  display.print(F("^^"));

  display.setCursor(0, 56);
  display.print(F("esq/dir  menu=ok"));
  display.display();
}

void desenharMenu() {
  if (editando) desenharMenuEdit();
  else          desenharMenuLista();
}


// =====================================================================
//  DISPATCHER — chamado a cada volta do loop(); faz throttle internamente.
// =====================================================================
void displayUpdate() {
  if (!displayOk) return;
  unsigned long now = millis();

  // Modo Fun: o gatilho e os passos finos da animação de entrada rodam
  // fora do throttle (precisam de resolução de ~15 ms).
  if (!emMenu && modoAtual == MODO_FUN) {
    funCheckTrigger(now);
    if (raceState != RACE_NONE) { funRaceTick(now); return; }
  }

  unsigned long intervalo = emMenu ? 120
                          : (modoAtual == MODO_TELEMETRIA ? 250 : 400);
  if (now - ultimoDraw < intervalo) return;
  ultimoDraw = now;

  if (emMenu) { desenharMenu(); return; }

  switch (modoAtual) {
    case MODO_HIBRIDO:    desenharModoHibrido();    break;
    case MODO_TELEMETRIA: desenharModoTelemetria(); break;
    case MODO_FUN:        desenharModoFun();         break;
    default: break;
  }
}
