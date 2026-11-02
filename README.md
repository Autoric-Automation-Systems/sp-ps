# Smart Plant - SP-PS

**Smart Plant Production States**
Módulo para monitoramento de estados de produção na linha industrial.

| Campo | Valor |
|-------|-------|
| **Placa** | ESP32-C3 DevKitM-1 |
| **Framework** | Arduino (PlatformIO) |
| **Versão** | 1.2 |

---

## Funcionalidades

- **Contador de pulsos** — Leitura de sensor indutivo/capacitivo via interrupção (GPIO5), com debounce de 10 ms
- **Eventos digitais** — Monitoramento de 3 entradas para sensores de estado (GPIO6, GPIO7, GPIO10)
- **Fila de eventos em RTC** — 64 posições; sobrevive a panic, watchdog e auto-restart
- **Heartbeat** — Envio periódico do status (WiFi, IP, uptime, nº de boots, heap livre)
- **WiFiManager** — Configuração via portal cativo (sem credenciais no código) com timeout
- **NTP** — Sincronização automática (fuso -3h) só quando o relógio está inválido
- **Configuração remota** — `heartbeatInterval` e `counterInterval` validados e gravados em NVS
- **HTTPS com validação de certificado** — cadeia Let's Encrypt do servidor
- **Resiliência** — reconexão a cada ciclo, portal após 3 ciclos sem WiFi, auto-restart após 6 ciclos sem envio
- **Atualização OTA** — o servidor informa versão e URL no heartbeat; a imagem é gravada na partição inativa

---

## Pinagem

| GPIO | Função | Descrição |
|------|--------|-----------|
| 5 | `kCounterPin` | Entrada do contador de pulsos (RISING, INPUT_PULLUP) |
| 6 | `kEventPin1` | Evento digital 1 (INPUT_PULLUP, lógica invertida) |
| 7 | `kEventPin2` | Evento digital 2 (INPUT_PULLUP, lógica invertida) |
| 10 | `kEventPin3` | Evento digital 3 (INPUT_PULLUP, lógica invertida) |

**Nota:** O GPIO8 foi evitado pois no ESP32-C3 ele é compartilhado com o LED interno (LED_BUILTIN) e também com SDA do I2C, causando leituras instáveis como entrada digital. Use GPIO10 como alternativa segura.

Todos os pinos e constantes ficam em `include/spps/Config.h`.

---

## API

### Heartbeat (POST /api/devices/heartbeat)

**Envia:**

```json
{
  "mac": "xx:xx:xx:xx:xx:xx",
  "type": "SP-PS",
  "version": "1.2",
  "wifi": -65,
  "ip": "192.168.x.x",
  "uptime": 1234567,
  "boots": 12,
  "heap": 232000
}
```

**Resposta (configuração remota):**

```json
{
  "heartbeatInterval": "30000",
  "counterInterval": 5
}
```

> Os dois campos são opcionais. `heartbeatInterval` pode vir como número ou string
> (`"30000"`), e ambos passam por validação de faixa antes de ir para o NVS:
> heartbeat 5 s a 1 h, counter 1 a 100 000 pulsos. Valor fora da faixa é ignorado —
> um `0` deixaria o aparelho em loop de rede (ou zeraria o divisor do contador).

A resposta pode incluir ainda `firmwareVersion` e `firmwareUrl` para disparar o OTA
(ver seção [Atualização OTA](#-atualização-ota-over-the-air)).

### Events (POST /api/devices/events)

**Envia:**

```json
{
  "mac": "xx:xx:xx:xx:xx:xx",
  "events": [
    { "name": "counter", "value": 2,   "ts": 1700000000 },
    { "name": "event1",  "value": 1,   "ts": 1700000000 },
    { "name": "wifi",    "value": -67, "ts": 1700000000 }
  ]
}
```

- Envio em **lotes de 20** (o servidor aceita até 200 por requisição)
- Os eventos só saem da fila quando o POST responde **2xx**; em falha são reenviados no
  ciclo seguinte com o timestamp original
- Valores vão com **no máximo 2 casas decimais** (`counter` inteiro continua inteiro)

---

## Eventos Gerados

| Evento | Tipo | Descrição |
|--------|------|-----------|
| counter | float | Disparado a cada `counterInterval` pulsos (valor = o intervalo configurado) |
| event1 | bool (0/1) | Estado do sensor no GPIO6 |
| event2 | bool (0/1) | Estado do sensor no GPIO7 |
| event3 | bool (0/1) | Estado do sensor no GPIO10 |
| wifi | float | RSSI do WiFi (enviado quando varia ≥ 10 dBm) |

Pulsos que não completam o `counterInterval` ficam em `pulsesPending` na memória RTC,
então um restart no meio da contagem não perde o que já foi medido.

---

## Configuração Remota

| Parâmetro | Padrão | Faixa | Descrição |
|-----------|--------|-------|-----------|
| heartbeatInterval | 10000 ms | 5 s – 1 h | Intervalo entre heartbeats |
| counterInterval | 2 pulsos | 1 – 100 000 | Pulsos para gerar um evento `counter` |

Ambos são gravados em **NVS** (`Preferences`), sobrevivendo a reboot, queda de energia e
regravação de firmware. Para ensaiar valores diferentes em bancada, use build flags:
`-DDEFAULT_HEARTBEAT_MS=5000UL` ou `-DDEFAULT_COUNTER_INTERVAL=1UL`.

---

## Primeiro Uso

1. Grave o firmware no ESP32-C3
2. Conecte-se à rede **SmartPlant SP-PS** que aparece no boot
3. O portal cativo do WiFiManager abrirá automaticamente
4. Selecione sua rede WiFi e insira a senha
5. O dispositivo reinicia e começa a operar

---

## Desenvolvimento Local

Em `include/spps/Config.h`, use:

```cpp
#define SPPS_LOCAL_SERVER 1   // 1 = servidor local (http://192.168.100.103:3000)
```

O transporte detecta o esquema da URL: `https://` usa `WiFiClientSecure` com validação de
certificado, `http://` usa cliente simples (não faz handshake TLS contra servidor local).

---

## Fluxo de Operação

```
setup()  →  Device::begin()
  Serial (115200) + banner com boot, motivo do reset e slot de firmware
  beginPersistentState()  → valida o bloco de RTC (checksum) e conta o boot
  EventQueue::restore()   → recupera eventos não enviados
  SettingsStore           → heartbeat e counterInterval do NVS
  PulseCounter::begin()   → ISR do contador (debounce 10 ms)
  DigitalInputs::begin()  → sincroniza o estado das 3 entradas sem gerar evento
  INetworkLink::autoConnect() → WiFi salvo ou portal

loop()  →  Device::tick()
  publishCounterEvents()  → pulsos → eventos `counter`
  publishInputEvents()    → transições → eventos `event1..3`
  Quando o IntervalTimer vence:
    runNetworkCycle()
      INetworkLink::connect()
      IClock::ensureSynced()      → NTP quando necessário
      publishSignalEvent()        → RSSI com histerese de 10 dBm
      publishQueuedEvents()       → lotes de 20, só sai da fila em 2xx
      ApiClient::sendHeartbeat()  → aplica intervalos e checa OTA
      HealthPolicy                → decide portal / auto-restart
      INetworkLink::disconnect()
```

---

## 📁 Estrutura do Projeto

```
sp-ps/
├── lib/
│   └── spps_core/src/spps/core/       # Domínio puro, sem Arduino (testável)
│       ├── Event.{h,cpp}              # Evento (POD, vive em RTC) + formatação 2 casas
│       ├── EventQueue.{h,cpp}         # Fila FIFO com checksum FNV-1a
│       ├── Checksum.{h,cpp}           # Validação de blocos em RTC
│       ├── SignalMonitor.{h,cpp}      # Histerese de RSSI (com semente de RTC)
│       ├── IntervalTimer.h            # Agendador à prova de rollover
│       ├── IntervalLimits.{h,cpp}     # Sanitização de intervalos
│       ├── HealthPolicy.{h,cpp}       # Portal e auto-restart
│       └── FirmwareVersion.{h,cpp}    # Comparação numérica de versão
├── include/spps/
│   ├── Config.h                       # Constantes de configuração
│   ├── Logger.h                       # Log serial
│   ├── roots_ca.h                     # Cadeia TLS do servidor
│   ├── hw/                            # PulseCounter, DigitalInputs, PersistentStorage
│   ├── net/                           # IClock, INetworkLink, ITransport,
│   │                                  # IFirmwareUpdater + implementações
│   ├── power/                         # IPowerController + RuntimeController
│   └── app/                           # Device (orquestrador), SettingsStore (NVS)
├── src/
│   ├── main.cpp                       # Composition root (setup/loop)
│   └── hw/  net/  app/  power/        # Implementações dos adaptadores
├── test/test_core/                    # Testes de unidade do domínio
├── partitions (ota_nofs_4MB.csv)      # app0/app1 de 1,94 MB + otadata
├── platformio.ini
└── README.md
```

### 🧱 Arquitetura

- **`core`** — regras puras (fila, checksum, histerese de RSSI, agendamento, política de
  saúde, comparação de versão). Não inclui `Arduino.h`.
- **`hw`** — adaptadores de hardware (ISR do contador, entradas digitais, blocos em RTC).
- **`net`** — `WifiLink`, `SecureHttpTransport` e `HttpOtaUpdater` atrás das interfaces
  `INetworkLink`/`ITransport`/`IFirmwareUpdater`; `ApiClient` só conhece `ITransport`.
- **`power`** — `IPowerController` abstrai reboot e slot de firmware.
- **`app`** — `Device` orquestra o ciclo; depende apenas das abstrações (DIP).
- **`main.cpp`** — composition root: o único arquivo que conhece as implementações.

É a mesma organização usada nos módulos **SP-FF** e **SP-TH**.

---

## Testes

O domínio é coberto por testes Unity que rodam no próprio ESP32:

```bash
pio test -e esp32-c3-devkitm-1 -f test_core
```

Casos cobertos: fila (ordem, overflow, sobrevivência a restart, lixo de RTC com checksum),
formatação de 2 casas no JSON, regra do divisor de pulsos, saneamento dos dois intervalos,
rollover do `millis()`, histerese de RSSI, política de saúde e comparação de versão.

---

## 🔄 Atualização OTA (Over-The-Air)

1. O heartbeat envia a versão em execução (`"version": "1.2"`).
2. O servidor responde com `firmwareVersion` e `firmwareUrl` quando quiser atualizar.
3. O dispositivo confere a versão (comparação **numérica**: `1.10` > `1.9`) e baixa apenas
   se a do servidor for maior.
4. A imagem é gravada na **partição inativa** (`app1` quando roda em `app0`), validada, e o
   dispositivo reinicia — o bootloader ativa o slot novo.
5. Se o download falhar, o firmware atual continua rodando: nada é sobrescrito.

O log de boot mostra o slot em uso:

```
Boot Smart Plant 1.2 | SP-PS | boot #1 | reset: SOFTWARE | slot: app1
```

Para ensaiar em bancada:

```bash
PLATFORMIO_BUILD_FLAGS='-DSPPS_OTA_FORCE_URL="http://192.168.0.10:8000/firmware.bin"' \
  pio run -e esp32-c3-devkitm-1 -t upload
```

A versão também pode ser forçada no build: `-DSPPS_FIRMWARE_VERSION='"1.3"'`.

---

## Observações Técnicas

- A ISR do contador ignora pulsos mais próximos que **10 ms** (debounce de ruído)
- A leitura dos pinos de evento é invertida (`!digitalRead`) — o projeto assume sensores NPN
- O WiFi é ligado a cada heartbeat e desligado em seguida (`WIFI_OFF`), reduzindo consumo
- O portal cativo abre apenas após **3 ciclos seguidos** sem WiFi, por 90 s, e nunca
  bloqueia o ciclo atual
- O tempo NTP só é sincronizado quando o relógio está inválido
