// =====================================================================
//  ALERTA DE ALAGAMENTO EM VIAS URBANAS (Arduino UNO)
//
//  Problema: ruas e túneis que enchem de água em chuva forte e carros
//  que entram sem saber o tamanho do perigo.
//
//  Solução: um sensor ultrassônico fica em cima do "rio" e mede o nível
//  da água. Conforme a água sobe, o sistema muda de estado:
//    NORMAL  -> LED verde, cancela aberta
//    ATENCAO -> LED amarelo, cancela aberta
//    PERIGO  -> LED vermelho piscando, buzzer apitando, cancela FECHADA
//
//  Tratamento de incertezas (exigido no trabalho):
//    1) Mediana: cada medida é feita 5 vezes e usamos o valor do meio.
//       Leitura muito errada (som batendo na parede do copo) é ignorada.
//    2) Média móvel: a água balança e o sensor erra um pouco às vezes.
//       Fazemos a média das últimas leituras pra suavizar.
//    3) Histerese: pra trocar de estado a água precisa passar do limite,
//       mas pra voltar ela precisa baixar um pouco MAIS que o limite.
//       Assim a cancela não fica abrindo e fechando quando a água está
//       bem na linha do limite.
// =====================================================================

#include <Arduino.h>
#include <Servo.h>
#include <EEPROM.h>   // memória que não apaga quando desliga (guarda a calibração)

// ---------------------------------------------------------------------
// PINOS
// ---------------------------------------------------------------------
const int trigPin = 11;     // sensor: manda o som
const int echoPin = 10;     // sensor: escuta o eco
const int servoPin = 12;    // servo da cancela

const int buzzer = 2;
const int redLed = 3;
const int greenLed = 4;
const int yellowLed = 5;

// ---------------------------------------------------------------------
// CONFIGURAÇÕES (dá pra ajustar conforme o copo da maquete)
// ---------------------------------------------------------------------
const float ATTENTION_LEVEL = 1.5;  // a partir de quantos cm de água vira ATENÇÃO (2 seringas na maquete)
const float DANGER_LEVEL = 4.0;     // a partir de quantos cm de água vira PERIGO
const float HYSTERESIS = 0.7;       // quanto a água precisa baixar a mais pra voltar de estado

const int GATE_OPEN = 90;           // ângulo do servo com a cancela aberta (palito em pé)
const int GATE_CLOSED = 0;          // ângulo do servo com a cancela fechada (palito deitado)

const int AVERAGE_SIZE = 10;                 // quantas leituras entram na média móvel
const unsigned long READ_INTERVAL = 60;      // de quanto em quanto tempo (ms) o sensor mede
const unsigned long BLINK_INTERVAL = 250;    // velocidade do pisca-pisca no PERIGO

// ---------------------------------------------------------------------
// ESTADOS DO SISTEMA (máquina de estados)
// ---------------------------------------------------------------------
enum FloodState { NORMAL, ATTENTION, DANGER };
FloodState state = NORMAL;

Servo gate;

float baseline = 0;                 // distância até o fundo do copo vazio (o "zero" da água)

// Onde a calibração fica salva na EEPROM
const int EEPROM_BASELINE_ADDR = 0;   // float (4 bytes)
const int EEPROM_MAGIC_ADDR = 4;      // byte que indica "já tem calibração salva"
const byte EEPROM_MAGIC = 0x42;
float readings[AVERAGE_SIZE];       // últimas leituras, pra média móvel
int readIndex = 0;
float waterLevel = 0;               // nível da água em cm (já suavizado)

unsigned long lastRead = 0;
unsigned long lastBlink = 0;
bool blinkOn = false;

float getDistance();
float getMedianDistance();
float averageDistance();
float measureEmptyDistance();
void calibrate(bool force);
void updateState();
void applyState();

// =====================================================================
// setup(): roda uma vez quando liga
// =====================================================================
void setup() {
  Serial.begin(115200);

  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(buzzer, OUTPUT);
  pinMode(redLed, OUTPUT);
  pinMode(greenLed, OUTPUT);
  pinMode(yellowLed, OUTPUT);

  gate.attach(servoPin);
  gate.write(GATE_OPEN);

  // Descobre onde é o "zero" da água (o fundo do copo vazio).
  // Usa a calibração salva, e só atualiza se o copo estiver vazio agora.
  calibrate(false);

  applyState();
}

// =====================================================================
// loop(): roda pra sempre
// =====================================================================
void loop() {
  // Comando pelo Monitor Serial: mandar a letra "c" força uma nova calibração
  // (use com o copo VAZIO)
  if (Serial.available() > 0 && Serial.read() == 'c') {
    calibrate(true);
  }

  // Mede o nível da água de tempos em tempos
  if (millis() - lastRead >= READ_INTERVAL) {
    lastRead = millis();

    float distance = getMedianDistance();

    // Leitura sem eco (999) é descartada: não entra na média
    if (distance < 999) {
      readings[readIndex] = distance;
      readIndex = (readIndex + 1) % AVERAGE_SIZE;
    }

    // Nível da água = distância do copo vazio - distância até a água agora
    waterLevel = baseline - averageDistance();
    if (waterLevel < 0) {
      waterLevel = 0;
    }

    updateState();

    Serial.print("Nivel: ");
    Serial.print(waterLevel);
    Serial.print(" cm  Estado: ");
    Serial.println(state == NORMAL ? "NORMAL" : state == ATTENTION ? "ATENCAO" : "PERIGO");
  }

  // No PERIGO, o LED vermelho pisca e o buzzer faz bip-bip
  if (state == DANGER && millis() - lastBlink >= BLINK_INTERVAL) {
    lastBlink = millis();
    blinkOn = !blinkOn;
    digitalWrite(redLed, blinkOn ? HIGH : LOW);
    if (blinkOn) {
      tone(buzzer, 1000);
    } else {
      noTone(buzzer);
    }
  }
}

// =====================================================================
// updateState(): decide o estado a partir do nível da água (com histerese)
// =====================================================================
void updateState() {
  FloodState newState = state;

  switch (state) {
    case NORMAL:
      if (waterLevel >= DANGER_LEVEL) {
        newState = DANGER;
      } else if (waterLevel >= ATTENTION_LEVEL) {
        newState = ATTENTION;
      }
      break;

    case ATTENTION:
      if (waterLevel >= DANGER_LEVEL) {
        newState = DANGER;
      } else if (waterLevel < ATTENTION_LEVEL - HYSTERESIS) {
        newState = NORMAL;   // só volta pro normal se baixar bem
      }
      break;

    case DANGER:
      if (waterLevel < DANGER_LEVEL - HYSTERESIS) {
        newState = ATTENTION;   // só sai do perigo se baixar bem
      }
      break;
  }

  // Só mexe nos LEDs/servo quando o estado muda de verdade
  if (newState != state) {
    state = newState;
    applyState();
  }
}

// =====================================================================
// applyState(): liga LEDs, buzzer e cancela conforme o estado
// =====================================================================
void applyState() {
  digitalWrite(greenLed, state == NORMAL ? HIGH : LOW);
  digitalWrite(yellowLed, state == ATTENTION ? HIGH : LOW);
  digitalWrite(redLed, LOW);
  noTone(buzzer);

  if (state == DANGER) {
    gate.write(GATE_CLOSED);   // fecha a rua
  } else {
    gate.write(GATE_OPEN);     // libera a rua
  }

  Serial.print(">>> Mudou para: ");
  Serial.println(state == NORMAL ? "NORMAL" : state == ATTENTION ? "ATENCAO" : "PERIGO");
}

// =====================================================================
// calibrate(): descobre o "nível zero" (fundo do copo vazio)
//
// A calibração fica salva na EEPROM. Ao ligar, o Arduino mede o copo:
//  - se não tem nada salvo, salva o que mediu;
//  - se mediu MAIS LONGE (ou igual) que o salvo, o copo está vazio:
//    atualiza a calibração;
//  - se mediu mais perto, tem água no copo: mantém a calibração salva.
// Assim, ligar o Arduino com água no copo não estraga o "zero".
// force = true ignora a regra e salva a medida atual (comando "c").
// =====================================================================
void calibrate(bool force) {
  Serial.println("Calibrando...");
  float measured = measureEmptyDistance();

  float saved = 0;
  bool hasSaved = EEPROM.read(EEPROM_MAGIC_ADDR) == EEPROM_MAGIC;
  if (hasSaved) {
    EEPROM.get(EEPROM_BASELINE_ADDR, saved);
  }

  if (force || !hasSaved || measured >= saved - 0.5) {
    baseline = measured;
    EEPROM.put(EEPROM_BASELINE_ADDR, baseline);
    EEPROM.update(EEPROM_MAGIC_ADDR, EEPROM_MAGIC);
    Serial.println("Copo vazio: calibracao atualizada e salva");
  } else {
    baseline = saved;
    Serial.println("Tem agua no copo: usando a calibracao salva");
  }

  // Enche a média móvel com a medida atual pra começar estável
  for (int i = 0; i < AVERAGE_SIZE; i++) {
    readings[i] = measured;
  }

  Serial.print("Fundo do copo a ");
  Serial.print(baseline);
  Serial.println(" cm do sensor");
}

// Faz várias medições seguidas e devolve a média (usada na calibração)
float measureEmptyDistance() {
  float sum = 0;
  int count = 0;
  for (int i = 0; i < 15; i++) {
    float d = getMedianDistance();
    if (d < 999) {
      sum += d;
      count++;
    }
    delay(20);
  }
  return count > 0 ? sum / count : 0;
}

// =====================================================================
// getMedianDistance(): mede 5 vezes rapidinho e fica com o valor do MEIO
// (mediana). Se uma das 5 leituras vier muito errada (o som bateu na
// parede do copo, por exemplo), ela fica na ponta e é ignorada.
// =====================================================================
float getMedianDistance() {
  float values[5];
  int n = 0;
  for (int i = 0; i < 5; i++) {
    float d = getDistance();
    if (d < 999) {
      values[n++] = d;
    }
    delay(8);   // pausa pro eco anterior sumir antes do próximo
  }
  if (n == 0) {
    return 999;
  }

  // Ordena os valores (são poucos, então um jeito simples já resolve)
  for (int i = 1; i < n; i++) {
    float key = values[i];
    int j = i - 1;
    while (j >= 0 && values[j] > key) {
      values[j + 1] = values[j];
      j--;
    }
    values[j + 1] = key;
  }
  return values[n / 2];
}

// =====================================================================
// averageDistance(): média móvel das últimas leituras
// =====================================================================
float averageDistance() {
  float sum = 0;
  for (int i = 0; i < AVERAGE_SIZE; i++) {
    sum += readings[i];
  }
  return sum / AVERAGE_SIZE;
}

// =====================================================================
// getDistance(): mede a distância com o sensor (em cm)
// O sensor solta um som, o som bate na água e volta.
// Pelo tempo do eco, dá a distância.
// =====================================================================
float getDistance() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH, 30000);
  if (duration == 0) {
    return 999;   // eco não voltou: leitura inválida
  }
  return duration * 0.034 / 2;
}
