# Alerta de Alagamento

Trabalho da VT da disciplina **N202 — Robótica e Automação Inteligentes** (Ciência da Computação, Unitri).

Um sistema que **mede sozinho o nível da água** e, conforme ela sobe, **avisa e bloqueia a rua automaticamente**: o semáforo muda de verde para amarelo e para vermelho, um alarme toca e uma cancela fecha a passagem.

## O problema

- **Quem sofre:** motoristas, motociclistas e pedestres que passam por avenidas, túneis e passagens baixas que enchem de água em chuva forte. Em Uberlândia, o exemplo mais conhecido é a Av. Rondon Pacheco.
- **Por que importa:** a água sobe em poucos minutos. O motorista entra sem saber a profundidade, o carro apaga no meio da água ou é arrastado. Isso dá prejuízo e pode matar.
- **Como é tratado hoje:**
  - A Defesa Civil manda alerta por SMS.
  - Agentes de trânsito bloqueiam a rua na mão.
  - Existem painéis de aviso.

  Mas o bloqueio depende de alguém chegar a tempo, e o painel só avisa: não impede ninguém de entrar.

## A solução

```
 [Sensor ultrassônico] ──► [Arduino UNO] ──► [Semáforo (LEDs) · Buzzer · Cancela (servo)]
     percepção               decisão                         atuação
```

O sensor fica em cima do "rio" (um copo na maquete), apontando para baixo, e mede a distância até a água. O nível é calculado como:

```
nível = distância até o fundo do copo vazio − distância até a água agora
```

### Máquina de estados

| Estado | Atuação | Entra quando | Sai quando |
|---|---|---|---|
| 🟢 NORMAL | LED verde, cancela aberta | — | água ≥ 1,5 cm |
| 🟡 ATENÇÃO | LED amarelo, cancela aberta | água ≥ 1,5 cm | água ≥ 4,0 cm (vai pro PERIGO) ou < 0,8 cm (volta pro NORMAL) |
| 🔴 PERIGO | LED vermelho piscando, buzzer, **cancela fechada** | água ≥ 4,0 cm | água < 3,3 cm (volta pra ATENÇÃO) |

Os valores são da escala da maquete. Na rua seriam algo como 15 cm (atenção) e 30 cm (perigo) de água na pista.

## Tratamento de incertezas

| Técnica | Problema que resolve | Como está no código |
|---|---|---|
| **Mediana** | Leituras isoladas muito erradas (o som batendo na parede do copo, por exemplo) | Cada medida é feita 5 vezes, e o código usa o valor do meio (`getMedianDistance`) |
| **Média móvel** | Água balançando e leitura "tremida" | Média das últimas 10 medidas, tiradas a cada 60 ms (`averageDistance`) |
| **Histerese** | Cancela abrindo e fechando quando a água está bem no limite | Limite de saída 0,7 cm abaixo do de entrada (`updateState`) |
| Leituras sem eco | Sensor sem retorno | Leitura descartada (o código usa o valor 999) |

## Componentes

| Componente | Qtd | Função |
|---|---|---|
| Arduino UNO | 1 | Controle (firmware) |
| Sensor ultrassônico HC-SR04 | 1 | Percepção: distância até a água |
| Servo motor SG90 | 1 | Atuação: cancela |
| LEDs vermelho, amarelo e verde | 1 de cada | Atuação: semáforo |
| Resistor 1 kΩ | 3 | Proteção dos LEDs |
| Buzzer | 1 | Atuação: alarme sonoro |
| Protoboard + jumpers (macho-macho e macho-fêmea) | — | Ligações |
| Power bank (5 V) | 1 | Alimentação própria da maquete |

## Esquema de ligação

| Componente | Pino do Arduino |
|---|---|
| Sensor — Trig | 11 |
| Sensor — Echo | 10 |
| Sensor — Vcc / Gnd | 5V / GND |
| Servo — sinal (laranja) | 12 |
| Servo — vermelho / marrom | 5V / GND |
| Buzzer (+) | 2 |
| LED vermelho (via resistor 1 kΩ para o GND) | 3 |
| LED verde (via resistor 1 kΩ para o GND) | 4 |
| LED amarelo (via resistor 1 kΩ para o GND) | 5 |

Na protoboard foi usada só a metade de cima (linhas A–E):

| Componente | Furos |
|---|---|
| Buzzer | perna + em C3, outra perna em C6. Pino 2 → A3. A6 → barra − |
| LED vermelho | perna longa em C9, curta em C10. Pino 3 → A9. Resistor de A10 → barra − |
| LED amarelo | perna longa em C13, curta em C14. Pino 5 → A13. Resistor de A14 → barra − |
| LED verde | perna longa em C17, curta em C18. Pino 4 → A17. Resistor de A18 → barra − |

## Como usar

### Gravar o firmware

O projeto usa o [PlatformIO](https://platformio.org/). Com a placa conectada por USB:

```bash
pio run -t upload
```

Para ver as leituras pelo Monitor Serial (115200 baud):

```bash
pio device monitor
```

### Calibração

- **O "zero" fica salvo na EEPROM.** Ao ligar, o Arduino mede o copo:
  - Se a distância medida for **maior ou igual** à salva, o copo está vazio, e o zero é atualizado.
  - Se for **menor**, tem água no copo, e o código usa o zero salvo. Assim, religar com água no copo não estraga a calibração.
- **Forçar recalibração** (com o copo vazio): no Monitor Serial, envie a letra `c`.
- Se o sensor ou o copo mudarem de lugar, esvazie o copo e recalibre.

### Demonstração

1. Ligue com o copo vazio e espere ~3 s para estabilizar.
2. Coloque água aos poucos. Com cerca de 2 seringas, o semáforo fica 🟡 amarelo.
3. Passou de 4 cm: 🔴 vermelho piscando, alarme tocando e cancela fechando.
4. Tire a água. A cancela abre abaixo de 3,3 cm, e o semáforo volta ao 🟢 verde abaixo de 0,8 cm.

## Segurança

- Tudo funciona em **5 V** (power bank). Nenhuma parte usa tensão de rede.
- A parte elétrica fica longe da água. O sensor mede de cima, sem encostar nela.
- O servo SG90 é pequeno e é alimentado pela mesma bateria externa, com GND comum ao Arduino.

## Limitações e melhorias futuras

| Limitação | Melhoria |
|---|---|
| O HC-SR04 não é à prova d'água | Usar o JSN-SR04T (versão à prova d'água) |
| O aviso só aparece no local | ESP32 com Wi-Fi para alertar o celular e a Defesa Civil |
| Se o sensor quebrar, ninguém fica sabendo | Detectar muitas leituras sem eco e fechar a cancela por segurança |
| Se o sensor ou o copo saírem do lugar, o zero fica errado | Botão físico de recalibrar |
| O sensor enxerga em cone e pode detectar a mão | Ignorar o que está acima da borda e exigir tempo de confirmação |

## Estrutura

```
platformio.ini     configuração do PlatformIO (placa Arduino UNO, biblioteca Servo)
src/main.cpp       firmware comentado
```

## Integrantes

- Leonardo Sergi Molina
- Gabriel Nogueira Rezende
- Lucas Carneiro Lozano Silva
- Vítor Tetsuya Kazuma
- Matheus Henrique de Lázaro Silva
