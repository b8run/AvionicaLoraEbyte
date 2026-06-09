/*
  Slave.ino  -  FOGUETE (Base funcional E220 mantida)
  Sensor de pressão: BMP180
  v3.0 — LED RGB + Buzzer + Beacon pós-pouso
*/

#include "Arduino.h"
#include "LoRa_E220.h"
#include "LoRaProtocol.h"
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP085.h>
#include <Adafruit_MPU6050.h>
#include <SD.h>
#include <TinyGPSPlus.h>

// --- Pinos LoRa E220 ---
#define PIN_RX   16
#define PIN_TX   17
#define PIN_M0   4
#define PIN_M1   13
#define PIN_AUX  35

// --- Pinos Sensores ---
#define SD_CS        5
#define SD_MOSI      23
#define SD_MISO      19
#define SD_SCK       18
#define GPS_RX_PIN   36
#define GPS_TX_PIN   14

// --- Pinos Atuadores ---
#define SQUIB_PIN    33
#define LED_R        26
#define LED_G        27
#define LED_B        25
#define BUZZER_PIN   32
#define BMP_ADDR     0x77

#define BUZZER_RES   8

// --------------------------------------------------------------------------
// LED E BUZZER — funções base
// --------------------------------------------------------------------------
void ledSet(bool r, bool g, bool b) {
  digitalWrite(LED_R, r ? HIGH : LOW);
  digitalWrite(LED_G, g ? HIGH : LOW);
  digitalWrite(LED_B, b ? HIGH : LOW);
}
void ledOff()      { ledSet(false, false, false); }
void ledVermelho() { ledSet(true,  false, false); }
void ledAmarelo()  { ledSet(true,  true,  false); }
void ledVerde()    { ledSet(false, true,  false); }
void ledAzul()     { ledSet(false, false, true);  }
void ledBranco()   { ledSet(true,  true,  true);  }

void buzzerTom(uint16_t freq, uint16_t durMs) {
  if (freq == 0) {
    ledcWrite(BUZZER_PIN, 0);
  } else {
    ledcWriteTone(BUZZER_PIN, freq);
    ledcWrite(BUZZER_PIN, 128);
  }
  delay(durMs);
  ledcWrite(BUZZER_PIN, 0);
}

void buzzerSilencio(uint16_t durMs) {
  ledcWrite(BUZZER_PIN, 0);
  delay(durMs);
}

// --------------------------------------------------------------------------
// SEQUÊNCIAS VISUAIS E SONORAS
// --------------------------------------------------------------------------

// Boot — branco 3× + 3 bipes
void sinalBoot() {
  for (int i = 0; i < 3; i++) {
    ledBranco();
    buzzerTom(1000, 80);
    ledOff();
    delay(80);
  }
}

// Sensor OK — N piscadas verdes + bipe por piscada
void sinalSensorOK(uint8_t n) {
  for (uint8_t i = 0; i < n; i++) {
    ledVerde();
    buzzerTom(1800, 60);
    ledOff();
    delay(120);
  }
}

// Sensor FALHA — vermelho 3× + tom grave
void sinalSensorFalha() {
  for (int i = 0; i < 3; i++) {
    ledVermelho();
    buzzerTom(200, 100);
    ledOff();
    delay(80);
  }
}

// LoRa OK — azul 4× + bipes médios
void sinalLoRaOK() {
  for (int i = 0; i < 4; i++) {
    ledAzul();
    buzzerTom(1200, 60);
    ledOff();
    delay(120);
  }
}

// Sistema pronto — amarelo fixo + sequência ascendente
void sinalPronto() {
  ledAmarelo();
  buzzerTom(800,  80); buzzerSilencio(40);
  buzzerTom(1000, 80); buzzerSilencio(40);
  buzzerTom(1200, 80); buzzerSilencio(40);
  buzzerTom(1600, 120);
}

// Lançamento — verde fixo + sirene ascendente
void sinalLancamento() {
  ledVerde();
  for (int f = 800; f <= 2400; f += 200) {
    ledcWriteTone(BUZZER_PIN, f);
    ledcWrite(BUZZER_PIN, 128);
    delay(40);
  }
  ledcWrite(BUZZER_PIN, 0);
}

// Apogeu — pisca vermelho+amarelo 6× + alarme
void sinalApogeu() {
  for (int i = 0; i < 6; i++) {
    ledVermelho();
    buzzerTom(2000, 60);
    ledAmarelo();
    buzzerTom(1400, 60);
  }
  ledOff();
  buzzerSilencio(50);
}

// Squib desligado — amarelo + 2 bipes
void sinalSquibDesligado() {
  ledAmarelo();
  buzzerTom(1000, 150); buzzerSilencio(100);
  buzzerTom(1000, 150);
  ledOff();
}

// Pouso — verde 3× lento + descendente + vermelho fixo
void sinalPouso() {
  for (int i = 0; i < 3; i++) {
    ledVerde();
    buzzerTom(1600, 150);
    ledOff();
    delay(150);
  }
  buzzerSilencio(100);
  for (int f = 1600; f >= 400; f -= 200) {
    ledcWriteTone(BUZZER_PIN, f);
    ledcWrite(BUZZER_PIN, 128);
    delay(60);
  }
  ledcWrite(BUZZER_PIN, 0);
  ledVermelho();
}

// GPS de pouso enviado — azul 2× + bipe duplo + volta vermelho
void sinalGPSEnviado() {
  for (int i = 0; i < 2; i++) {
    ledAzul();
    buzzerTom(1400, 80);
    ledOff();
    delay(80);
  }
  ledVermelho();
}

// Beacon de localização — 5s de bipes 2200Hz a cada 90s
void sinalBeacon() {
  Serial.println(F("[BEACON] Sinal de localizacao ativo (5s)"));
  uint32_t inicio = millis();
  while (millis() - inicio < 5000) {
    ledAmarelo();
    buzzerTom(2200, 80);
    ledOff();
    delay(170);
  }
  ledVermelho();
}

// Erro crítico — vermelho piscando ∞ + bipe grave
void sinalErroCritico() {
  while (1) {
    ledVermelho();
    buzzerTom(300, 400);
    ledOff();
    buzzerSilencio(200);
  }
}

// --------------------------------------------------------------------------
// Instâncias
// --------------------------------------------------------------------------
LoRa_E220 Transceiver(&Serial2, PIN_AUX, PIN_M0, PIN_M1);
HardwareSerial SerialGPS(1);
TinyGPSPlus gps;
Adafruit_BMP085 bmp;
Adafruit_MPU6050 mpu;
SPIClass spiSD(HSPI);

// --- Flags de status ---
bool statusBMP   = false;
bool statusMPU   = false;
bool statusSD    = false;
bool statusLoRa  = false;
bool statusSquib = false;

// --- Baseline ---
float altitudeAtual  = 0.0;
float altitudeMaxima = -9999.0;
float pressaoBase    = 0.0;

// --- Nome do arquivo de voo ---
char nomeArquivoVoo[20] = "/voo_001.csv";

// --- Variáveis de Voo ---
enum EstadoVoo { AGUARDANDO, SUBIDA, DESCIDA, POUSADO };
EstadoVoo estadoAtual = AGUARDANDO;

float    accTotal              = 0.0;
uint16_t _numLeituras          = 0;
uint32_t tempoUltimoLog        = 0;
uint32_t tempoUltimoLoRa       = 0;
uint32_t tempoUltimoBMP        = 0;
uint32_t tempoAcionamentoSquib = 0;
uint32_t tempoPouso            = 0;
bool     gpsPousoEnviado       = false;
bool     beaconAtivo           = false;
uint32_t tempoUltimoBeacon     = 0;

// --- BMP assíncrono ---
bool     bmpConvertendo     = false;
uint32_t bmpInicioConversao = 0;
#define  BMP_CONV_MS        26

// --------------------------------------------------------------------------
// BUFFER SD EM RAM
// --------------------------------------------------------------------------
#define SD_BUFFER_MAX 40

struct LogLine {
  uint32_t tempo;
  uint8_t  estado;
  float    altitude;
  float    accX, accY, accZ;
  float    lat, lon;
};

LogLine sdBuffer[SD_BUFFER_MAX];
uint8_t sdBufferIdx = 0;

void FlushBufferSD() {
  if (!statusSD || sdBufferIdx == 0) return;
  File file = SD.open(nomeArquivoVoo, FILE_APPEND);
  if (!file) {
    Serial.println(F("[SD] ERRO ao abrir arquivo para flush!"));
    return;
  }
  for (uint8_t i = 0; i < sdBufferIdx; i++) {
    file.print(sdBuffer[i].tempo);       file.print(",");
    file.print(sdBuffer[i].estado);      file.print(",");
    file.print(sdBuffer[i].altitude, 2); file.print(",");
    file.print(sdBuffer[i].accX, 3);     file.print(",");
    file.print(sdBuffer[i].accY, 3);     file.print(",");
    file.print(sdBuffer[i].accZ, 3);     file.print(",");
    if (sdBuffer[i].lat != 0.0 || sdBuffer[i].lon != 0.0) {
      file.print(sdBuffer[i].lat, 6); file.print(",");
      file.println(sdBuffer[i].lon, 6);
    } else {
      file.println("0,0");
    }
  }
  file.close();
  Serial.print(F("[SD] Flush: "));
  Serial.print(sdBufferIdx);
  Serial.print(F(" linhas → "));
  Serial.println(nomeArquivoVoo);
  sdBufferIdx = 0;
}

void SalvaDadosSD(sensors_event_t& a, sensors_event_t& g) {
  if (!statusSD) return;
  if (sdBufferIdx < SD_BUFFER_MAX) {
    sdBuffer[sdBufferIdx].tempo    = millis();
    sdBuffer[sdBufferIdx].estado   = estadoAtual;
    sdBuffer[sdBufferIdx].altitude = altitudeAtual;
    sdBuffer[sdBufferIdx].accX     = a.acceleration.x;
    sdBuffer[sdBufferIdx].accY     = a.acceleration.y;
    sdBuffer[sdBufferIdx].accZ     = a.acceleration.z;
    sdBuffer[sdBufferIdx].lat      = gps.location.isValid() ? gps.location.lat() : 0.0;
    sdBuffer[sdBufferIdx].lon      = gps.location.isValid() ? gps.location.lng() : 0.0;
    sdBufferIdx++;
  }
  if (sdBufferIdx >= SD_BUFFER_MAX) FlushBufferSD();
}

// --------------------------------------------------------------------------
// CRIAR ARQUIVO DE VOO
// --------------------------------------------------------------------------
void criarArquivoVoo() {
  uint8_t num = 1;
  while (num < 255) {
    sprintf(nomeArquivoVoo, "/voo_%03d.csv", num);
    if (!SD.exists(nomeArquivoVoo)) break;
    num++;
  }
  File csv = SD.open(nomeArquivoVoo, FILE_WRITE);
  if (csv) {
    csv.println("Tempo,Estado,Alt,AccX,AccY,AccZ,Lat,Lon");
    csv.close();
    Serial.print(F("[SD] Arquivo criado: "));
    Serial.println(nomeArquivoVoo);
  } else {
    Serial.println(F("[SD] ERRO ao criar arquivo!"));
  }
}

// --------------------------------------------------------------------------
// BMP ASSÍNCRONO
// --------------------------------------------------------------------------
void bmpDispararConversao() {
  Wire.beginTransmission(BMP_ADDR);
  Wire.write(0x34 + (3 << 6));
  Wire.endTransmission();
  bmpInicioConversao = millis();
  bmpConvertendo     = true;
}

float bmpLerResultado() {
  Wire.beginTransmission(BMP_ADDR);
  Wire.write(0xF6);
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)BMP_ADDR, (uint8_t)3);
  if (Wire.available() < 3) return altitudeAtual;
  Wire.read(); Wire.read(); Wire.read();
  bmpConvertendo = false;
  int32_t pressRaw = bmp.readPressure();
  if (pressRaw < 30000 || pressRaw > 110000) return altitudeAtual;
  float pressHPa = pressRaw / 100.0;
  return 44330.0 * (1.0 - pow(pressHPa / pressaoBase, 0.1903));
}

// --------------------------------------------------------------------------
// BASELINE
// --------------------------------------------------------------------------
float setBaseline(int amostras) {
  Serial.print(F("  Calculando baseline ("));
  Serial.print(amostras);
  Serial.println(F(" amostras)..."));
  double soma    = 0;
  int    validas = 0;
  for (int i = 0; i < amostras; i++) {
    int32_t press = bmp.readPressure();
    if (press > 30000 && press < 110000) {
      soma += press / 100.0;
      validas++;
    }
    delay(100);
  }
  if (validas == 0) {
    Serial.println(F("  ERRO: nenhuma amostra valida!"));
    return 0.0;
  }
  float baseline = soma / validas;
  Serial.print(F("  Baseline: "));
  Serial.print(baseline, 4);
  Serial.print(F(" hPa ("));
  Serial.print(validas);
  Serial.println(F(" amostras validas)"));
  return baseline;
}

// --------------------------------------------------------------------------
// VERIFICAÇÃO DO SISTEMA
// --------------------------------------------------------------------------
void VerificacaoSistema() {
  Serial.println(F("========================================"));
  Serial.println(F("  VERIFICACAO DO SISTEMA"));
  Serial.println(F("========================================"));

  // BMP180 — 2 piscadas verdes
  Wire.beginTransmission(BMP_ADDR);
  byte errBMP = Wire.endTransmission();
  if (errBMP == 0) {
    statusBMP = bmp.begin();
    if (statusBMP) {
      float p = bmp.readPressure() / 100.0;
      float t = bmp.readTemperature();
      if (p > 300.0 && p < 1100.0) {
        Serial.print(F("  [OK] BMP180 | "));
        Serial.print(p, 2); Serial.print(F(" hPa | "));
        Serial.print(t, 1); Serial.println(F(" C"));
        sinalSensorOK(2);
      } else {
        statusBMP = false;
        Serial.println(F("  [FALHA] BMP180 | Leitura invalida"));
        sinalSensorFalha();
      }
    } else {
      Serial.println(F("  [FALHA] BMP180 | begin() false"));
      sinalSensorFalha();
    }
  } else {
    Serial.print(F("  [FALHA] BMP180 | I2C erro ")); Serial.println(errBMP);
    sinalSensorFalha();
  }
  delay(300);

  // MPU6050 — 3 piscadas verdes
  Wire.beginTransmission(0x68);
  byte errMPU = Wire.endTransmission();
  if (errMPU == 0) {
    statusMPU = mpu.begin();
    if (statusMPU) {
      mpu.setAccelerometerRange(MPU6050_RANGE_16_G);
      mpu.setGyroRange(MPU6050_RANGE_500_DEG);
      mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
      sensors_event_t a, g, t;
      mpu.getEvent(&a, &g, &t);
      float acc = sqrt(a.acceleration.x * a.acceleration.x +
                       a.acceleration.y * a.acceleration.y +
                       a.acceleration.z * a.acceleration.z);
      Serial.print(F("  [OK] MPU6050 | "));
      Serial.print(acc, 2); Serial.print(F(" m/s2 | "));
      Serial.print(t.temperature, 1); Serial.println(F(" C"));
      sinalSensorOK(3);
    } else {
      Serial.println(F("  [FALHA] MPU6050 | begin() false"));
      sinalSensorFalha();
    }
  } else {
    Serial.print(F("  [FALHA] MPU6050 | I2C erro ")); Serial.println(errMPU);
    sinalSensorFalha();
  }
  delay(300);

  // SD Card
  spiSD.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  statusSD = SD.begin(SD_CS, spiSD);
  if (statusSD) {
    uint64_t tam = SD.cardSize() / (1024 * 1024);
    Serial.print(F("  [OK] SD Card | "));
    Serial.print((uint32_t)tam); Serial.println(F(" MB"));
    File f = SD.open("/teste_boot.txt", FILE_WRITE);
    if (f) { f.println("ok"); f.close(); SD.remove("/teste_boot.txt"); }
  } else {
    Serial.println(F("  [FALHA] SD Card"));
  }
  delay(300);

  // GPS — 5 piscadas verdes
  SerialGPS.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  delay(1000);
  bool gpsOk = false;
  uint32_t t0 = millis();
  while (millis() - t0 < 1000) {
    if (SerialGPS.available()) { gpsOk = true; break; }
  }
  if (gpsOk) {
    Serial.println(F("  [OK] GPS | Recebendo NMEA"));
    sinalSensorOK(5);
  } else {
    Serial.println(F("  [FALHA] GPS"));
    sinalSensorFalha();
  }
  delay(300);

  // LoRa — azul 4 piscadas
  Serial2.begin(9600, SERIAL_8N1, PIN_RX, PIN_TX);
  Transceiver.begin();
  ResponseStructContainer c = Transceiver.getConfiguration();
  statusLoRa = (c.status.code == 1);
  if (statusLoRa) {
    Serial.println(F("  [OK] LoRa E220"));
    sinalLoRaOK();
  } else {
    Serial.println(F("  [FALHA] LoRa E220"));
    sinalSensorFalha();
  }
  c.close();
  delay(300);

  // Squib
  pinMode(SQUIB_PIN, OUTPUT);
  digitalWrite(SQUIB_PIN, LOW);
  statusSquib = true;
  Serial.println(F("  [OK] Squib | LOW (seguro)"));

  // Resumo serial
  Serial.println(F("----------------------------------------"));
  Serial.print(F("  BMP180:  ")); Serial.println(statusBMP  ? F("OK") : F("FALHA"));
  Serial.print(F("  MPU6050: ")); Serial.println(statusMPU  ? F("OK") : F("FALHA"));
  Serial.print(F("  SD Card: ")); Serial.println(statusSD   ? F("OK") : F("FALHA"));
  Serial.print(F("  LoRa:    ")); Serial.println(statusLoRa ? F("OK") : F("FALHA"));
  Serial.println(F("  Squib:   OK (LOW)"));
  Serial.println(F("----------------------------------------"));

  if (!statusBMP || !statusMPU) {
    Serial.println(F("  ERRO CRITICO: travado por seguranca."));
    sinalErroCritico();
  }
  if (!statusSD)   Serial.println(F("  AVISO: SD ausente — sem datalogger!"));
  if (!statusLoRa) Serial.println(F("  AVISO: LoRa ausente — sem telemetria!"));
  Serial.println(F("  Sistema OK."));
  Serial.println(F("========================================\n"));
}

// --------------------------------------------------------------------------
// SETUP
// --------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(500);

  // Pinos de saída
  pinMode(SQUIB_PIN, OUTPUT);
  digitalWrite(SQUIB_PIN, LOW);
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  ledOff();

  // Buzzer LEDC
  ledcAttach(BUZZER_PIN, 2000, BUZZER_RES);
  ledcWrite(BUZZER_PIN, 0);

  // Boot
  ledVermelho();
  sinalBoot();

  Wire.begin(21, 22);
  Wire.setClock(100000);

  VerificacaoSistema();

  // Estabilização — vermelho pulsando
  Serial.println(F("Aguardando estabilizacao (3s)..."));
  for (int i = 0; i < 6; i++) {
    ledVermelho(); delay(250);
    ledOff();      delay(250);
  }

  sensors_event_t a, g, t;
  for (int i = 0; i < 50; i++) {
    mpu.getEvent(&a, &g, &t);
    delay(20);
  }

  pressaoBase = setBaseline(50);
  if (pressaoBase == 0.0) {
    Serial.println(F("ERRO CRITICO: Baseline invalida!"));
    sinalErroCritico();
  }

  if (statusSD) criarArquivoVoo();

  altitudeMaxima = -9999.0;
  bmpDispararConversao();

  // Pronto — amarelo + sequência ascendente
  sinalPronto();
  Serial.println(F("===== SLAVE (FOGUETE) PRONTO ====="));
}

// --------------------------------------------------------------------------
// LOOP
// --------------------------------------------------------------------------
void loop() {
  // 1. GPS
  while (SerialGPS.available() > 0) {
    gps.encode(SerialGPS.read());
  }

  // 2. LoRa
  if (Transceiver.available() > 0) {
    ResponseStructContainer rsc = Transceiver.receiveMessage(sizeof(Command));
    if (rsc.status.code == 1) {
      Command c;
      memcpy(&c, rsc.data, sizeof(Command));
      atenderComando(c);
    }
    rsc.close();
  }

  // 3. BMP assíncrono
  uint32_t now = millis();
  if (bmpConvertendo && (now - bmpInicioConversao >= BMP_CONV_MS)) {
    altitudeAtual  = bmpLerResultado();
    tempoUltimoBMP = now;
    if (sdBufferIdx >= SD_BUFFER_MAX) FlushBufferSD();
  }
  if (!bmpConvertendo && (now - tempoUltimoBMP >= 24)) {
    bmpDispararConversao();
  }

  // 4. Voo
  ControleDeVoo();
}

// --------------------------------------------------------------------------
// LÓGICA DE VOO
// --------------------------------------------------------------------------
void ControleDeVoo() {
  uint32_t now = millis();

  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  accTotal = sqrt(a.acceleration.x * a.acceleration.x +
                  a.acceleration.y * a.acceleration.y +
                  a.acceleration.z * a.acceleration.z);

  // --- Máquina de estados ---
  if (estadoAtual == AGUARDANDO) {
    static uint8_t contadorLancamento = 0;
    if (accTotal > 35.0) {
      contadorLancamento++;
      if (contadorLancamento >= 5) {
        estadoAtual = SUBIDA;
        contadorLancamento = 0;
        sinalLancamento();
        Serial.println(F("[VOO] LANCAMENTO DETECTADO!"));
      }
    } else {
      contadorLancamento = 0;
    }
  }
  else if (estadoAtual == SUBIDA) {
    if (altitudeAtual > altitudeMaxima) altitudeMaxima = altitudeAtual;

    static uint8_t contadorApogeu = 0;
    if ((altitudeMaxima - altitudeAtual) > 1.5) {
      contadorApogeu++;
      if (contadorApogeu >= 3) {
        estadoAtual = DESCIDA;
        digitalWrite(SQUIB_PIN, HIGH);   // squib PRIMEIRO
        tempoAcionamentoSquib = now;
        contadorApogeu = 0;
        sinalApogeu();   // pisca vermelho+amarelo — não bloqueia o squib
        Serial.print(F("[VOO] APOGEU CONFIRMADO! Alt max: "));
        Serial.print(altitudeMaxima, 2);
        Serial.println(F(" m — Paraquedas acionado."));
      }
    } else {
      contadorApogeu = 0;
    }
  }
  else if (estadoAtual == DESCIDA) {
    ledVerde();

    static uint8_t contadorPouso = 0;
    if (accTotal > 8.5 && accTotal < 11.0 && altitudeAtual < 10.0) {
      contadorPouso++;
      if (contadorPouso >= 10) {
        estadoAtual       = POUSADO;
        tempoPouso        = now;
        gpsPousoEnviado   = false;
        beaconAtivo       = true;
        tempoUltimoBeacon = now;   // primeiro beacon em 90s
        contadorPouso     = 0;
        FlushBufferSD();
        sinalPouso();
        Serial.println(F("[VOO] POUSO DETECTADO!"));
      }
    } else {
      contadorPouso = 0;
    }
  }
  else if (estadoAtual == POUSADO) {
    // GPS de pouso — 1 envio automático após 1s
    if (!gpsPousoEnviado && (now - tempoPouso >= 1000)) {
      Response r;
      r.timestamp = millis();
      r.contador  = _numLeituras++;
      r.resp      = RESP_GPS;
      r.valor1    = gps.location.isValid() ? gps.location.lat() : 0.0;
      r.valor2    = gps.location.isValid() ? gps.location.lng() : 0.0;
      Transceiver.sendMessage(&r, sizeof(r));
      gpsPousoEnviado = true;
      sinalGPSEnviado();
      Serial.print(F("[VOO] GPS de pouso enviado: "));
      Serial.print(r.valor1, 6); Serial.print(F(", "));
      Serial.println(r.valor2, 6);
    }

    // Beacon a cada 90s — 5s de bipes 2200Hz
    if (beaconAtivo && (now - tempoUltimoBeacon >= 90000)) {
      tempoUltimoBeacon = now;
      sinalBeacon();
    }
  }

  // Desliga squib após 2s → flush → sinal
  if (tempoAcionamentoSquib > 0 && (now - tempoAcionamentoSquib > 2000)) {
    digitalWrite(SQUIB_PIN, LOW);
    tempoAcionamentoSquib = 0;
    FlushBufferSD();
    sinalSquibDesligado();
    Serial.println(F("[VOO] Squib desligado."));
  }

  // Datalogger 20Hz — RAM
  if (now - tempoUltimoLog >= 50) {
    SalvaDadosSD(a, g);
    tempoUltimoLog = now;
  }

  // Telemetria 750ms em voo
  if (estadoAtual == SUBIDA || estadoAtual == DESCIDA) {
    if (now - tempoUltimoLoRa >= 750) {
      Command c; c.cmd = CMD_READ;
      atenderComando(c);
      tempoUltimoLoRa = now;
    }
  }

  // Flush de segurança no solo a cada 10s
  static uint32_t tempoUltimoFlush = 0;
  if (estadoAtual == AGUARDANDO && (now - tempoUltimoFlush >= 10000)) {
    if (sdBufferIdx > 0) FlushBufferSD();
    tempoUltimoFlush = now;
  }
}

// --------------------------------------------------------------------------
// COMANDOS RF
// --------------------------------------------------------------------------
void atenderComando(const Command& c) {
  Serial.print(F("[LORA] Comando: "));
  switch (c.cmd) {
    case CMD_READ:        Serial.println(F("READ"));        break;
    case CMD_GET_GPS:     Serial.println(F("GET_GPS"));     break;
    case CMD_PING:        Serial.println(F("PING"));        break;
    case CMD_ARMAR:       Serial.println(F("ARMAR"));       break;
    case CMD_RESET_BASE:  Serial.println(F("RESET_BASE"));  break;
    case CMD_DOWNLOAD_SD: Serial.println(F("DOWNLOAD_SD")); break;
    default:
      Serial.print(F("DESCONHECIDO (0x")); Serial.print(c.cmd, HEX); Serial.println(F(")"));
      break;
  }

  Response r;
  r.timestamp = millis();
  r.contador  = _numLeituras++;

  switch (c.cmd) {
    case CMD_READ:
      r.resp     = RESP_DATA;
      r.valor1   = altitudeAtual;
      r.valor2   = accTotal;
      r.contador = estadoAtual;
      Transceiver.sendMessage(&r, sizeof(r));
      break;
    case CMD_GET_GPS:
      r.resp   = RESP_GPS;
      r.valor1 = gps.location.isValid() ? gps.location.lat() : 0.0;
      r.valor2 = gps.location.isValid() ? gps.location.lng() : 0.0;
      Transceiver.sendMessage(&r, sizeof(r));
      break;
    case CMD_PING:
      r.resp = RESP_PONG; r.valor1 = 0; r.valor2 = 0;
      Transceiver.sendMessage(&r, sizeof(r));
      break;
    case CMD_ARMAR:
      estadoAtual = SUBIDA;
      Serial.println(F("[CMD] Armado manualmente."));
      break;
    case CMD_RESET_BASE:
      pressaoBase    = setBaseline(50);
      altitudeMaxima = -9999.0;
      Serial.println(F("[CMD] Baseline resetada."));
      break;
    case CMD_DOWNLOAD_SD:
      if (estadoAtual == AGUARDANDO || estadoAtual == POUSADO) {
        FlushBufferSD();
        EnviaDadosSDLoRa();
      }
      break;
  }
}

// --------------------------------------------------------------------------
// ENVIO SD VIA LORA
// --------------------------------------------------------------------------
void EnviaDadosSDLoRa() {
  if (!statusSD) return;
  File file = SD.open(nomeArquivoVoo, FILE_READ);
  if (file) {
    uint8_t buffer[60];
    while (file.available()) {
      int idx = 0;
      while (file.available() && idx < 60) buffer[idx++] = file.read();
      Transceiver.sendMessage(buffer, idx);
      delay(200);
    }
    file.close();
  }
  Response r; r.resp = RESP_SD_FIM;
  Transceiver.sendMessage(&r, sizeof(r));
}