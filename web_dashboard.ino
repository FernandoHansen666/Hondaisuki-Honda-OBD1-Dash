// =====================================================================
//  web_dashboard.ino  —  Interface web
// =====================================================================
//  Tudo que o navegador acessa:
//    handleData()         -> GET  /data     (JSON dos sensores)
//    handleTestLog()      -> POST /log      (anota mensagem no Serial)
//    handleRoot()         -> GET  /         (dashboard principal, HTML)
//    handleAllDataPage()  -> GET  /alldata  (lista completa de sensores, HTML)
//
//  Os valores vêm das variáveis globais preenchidas pela ECU
//  (rpm, ect, mapVal, ...). Para mostrar um sensor novo: adicione a
//  chave no handleData() e leia-a no HTML correspondente.
// =====================================================================


// ---------------------------------------------------------------------
//  GET /data  —  JSON com os valores atuais dos sensores
// ---------------------------------------------------------------------
void handleData() {
  StaticJsonDocument<2048> doc;

  doc["rpm"]   = (rpm > 9000) ? 0 : (int)(rpm + 0.5f);  // inteiro arredondado
  doc["speed"] = speed;             // VELOCIDADE (VSS, RAM 0x02) km/h
  doc["tps"]   = (int)(tps + 0.5f);
  doc["ect"]   = String(ect, 0);
  doc["iat"]   = String(iat, 0);
  doc["map"]   = String(mapVal, 0);
  doc["baro"]  = String(baro, 0);
  doc["bat"]   = String(bat, 1);
  doc["o2"]    = String(o2, 2);
  doc["cel"]   = celOn;
  doc["ign"]   = String(ignition, 1);
  doc["inj"]   = String(injTime, 1);
  doc["knocklim"] = String(knockLimit, 1);
  doc["iacv"]  = String(iacv, 0);
  doc["stft"]  = String(stft, 1);
  doc["ltft"]  = String(ltft, 1);
  doc["alt"]   = String(alternator, 0);
  doc["eld"]   = String(eld, 1);
  doc["err"]    = errCodes;         // códigos DTC (só preenche quando CEL acende)
  doc["errhex"] = errHex;           // bloco 0x40 bruto (p/ confirmar o encoding)
  doc["freeze"] = freezeFrame;      // sensores no momento da falha
  doc["stat"]   = lastStatusHex;    // frame STATUS cru (usado em /alldata)

  // --- Novos: knock, ECU ID, interruptores, relés, flags ---
  doc["knock"]   = String(knock, 1);
  doc["ecuid"]   = ecuId;
  doc["sw_start"]= swStart;  doc["sw_ac"]    = swAC;    doc["sw_pas"]  = swPAS;
  doc["sw_brake"]= swBrake;  doc["sw_vtecp"] = swVtecP;
  doc["sw_scs"]  = swSCS;    doc["sw_vtecc"] = swVtecC;
  doc["rel_main"]= relMain;  doc["rel_ac"]   = relACcl; doc["rel_o2h"] = relO2H;
  doc["fl_fan"]  = flFan;    doc["fl_vtece"] = flVtecE; doc["fl_econo"]= flEcono;

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}


// ---------------------------------------------------------------------
//  Registra um log no buffer circular (mantém só os últimos LOG_MAX).
//  Cada entrada junta a nota do usuário com os frames brutos do momento,
//  pra depois correlacionar "velocidade real -> qual byte mudou".
void addLog(const String& msg) {
  String entry  = "#" + String(logCount + 1);
  entry += " t=" + String(millis()) + "ms";
  entry += " nota=" + msg;
  entry += " rpm=" + String((int)(rpm + 0.5f));
  entry += " vel=" + String(speed) + "km/h";
  entry += "\n  STAT: " + lastStatusHex;
  entry += "\n  SENS: " + lastSensorsHex;

  logBuf[logHead] = entry;
  logHead = (logHead + 1) % LOG_MAX;
  logCount++;
}

//  POST /log?msg=...  —  anota uma mensagem (vai pro Serial e pro buffer)
// ---------------------------------------------------------------------
void handleTestLog() {
  if (server.hasArg("msg")) {
    String msg = server.arg("msg");
    Serial.println("\n####################################");
    Serial.printf(">>> ANOTAÇÃO USUÁRIO: %s\n", msg.c_str());
    Serial.println("####################################\n");
    addLog(msg);
  }
  server.send(200, "text/plain", "OK");
}

//  GET /clearcel  —  apaga os códigos de erro / reseta a ECU (comando 21 04 03 D8)
// ---------------------------------------------------------------------
void handleClearCel() {
  clearPending = true;   // ecuPoll envia o comando no próximo ciclo
  server.send(200, "text/plain", "OK");
}

//  GET /logs  —  últimos logs em texto puro (pra copiar e colar)
// ---------------------------------------------------------------------
void handleLogs() {
  String out = "";
  int n     = (logCount < LOG_MAX) ? logCount : LOG_MAX;
  int start = (logCount > LOG_MAX) ? logHead : 0;
  for (int i = 0; i < n; i++) {
    int idx = (start + i) % LOG_MAX;
    out += logBuf[idx];
    out += "\n\n";
  }
  server.send(200, "text/plain", out);
}

// ---------------------------------------------------------------------
//  GET /  —  Dashboard principal
// ---------------------------------------------------------------------
void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Civic 98 Dash</title>
<style>
  body{background:#0a0a0a;color:#fff;font-family:sans-serif;padding:10px;text-align:center;margin:0;}
  .grid{display:grid;grid-template-columns:1fr 1fr;gap:8px;padding:10px;}
  .card{background:#141414;border-radius:12px;padding:12px;border:1px solid #222;}
  .full{grid-column:1/-1;} .label{color:#666;font-size:0.75em;text-transform:uppercase;letter-spacing:1px;}
  .value{font-size:2.4em;font-weight:bold;margin:2px 0;} .unit{font-size:0.4em;color:#444;margin-left:4px;}
  .bar-bg{background:#222;height:10px;border-radius:5px;margin-top:10px;overflow:hidden;}
  #rpm-bar{height:100%;transition:0.2s;width:0%;background:#0f0;}
  .input-area{margin:10px; padding:12px; background:#1a1a1a; border-radius:12px; border:1px solid #00aaff;}
  input[type=text]{width:60%;padding:10px;background:#000;border:1px solid #333;color:#fff;border-radius:4px;}
  button{width:30%;padding:10px;background:#00aaff;border:none;color:#fff;border-radius:4px;font-weight:bold;}
  #cel-lamp{font-size:1.2em;margin:15px 0 5px 0;}
  .cel-off{color:#1a1a1a;} .cel-on{color:#f33; text-shadow: 0 0 15px #f00;}
  .log-area{margin:10px;padding:12px;background:#111;border-radius:12px;border:1px solid #0f0;}
  textarea{width:94%;height:150px;margin-top:8px;background:#000;color:#0f0;border:1px solid #333;border-radius:6px;font-family:monospace;font-size:0.75em;}
</style>
</head><body>
  <div id='cel-lamp' class='cel-off' onclick='clearCel()' style='cursor:pointer' title='Tocar para apagar os códigos'>● CHECK ENGINE</div>
  <div id='freeze' style='color:#fa0;font-size:0.8em;margin:2px 10px 8px;'></div>
  <div class='grid'>
    <div class='card full'><div class='label'>Rotação</div><div class='value' id='v-rpm'>0<span class='unit'>RPM</span></div>
    <div class='bar-bg'><div id='rpm-bar'></div></div></div>
    <div class='card'><div class='label'>MAP</div><div class='value' id='v-map' style='color:#a0f'>0<span class='unit'>kPa</span></div></div>
    <div class='card'><div class='label'>Bateria</div><div class='value' id='v-bat' style='color:#0fa'>0<span class='unit'>V</span></div></div>
    <div class='card'><div class='label'>Ar (IAT)</div><div class='value' id='v-iat' style='color:#0af'>0<span class='unit'>°C</span></div></div>
    <div class='card'><div class='label'>Água (ECT)</div><div class='value' id='v-ect' style='color:#f55'>0<span class='unit'>°C</span></div></div>
    <div class='card'><div class='label'>TPS</div><div class='value' id='v-tps' style='color:#fa0'>0<span class='unit'>%</span></div></div>
    <div class='card'><div class='label'>Velocidade</div><div class='value' id='v-speed' style='color:#0ff'>0<span class='unit'>km/h</span></div></div>
  </div>
  <div class='input-area'><input type='text' id='note' placeholder='velocidade real (km/h)'><button onclick='sendNote()'>LOG</button></div>
  <div class='log-area'>
    <div class='label'>Últimos 10 logs — copie e cole</div>
    <textarea id='logs' readonly></textarea>
    <button onclick='copyLogs()'>COPIAR</button>
  </div>
<script>
  // Tabela de códigos de erro Honda OBD1 (DTC -> descrição) — Civic LX D16Y7
  const CEL_MAP = {
    0:"Checksum interno da ECU", 1:"O2 (sonda primária)", 2:"O2 secundária",
    3:"MAP (sinal)", 4:"CKP (virabrequim)", 5:"MAP (vácuo)",
    6:"ECT (temp. água)", 7:"TPS (borboleta)", 8:"TDC (ponto morto)",
    9:"CYP (cilindro)", 10:"IAT (temp. ar)", 12:"EGR", 13:"Baro (pressão atm.)",
    14:"IACV (marcha lenta)", 15:"Ignição (ICM)", 16:"Injetores",
    17:"VSS (velocidade)", 19:"Lock-up (câmbio)", 20:"ELD (carga elétrica)",
    21:"VTEC (solenoide)", 22:"VTEC (pressostato)", 23:"Knock (detonação)",
    30:"A/T sinal A", 31:"A/T sinal B", 41:"Aquecedor sonda primária",
    43:"Sistema de combustível", 45:"LTFT (fuel trim longo)",
    54:"CKF (flutuação virab.)", 63:"O2 secundária (sinal)",
    65:"Aquecedor sonda secundária"};
  function celCodes(err) {
    if (!err || err === "nenhum") return "— sem código";
    return err.split(",").map(c => {
      let n = CEL_MAP[parseInt(c, 10)];
      return n ? ("código " + c + ": " + n) : ("código " + c + " (desconhecido)");
    }).join("  |  ");
  }
  function clearCel() {
    if (confirm("Apagar os códigos de erro e resetar a ECU?"))
      fetch('/clearcel').then(() => alert("Comando enviado. A luz deve apagar em instantes."));
  }
  function updateDash() {
    fetch('/data').then(r => r.json()).then(d => {
      document.getElementById('v-rpm').innerHTML = d.rpm + "<span class='unit'>RPM</span>";
      document.getElementById('v-map').innerHTML = d.map + "<span class='unit'>kPa</span>";
      document.getElementById('v-bat').innerHTML = d.bat + "<span class='unit'>V</span>";
      document.getElementById('v-iat').innerHTML = d.iat + "<span class='unit'>°C</span>";
      document.getElementById('v-ect').innerHTML = d.ect + "<span class='unit'>°C</span>";
      document.getElementById('v-tps').innerHTML = d.tps + "<span class='unit'>%</span>";
      document.getElementById('v-speed').innerHTML = d.speed + "<span class='unit'>km/h</span>";
      document.getElementById('rpm-bar').style.width = Math.min((d.rpm/7500)*100, 100) + "%";
      let lamp = document.getElementById('cel-lamp');
      lamp.className = d.cel ? 'cel-on' : 'cel-off';
      lamp.innerHTML = d.cel ? ("● CHECK ENGINE " + celCodes(d.err)) : "● CHECK ENGINE";
      document.getElementById('freeze').innerHTML =
        (d.cel && d.freeze) ? ("📸 No momento do erro: " + d.freeze) : "";
      document.getElementById('rpm-bar').style.background = (d.rpm > 6500) ? '#f00' : '#0f0';
    });
  }
  function sendNote() {
    let noteEl = document.getElementById('note');
    let f = new FormData(); f.append('msg', noteEl.value);
    fetch('/log', {method:'POST', body:f}).then(() => { noteEl.value = ''; loadLogs(); });
  }
  function loadLogs() {
    fetch('/logs').then(r => r.text()).then(t => { document.getElementById('logs').value = t; });
  }
  function copyLogs() {
    let t = document.getElementById('logs');
    t.select();
    if (navigator.clipboard) navigator.clipboard.writeText(t.value);
    else document.execCommand('copy');
  }
  loadLogs();
  setInterval(updateDash, 300);
</script></body></html>
)rawliteral";
    server.send(200, "text/html", html);
}


// ---------------------------------------------------------------------
//  GET /alldata  —  Lista completa de sensores (texto cru)
// ---------------------------------------------------------------------
void handleAllDataPage() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>All Data</title>

<style>
body {
  background:#000;
  color:#0f0;
  font-family:monospace;
  padding:10px;
}

h2 {
  color:#fff;
}

.block {
  margin-bottom:20px;
  padding:10px;
  border:1px solid #0f0;
}

.line {
  display:flex;
  justify-content:space-between;
  border-bottom:1px solid #111;
  padding:4px 0;
}

.label {
  color:#0f0;
}

.value {
  color:#fff;
}
</style>
</head>

<body>

<h2>ECU FULL DATA</h2>

<div class="block" id="data"></div>

<script>
function update() {
  fetch('/data')
    .then(r => r.json())
    .then(d => {

      let html = "";
      const L = (nome, val, un) =>
        "<div class='line'><span class='label'>" + nome + "</span><span class='value'>" + val + (un ? " " + un : "") + "</span></div>";

      html += L("RPM", d.rpm);
      html += L("Velocidade", d.speed, "km/h");
      html += L("MAP", d.map, "kPa");
      html += L("Baro", d.baro, "kPa");
      html += L("TPS", d.tps, "%");
      html += L("ECT (água)", d.ect, "°C");
      html += L("IAT (ar)", d.iat, "°C");
      html += L("Bateria", d.bat, "V");
      html += L("O2", d.o2, "V");
      html += L("Ignição", d.ign, "°");
      html += L("Knock limit", d.knocklim, "°");
      html += L("Injeção", d.inj, "ms");
      html += L("IACV", d.iacv, "%");
      html += L("Fuel Trim curto", d.stft, "%");
      html += L("Fuel Trim longo", d.ltft, "%");
      html += L("Alternador", d.alt, "%");
      html += L("ELD", d.eld, "A");
      html += L("Knock (det.)", d.knock);
      html += L("CEL", d.cel ? "ON" : "OFF");
      if (d.cel) {
        html += L("Códigos", d.err || "—");
        html += L("No momento do erro", d.freeze || "—");
        html += L("Erros 0x40-5F bruto", d.errhex || "—");
      }

      // ===== Interruptores / relés / flags (SIM/não) =====
      const H = (txt) =>
        "<div class='line'><span class='label' style='color:#ff0'>" + txt + "</span><span class='value'></span></div>";
      const B = (nome, v) => L(nome, v ? "SIM" : "não");
      html += H("--- Interruptores ---");
      html += B("Freio", d.sw_brake);
      html += B("A/C (botão)", d.sw_ac);
      html += B("Direção (PAS)", d.sw_pas);
      html += B("Partida", d.sw_start);
      html += B("Pressostato VTEC", d.sw_vtecp);
      html += B("SCS (autodiag.)", d.sw_scs);
      html += B("Controle VTEC", d.sw_vtecc);
      html += H("--- Relés ---");
      html += B("Relé principal", d.rel_main);
      html += B("Embreagem A/C", d.rel_ac);
      html += B("Aquec. sonda O2", d.rel_o2h);
      html += H("--- Flags ---");
      html += B("Ventoinha", d.fl_fan);
      html += B("VTEC-E", d.fl_vtece);
      html += B("Econo", d.fl_econo);
      html += L("ECU ID", d.ecuid || "—");

      // ===== STATUS byte a byte (p/ continuar decodificando) =====
      if (d.stat) {
        let bytes = d.stat.trim().split(/\s+/);
        html += "<div class='line'><span class='label' style='color:#ff0'>--- STATUS bruto ---</span><span class='value'></span></div>";
        for (let i = 0; i < bytes.length; i++)
          html += "<div class='line'><span class='label'>b" + i + "</span><span class='value'>" +
                  parseInt(bytes[i], 16) + "  (0x" + bytes[i] + ")</span></div>";
      }

      document.getElementById("data").innerHTML = html;
    });
}

setInterval(update, 300);
</script>

</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}
