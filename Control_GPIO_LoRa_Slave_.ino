/*
  Slave.ino  -  FOGUETE (Base funcional E220 mantida)
*/

#include "Arduino.h"
#include "LoRa_E220.h"
#include "LoRaProtocol.h"
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_MPU6050.h>
#include <SD.h>
#include <TinyGPSPlus.h>

// --- Pinos LoRa E220 (Base original do utilizador mantida) ---
#define PIN_RX   16
#define PIN_TX   17
#define PIN_M0   21
#define PIN_M1   19
#define PIN_AUX  18

// --- Pinos de Sensores Remapeados (Evita conflito com o LoRa) ---
#define I2C_SDA_PIN  25
#define I2C_SCL_PIN  27
#define SD_CS        5
#define SD_MOSI      13
#define SD_MISO      12
#define SD_SCK       14
#define GPS_RX_PIN   4
#define GPS_TX_PIN   2
#define SQUIB_PIN    26

#define BMP_ADDR 0x76

// --- Instâncias ---
LoRa_E220 Transceiver(&Serial2, PIN_AUX, PIN_M0, PIN_M1);
HardwareSerial SerialGPS(1);
TinyGPSPlus gps;
Adafruit_BMP280 bmp;
Adafruit_MPU6050 mpu;
SPIClass spiSD(HSPI); // Usa barramento secundário para o SD

// --- Variáveis de Voo ---
enum EstadoVoo { AGUARDANDO, SUBIDA, DESCIDA, POUSADO };
EstadoVoo estadoAtual = AGUARDANDO;

float altitudeBase = 0.0;
float altitudeAtual = 0.0;
float altitudeMaxima = -9999.0;
float accTotal = 0.0;
uint16_t _numLeituras = 0;

uint32_t tempoUltimoLog = 0;
uint32_t tempoUltimoLoRa = 0;
uint32_t tempoAcionamentoSquib = 0;

// Prototipação
void atenderComando(const Command& c);
void ControleDeVoo();
void InicializaSensores();
void ResetarAltitudeBase();
void SalvaDadosSD(sensors_event_t& a, sensors_event_t& g);
void EnviaDadosSDLoRa();

void setup() {
  Serial.begin(115200);
  delay(500);
  
  pinMode(SQUIB_PIN, OUTPUT);
  digitalWrite(SQUIB_PIN, LOW);

  Serial2.begin(9600, SERIAL_8N1, PIN_RX, PIN_TX);
  Transceiver.begin();

  InicializaSensores();
  ResetarAltitudeBase();

  Serial.println(F("===== SLAVE (FOGUETE) PRONTO ====="));
}

void loop() {
  // 1. GPS contínuo
  while (SerialGPS.available() > 0) {
    gps.encode(SerialGPS.read());
  }

  // 2. Escuta Comandos do Master
  if (Transceiver.available() > 1) {
    ResponseStructContainer rsc = Transceiver.receiveMessage(sizeof(Command));
    if (rsc.status.code == 1) {
      Command c;
      memcpy(&c, rsc.data, sizeof(Command));
      atenderComando(c);
    }
    rsc.close();
  }

  // 3. Lógica Inercial e SD
  ControleDeVoo();
}

// --------------------------------------------------------------------------
// LÓGICA DE VOO E NAVEGAÇÃO
// --------------------------------------------------------------------------
void ControleDeVoo() {
  uint32_t now = millis();
  altitudeAtual = bmp.readAltitude(1013.25) - altitudeBase;
  
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  accTotal = sqrt(a.acceleration.x*a.acceleration.x + a.acceleration.y*a.acceleration.y + a.acceleration.z*a.acceleration.z);

  // Máquina de Estados
  if (estadoAtual == AGUARDANDO) {
    if (accTotal > 35.0) { // ~3.5G
      estadoAtual = SUBIDA;
      Serial.println(F("[VOO] LANÇAMENTO DETECTADO!"));
    }
  } 
  else if (estadoAtual == SUBIDA) {
    if (altitudeAtual > altitudeMaxima) altitudeMaxima = altitudeAtual;
    
    // Apogeu: drop de 1m
    if ((altitudeMaxima - altitudeAtual) > 1.0) {
      estadoAtual = DESCIDA;
      digitalWrite(SQUIB_PIN, HIGH);
      tempoAcionamentoSquib = millis();
      Serial.println(F("[VOO] APOGEU! Paraquedas acionado."));
    }
  }

  // Desliga squib
  if ((now - tempoAcionamentoSquib > 2000) && tempoAcionamentoSquib > 0) {
    digitalWrite(SQUIB_PIN, LOW);
    tempoAcionamentoSquib = 0; 
  }

  // Datalogger 20Hz
  if (now - tempoUltimoLog >= 50) {
    SalvaDadosSD(a, g);
    tempoUltimoLog = now;
  }

  // Telemetria Automática 1Hz em Voo
  if (estadoAtual == SUBIDA || estadoAtual == DESCIDA) {
    if (now - tempoUltimoLoRa >= 1000) {
      Command c; c.cmd = CMD_READ; // Força uma auto-leitura
      atenderComando(c); 
      tempoUltimoLoRa = now;
    }
  }
}

// --------------------------------------------------------------------------
// PROCESSA COMANDOS DE RF
// --------------------------------------------------------------------------
void atenderComando(const Command& c) {
  Response r;
  r.timestamp = millis();
  r.contador  = _numLeituras++;

  switch (c.cmd) {
    case CMD_READ:
      r.resp   = RESP_DATA;
      r.valor1 = altitudeAtual;
      r.valor2 = accTotal;
      r.contador = estadoAtual; // Usa o contador para mandar o estado
      Transceiver.sendMessage(&r, sizeof(r));
      break;

    case CMD_GET_GPS:
      r.resp   = RESP_GPS;
      if (gps.location.isValid()) {
        r.valor1 = gps.location.lat();
        r.valor2 = gps.location.lng();
      } else {
        r.valor1 = 0.0; r.valor2 = 0.0;
      }
      Transceiver.sendMessage(&r, sizeof(r));
      break;

    case CMD_PING:
      r.resp = RESP_PONG;
      r.valor1 = 0; r.valor2 = 0;
      Transceiver.sendMessage(&r, sizeof(r));
      break;

    case CMD_ARMAR:
      estadoAtual = SUBIDA;
      break;

    case CMD_RESET_BASE:
      ResetarAltitudeBase();
      break;

    case CMD_DOWNLOAD_SD:
      if (estadoAtual == AGUARDANDO || estadoAtual == POUSADO) {
        EnviaDadosSDLoRa();
      }
      break;
  }
}

// --------------------------------------------------------------------------
// FUNÇÕES AUXILIARES (Hardware e Setup)
// --------------------------------------------------------------------------
void InicializaSensores() {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN); // I2C customizado
  Wire.setClock(400000);
  SerialGPS.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  bmp.begin(BMP_ADDR);
  bmp.setSampling(Adafruit_BMP280::MODE_NORMAL, Adafruit_BMP280::SAMPLING_X2, Adafruit_BMP280::SAMPLING_X16, Adafruit_BMP280::FILTER_X16, Adafruit_BMP280::STANDBY_MS_1); 
  
  mpu.begin();
  mpu.setAccelerometerRange(MPU6050_RANGE_16_G); 

  // Inicializa SD no barramento secundário
  spiSD.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  SD.begin(SD_CS, spiSD);
  
  File file = SD.open("/flight_data.csv", FILE_APPEND);
  if (file && file.size() == 0) {
    file.println("Tempo,Estado,Alt,AccX,AccY,AccZ,Lat,Lon");
  }
  file.close();
}

void ResetarAltitudeBase() {
  float soma = 0;
  for(int i=0; i<20; i++) { soma += bmp.readAltitude(1013.25); delay(10); }
  altitudeBase = soma / 20.0;
  altitudeMaxima = -9999.0;
}

void SalvaDadosSD(sensors_event_t& a, sensors_event_t& g) {
  File file = SD.open("/flight_data.csv", FILE_APPEND);
  if (file) {
    file.print(millis()); file.print(",");
    file.print(estadoAtual); file.print(",");
    file.print(altitudeAtual); file.print(",");
    file.print(a.acceleration.x); file.print(",");
    file.print(a.acceleration.y); file.print(",");
    file.print(a.acceleration.z); file.print(",");
    if (gps.location.isValid()) {
      file.print(gps.location.lat(), 6); file.print(",");
      file.println(gps.location.lng(), 6);
    } else {
      file.println("0,0");
    }
    file.close();
  }
}

void EnviaDadosSDLoRa() {
  File file = SD.open("/flight_data.csv", FILE_READ);
  if (file) {
    uint8_t buffer[60];
    while (file.available()) {
      int idx = 0;
      while (file.available() && idx < 60) buffer[idx++] = file.read();
      Transceiver.sendMessage(buffer, idx);
      delay(200); // Essencial para o buffer RF não sobrecarregar
    }
    file.close();
  }
  Response r; r.resp = RESP_SD_FIM;
  Transceiver.sendMessage(&r, sizeof(r));
}