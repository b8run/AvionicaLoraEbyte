/*
  Master.ino  -  ESTAÇÃO BASE (Com leitura Binária Corrigida)
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

// Variável de controlo para separar a Telemetria Binária do Texto do SD
bool recebendoSD = false; 

void handleCommand(String cmd);
void enviarComando(uint8_t tipoCmd);
void processarResposta(const Response& r);
void printHelp();

void setup() {
  Serial.begin(115200); 
  delay(500);
  Serial2.begin(9600, SERIAL_8N1, PIN_RX, PIN_TX);

  Serial.println(F("===== MASTER (BASE) ====="));
  Transceiver.begin();
  printHelp();
}

void loop() {
  // Lida com comandos digitados no PC
  if (Serial.available()) {
    String input = Serial.readString();
    handleCommand(input);
  }

  // Escuta RF de forma robusta
  if (Transceiver.available() > 0) {
    
    // --- MODO TEXTO (Apenas quando pedimos o download do SD) ---
    if (recebendoSD) {
      ResponseContainer rc = Transceiver.receiveMessage();
      if (rc.status.code == 1) {
        // Se detetar o byte de fim do SD (RESP_SD_FIM que é 4)
        if (rc.data.indexOf((char)RESP_SD_FIM) != -1) {
          Serial.println(F("\n>> [SD] DOWNLOAD CONCLUIDO."));
          recebendoSD = false; // Volta ao modo de telemetria binária
        } else {
          Serial.print(rc.data);
        }
      }
      //rc.close();
    } 
    // --- MODO BINÁRIO (Telemetria, GPS, Pong) ---
    else {
      // Só tenta ler se o buffer tiver, no mínimo, o tamanho exato da Struct
      if (Transceiver.available() >= sizeof(Response)) {
        ResponseStructContainer rsc = Transceiver.receiveMessage(sizeof(Response));
        
        if (rsc.status.code == 1) {
          Response r;
          memcpy(&r, rsc.data, sizeof(Response));
          
          // Verifica se é um pacote válido (tipos de 1 a 4). 
          // Se for lixo RF, limpa o buffer para ressincronizar.
          if (r.resp >= RESP_DATA && r.resp <= RESP_SD_FIM) {
            processarResposta(r);
          } else {
            Serial.println(F("[AVISO] Ruido RF detetado. A limpar buffer..."));
            while(Serial2.available()) Serial2.read(); // Limpa o lixo
          }
        }
        rsc.close();
      }
    }
  }
}

void handleCommand(String cmd) {
  cmd.trim();
  cmd.toUpperCase();
  
  if (cmd == "READ")       enviarComando(CMD_READ);
  else if (cmd == "PING")  enviarComando(CMD_PING);
  else if (cmd == "ARMAR") enviarComando(CMD_ARMAR);
  else if (cmd == "RESET") enviarComando(CMD_RESET_BASE);
  else if (cmd == "GPS")   enviarComando(CMD_GET_GPS);
  else if (cmd == "SD")    { recebendoSD = true; enviarComando(CMD_DOWNLOAD_SD); }
  else if (cmd == "HELP")  printHelp();
}

void enviarComando(uint8_t tipoCmd) {
  Command c; c.cmd = tipoCmd;
  Transceiver.sendMessage(&c, sizeof(c));
  Serial.println(F(">> Comando RF enviado."));
}

void processarResposta(const Response& r) {
  switch (r.resp) {
    case RESP_DATA:
      Serial.println(F("\n>> [TELEMETRIA]"));
      
      // Decodifica o estado visualmente
      Serial.print(F("   Estado    : ")); 
      if (r.contador == 0) Serial.println("AGUARDANDO");
      else if (r.contador == 1) Serial.println("SUBIDA");
      else if (r.contador == 2) Serial.println("DESCIDA");
      else if (r.contador == 3) Serial.println("POUSADO");
      else Serial.println(r.contador);

      Serial.print(F("   Altitude  : ")); Serial.print(r.valor1, 2); Serial.println(F(" m"));
      Serial.print(F("   Aceleracao: ")); Serial.print(r.valor2, 2); Serial.println(F(" m/s2"));
      break;
      
    case RESP_GPS:
      Serial.println(F("\n>> [GPS]"));
      Serial.print(F("   Latitude  : ")); Serial.println(r.valor1, 6);
      Serial.print(F("   Longitude : ")); Serial.println(r.valor2, 6);
      break;

    case RESP_SD_FIM:
      Serial.println(F("\n>> [SD] DOWNLOAD CONCLUIDO."));
      recebendoSD = false;
      break;

    case RESP_PONG:
      Serial.println(F("\n>> PONG recebido (Foguete Online)."));
      break;
  }
}

void printHelp() {
  Serial.println(F("\n--- Comandos ---"));
  Serial.println(F(" READ  -> Telemetria | GPS   -> Coordenadas"));
  Serial.println(F(" ARMAR -> Forca Voo  | RESET -> Zera Altimetro"));
  Serial.println(F(" SD    -> Baixa Log  | PING  -> Testa Conexao"));
}