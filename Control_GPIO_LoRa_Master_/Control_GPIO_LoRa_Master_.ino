/*
  Master.ino  -  ESTAÇÃO BASE (Diagnóstico RAW)
*/

#include "Arduino.h"
#include "LoRa_E220.h"
#include "LoRaProtocol.h"

#define PIN_RX   16
#define PIN_TX   17
#define PIN_M0   21
#define PIN_M1   19
#define PIN_AUX  18

LoRa_E220 Transceiver(&Serial2, PIN_AUX, PIN_M0, PIN_M1);

bool recebendoSD = false;
uint32_t ultimoComandoEnviado = 0;

void handleCommand(String cmd);
void enviarComando(uint8_t tipoCmd);
void processarResposta(const Response& r);
void printHelp();

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial2.begin(9600, SERIAL_8N1, PIN_RX, PIN_TX);

  Serial.println(F("===== MASTER (BASE) ====="));
  Serial.print(F("sizeof(Command) = ")); Serial.println(sizeof(Command));
  Serial.print(F("sizeof(Response) = ")); Serial.println(sizeof(Response));
  Transceiver.begin();
  printHelp();
}

void loop() {
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    handleCommand(input);
  }

  if (Serial2.available() >= (int)sizeof(Response)) {
    delay(20);   // aguarda pacote completo

    uint8_t buf[64];
    int lidos = 0;
    while (Serial2.available() && lidos < 64) {
      buf[lidos++] = Serial2.read();
    }

    Serial.print(F("\n[RAW] "));
    Serial.print(lidos);
    Serial.print(F(" bytes | Hex: "));
    for (int i = 0; i < lidos; i++) {
      if (buf[i] < 0x10) Serial.print("0");
      Serial.print(buf[i], HEX);
      Serial.print(" ");
    }
    Serial.println();

    if (lidos == sizeof(Response)) {
      Response r;
      memcpy(&r, buf, sizeof(Response));
      if (r.resp >= RESP_DATA && r.resp <= RESP_SD_FIM) {
        processarResposta(r);
      } else {
        Serial.println(F("[AVISO] Pacote invalido descartado"));
      }
    } else {
      Serial.print(F("[AVISO] Tamanho inesperado: "));
      Serial.println(lidos);
    }
  }
}

void handleCommand(String cmd) {
  cmd.trim();
  cmd.toUpperCase();

  if      (cmd == "READ")  enviarComando(CMD_READ);
  else if (cmd == "PING")  enviarComando(CMD_PING);
  else if (cmd == "ARMAR") enviarComando(CMD_ARMAR);
  else if (cmd == "RESET") enviarComando(CMD_RESET_BASE);
  else if (cmd == "GPS")   enviarComando(CMD_GET_GPS);
  else if (cmd == "SD")    { recebendoSD = true; enviarComando(CMD_DOWNLOAD_SD); }
  else if (cmd == "HELP")  printHelp();
  else {
    Serial.print(F("[AVISO] Comando desconhecido: "));
    Serial.println(cmd);
  }
}

void enviarComando(uint8_t tipoCmd) {
  Command c; c.cmd = tipoCmd;
  Transceiver.sendMessage(&c, sizeof(c));
  ultimoComandoEnviado = millis();

  Serial.print(F(">> Comando enviado: 0x"));
  Serial.print(tipoCmd, HEX);
  Serial.println();

  // Espera ativa pela resposta (3000ms)
  uint32_t t0 = millis();
  bool respondeu = false;
  while (millis() - t0 < 3000) {
    if (Serial2.available() >= (int)sizeof(Response)) {
      Serial.print(F("   Resposta em "));
      Serial.print(millis() - t0);
      Serial.println(F("ms"));
      respondeu = true;
      break;
    }
    delay(10);
  }

  if (!respondeu) {
    Serial.println(F("[AVISO] Sem resposta em 3000ms"));
  }
}


void processarResposta(const Response& r) {
  // Removido o filtro de tempo — atrapalha telemetria automatica do Slave
  switch (r.resp) {
    case RESP_DATA:
      Serial.println(F("\n>> [TELEMETRIA]"));
      Serial.print(F("   Estado    : "));
      switch (r.contador) {
        case 0: Serial.println(F("AGUARDANDO")); break;
        case 1: Serial.println(F("SUBIDA"));     break;
        case 2: Serial.println(F("DESCIDA"));    break;
        case 3: Serial.println(F("POUSADO"));    break;
        default: Serial.println(r.contador);     break;
      }
      Serial.print(F("   Altitude  : ")); Serial.print(r.valor1, 2); Serial.println(F(" m"));
      Serial.print(F("   Aceleracao: ")); Serial.print(r.valor2, 2); Serial.println(F(" m/s2"));
      break;

    case RESP_GPS:
      Serial.println(F("\n>> [GPS]"));
      Serial.print(F("   Latitude  : ")); Serial.println(r.valor1, 6);
      Serial.print(F("   Longitude : ")); Serial.println(r.valor2, 6);
      break;

    case RESP_PONG:
      Serial.println(F("\n>> PONG recebido (Foguete Online)."));
      break;

    case RESP_SD_FIM:
      Serial.println(F("\n>> [SD] DOWNLOAD CONCLUIDO."));
      recebendoSD = false;
      break;
  }
}

void printHelp() {
  Serial.println(F("\n--- Comandos ---"));
  Serial.println(F(" READ  -> Telemetria | GPS   -> Coordenadas"));
  Serial.println(F(" ARMAR -> Forca Voo  | RESET -> Zera Altimetro"));
  Serial.println(F(" SD    -> Baixa Log  | PING  -> Testa Conexao"));
  Serial.println(F(" HELP  -> Este menu"));
}