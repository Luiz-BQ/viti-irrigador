#include <SPI.h>
#include <LoRa.h>
#include <DHT.h>

// =========================================================================
// CONFIGURAÇÃO DE AMBIENTE
#define SIMULACAO_WOKWI true // Mude para 'false' para o ESP32 físico real
// =========================================================================

// Definição dos Pinos LoRa (SX1276)
#define SCK_PIN   18
#define MISO_PIN  19
#define MOSI_PIN  23
#define SS_PIN    5
#define RST_PIN   14
#define DIO0_PIN  2

// Definição dos Sensores e Atuadores
#define DHTPIN        4
#define DHTTYPE       DHT22 // Sensor DHT22 (-40 °C a +80 °C)
#define SOIL_PIN      34    // Entrada Analógica (ADC1)
#define RELAY_PIN     26    // Controle do Relé da Válvula de Irrigação

// Limiares para Viticultura
const float UMIDADE_SOLO_MIN = 30.0; // Ativa irrigação em < 30%
const float UMIDADE_SOLO_MAX = 55.0; // Desliga irrigação em >= 55%
const float TEMP_ALERTA_GEADA = 2.0; // Alerta de risco de geada (<= 2 °C)

// Calibração do Sensor Capacitivo no Wokwi (Potenciômetro: 0 a 4095)
const int VALOR_SOLO_SECO = 4095;
const int VALOR_SOLO_MOLHADO = 0;

DHT dht(DHTPIN, DHTTYPE);

bool loraDisponivel = false;
bool irrigacaoAtiva = false;
bool modoManual = false;
bool alertaGeadaAtivo = false;

unsigned long ultimoEnvio = 0;
unsigned long ultimaLeituraDHT = 0;

const unsigned long INTERVALO_ENVIO = SIMULACAO_WOKWI ? 10000 : 360000;
const unsigned long INTERVALO_DHT = 2500; // Timer seguro para leitura do DHT22

float lerUmidadeSolo() {
  int leituraRaw = analogRead(SOIL_PIN);
  float porcentagem = map(leituraRaw, VALOR_SOLO_SECO, VALOR_SOLO_MOLHADO, 0, 100);
  return constrain(porcentagem, 0.0, 100.0);
}

void controlarIrrigacao(bool ligar, String motivo) {
  irrigacaoAtiva = ligar;
  digitalWrite(RELAY_PIN, irrigacaoAtiva ? HIGH : LOW);
  
  String statusTexto = irrigacaoAtiva ? "LIGADO" : "DESLIGADO";
  Serial.printf("[CAMPO] Irrigacao %s | Motivo: %s\n", statusTexto.c_str(), motivo.c_str());

  if (loraDisponivel) {
    String packet = "STATUS_IRRIG:" + statusTexto + " (" + motivo + ")";
    LoRa.beginPacket();
    LoRa.print(packet);
    LoRa.endPacket();
  }
}

void verificarRiscoGeada(float tempAr) {
  if (tempAr <= TEMP_ALERTA_GEADA) {
    if (!alertaGeadaAtivo) {
      alertaGeadaAtivo = true;
      Serial.printf("[PERIGO] ALERTA DE GEADA DETECTADO! Temp: %.1f °C\n", tempAr);
      
      if (loraDisponivel) {
        String alertPacket = "ALERTA_GEADA:" + String(tempAr, 1);
        LoRa.beginPacket();
        LoRa.print(alertPacket);
        LoRa.endPacket();
      }
    }
  } else {
    alertaGeadaAtivo = false; // Temperatura normalizada
  }
}

void enviarDadosSensores() {
  float tempAr = dht.readTemperature();
  float humAr = dht.readHumidity();
  float umidSolo = lerUmidadeSolo();

  if (isnan(tempAr) || isnan(humAr)) {
    Serial.println("[ALERTA] Falha na leitura do DHT22! Utilizando valores padrão.");
    tempAr = 25.0;
    humAr = 60.0;
  }

  verificarRiscoGeada(tempAr);

  String statusTexto = irrigacaoAtiva ? "LIGADO" : "DESLIGADO";
  
  // Formato compatível com o Receptor: DATA:temp,umidAr,umidSolo,status
  String payload = "DATA:" + String(tempAr, 1) + "," + 
                   String(humAr, 1) + "," + 
                   String(umidSolo, 1) + "," + 
                   statusTexto;

  Serial.println("[CAMPO] Transmitindo payload: " + payload);

  if (loraDisponivel) {
    LoRa.beginPacket();
    LoRa.print(payload);
    LoRa.endPacket();
  }
}

void processarComandoRemoto(String comando) {
  if (comando.startsWith("CMD:")) {
    int val = comando.substring(4).toInt();
    if (val == 1) {
      modoManual = true;
      controlarIrrigacao(true, "MANUAL_ON");
    } else if (val == 0) {
      modoManual = false;
      controlarIrrigacao(false, "MANUAL_OFF");
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  dht.begin();

  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
  LoRa.setPins(SS_PIN, RST_PIN, DIO0_PIN);
  
  if (LoRa.begin(915E6)) {
    loraDisponivel = true;
    Serial.println("[LORA] Radio SX1276 inicializado com sucesso!");
  } else {
    Serial.println("[ALERTA] LoRa nao detectado! Executando em Modo Simulacao.");
    if (!SIMULACAO_WOKWI) {
      while (1);
    }
  }
  
  Serial.println(">>> Nó do Vinhedo Pronto (DHT22 com Alerta de Geada) <<<");
}

void loop() {
  // Lógica Automática de Irrigação
  if (!modoManual) {
    float umidSolo = lerUmidadeSolo();
    if (umidSolo < UMIDADE_SOLO_MIN && !irrigacaoAtiva) {
      controlarIrrigacao(true, "AUTO_SECO");
    } else if (umidSolo >= UMIDADE_SOLO_MAX && irrigacaoAtiva) {
      controlarIrrigacao(false, "AUTO_SUFICIENTE");
    }
  }

  // Verificação temporizada de geada (respeitando o tempo mínimo de amostragem do DHT22)
  if (millis() - ultimaLeituraDHT >= INTERVALO_DHT) {
    ultimaLeituraDHT = millis();
    float tempAtual = dht.readTemperature();
    if (!isnan(tempAtual)) {
      verificarRiscoGeada(tempAtual);
    }
  }

  // Envio periódico dos dados via LoRa
  if (millis() - ultimoEnvio >= INTERVALO_ENVIO) {
    ultimoEnvio = millis();
    enviarDadosSensores();
  }

  // Escuta comandos remotos vindos do Gateway
  if (loraDisponivel) {
    int packetSize = LoRa.parsePacket();
    if (packetSize) {
      String comando = "";
      while (LoRa.available()) {
        comando += (char)LoRa.read();
      }
      processarComandoRemoto(comando);
    }
  }
}