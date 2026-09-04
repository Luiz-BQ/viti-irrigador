#include <WiFi.h>
#include "AdafruitIO_WiFi.h"
#include <SPI.h>
#include <LoRa.h>

// =========================================================================
// Defina 'true' para testar no Wokwi (gera dados dinâmicos para o Adafruit IO)
// Defina 'false' para usar com ESP32 + LoRa FÍSICOS REAIS
#define SIMULACAO_WOKWI true 
// =========================================================================

#define WIFI_SSID       "SEU_WIFI_AQUI"
#define WIFI_PASS       "SUA_SENHA_AQUI"

#define IO_USERNAME     "SEU_USER_AQUI"
#define IO_KEY          "SUA_KEY_AQUI"

#define SCK_PIN   18
#define MISO_PIN  19
#define MOSI_PIN  23
#define SS_PIN    5
#define RST_PIN   14
#define DIO0_PIN  2
#define LED_PIN   13

AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);

AdafruitIO_Feed *feedTemp   = io.feed("vinhedo-temperatura");
AdafruitIO_Feed *feedHum    = io.feed("vinhedo-umidade-ar");
AdafruitIO_Feed *feedSolo   = io.feed("vinhedo-umidade-solo");
AdafruitIO_Feed *feedCmd    = io.feed("vinhedo-comando-irrigacao");
AdafruitIO_Feed *feedStatus = io.feed("vinhedo-status-irrigacao");

bool loraDisponivel = false;
unsigned long ultimoEnvioSimulado = 0;

void handleComandoRemoto(AdafruitIO_Data *data) {
  bool estado = data->toBool();
  String payload = "CMD:" + String(estado ? "1" : "0");
  
  Serial.println("[GATEWAY] Comando recebido da Nuvem -> " + payload);

  if (loraDisponivel) {
    LoRa.beginPacket();
    LoRa.print(payload);
    LoRa.endPacket();
  }
}

void processarPacoteTexto(String pacote) {
  digitalWrite(LED_PIN, HIGH);
  delay(50);
  digitalWrite(LED_PIN, LOW);

  // Exemplo de pacote recebido: DATA:23.5,65.0,42.0,DESLIGADO
  if (pacote.startsWith("DATA:")) {
    pacote.remove(0, 5); // Remove o prefixo "DATA:"
    
    int c1 = pacote.indexOf(',');
    int c2 = pacote.indexOf(',', c1 + 1);
    int c3 = pacote.indexOf(',', c2 + 1);

    if (c1 > 0 && c2 > 0) {
      float t = pacote.substring(0, c1).toFloat();
      float h = pacote.substring(c1 + 1, c2).toFloat();
      float s = (c3 > 0) ? pacote.substring(c2 + 1, c3).toFloat() : pacote.substring(c2 + 1).toFloat();
      
      String statusIrrig = "";
      if (c3 > 0) {
        statusIrrig = pacote.substring(c3 + 1);
      }

      Serial.printf("[GATEWAY] Enviando Adafruit IO -> Temp: %.1f°C | Umid: %.1f%% | Solo: %.1f%% | Status: %s\n", 
                    t, h, s, statusIrrig.c_str());

      // Envia imediatamente para o Adafruit IO
      feedTemp->save(t);
      feedHum->save(h);
      feedSolo->save(s);

      if (statusIrrig.length() > 0) {
        feedStatus->save(statusIrrig);
      }
    }
  } 
  else if (pacote.startsWith("STATUS_IRRIG:")) {
    String statusMsg = pacote.substring(13);
    feedStatus->save(statusMsg);
    Serial.println("[GATEWAY] Status de Irrigacao: " + statusMsg);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);

  Serial.print("Conectando ao Adafruit IO");
  io.connect();
  feedCmd->onMessage(handleComandoRemoto);

  while (io.status() < AIO_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\n[OK] Conectado ao Adafruit IO!");

  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
  LoRa.setPins(SS_PIN, RST_PIN, DIO0_PIN);
  
  if (LoRa.begin(915E6)) {
    loraDisponivel = true;
    Serial.println("[LORA] Rádio SX1276 detectado!");
  } else {
    Serial.println("[ALERTA] Rádio LoRa não detectado. Usando modo simulação.");
  }
}

void loop() {
  io.run(); // Mantém comunicação com a nuvem

  // 1. Tenta ler via LoRa real (se houver pacote no rádio)
  if (loraDisponivel) {
    int packetSize = LoRa.parsePacket();
    if (packetSize) {
      String pacote = "";
      while (LoRa.available()) {
        pacote += (char)LoRa.read();
      }
      processarPacoteTexto(pacote);
      return;
    }
  }

  // 2. Modo Simulação Wokwi: Gera leituras dinâmicas a cada 5s para validar o Dashboard
  if (SIMULACAO_WOKWI) {
    if (millis() - ultimoEnvioSimulado >= 5000) {
      ultimoEnvioSimulado = millis();

      // Gera leituras aleatórias dentro de faixas normais
      float tempSim = 20.0 + random(-30, 80) / 10.0; // Ex: 17.0°C a 28.0°C
      float humSim = 60.0 + random(-10, 10);
      float soloSim = 45.0 + random(-15, 15);
      String statusSim = (soloSim < 35.0) ? "LIGADO" : "DESLIGADO";

      String pacoteSimulado = "DATA:" + String(tempSim, 1) + "," + 
                              String(humSim, 1) + "," + 
                              String(soloSim, 1) + "," + 
                              statusSim;

      processarPacoteTexto(pacoteSimulado);
    }
  }
}