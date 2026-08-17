# SSED vs SED — analiza problemu CSL / timestamper (nRF54L15 RCP leader)

**Data notatki:** 2026-08-17  
**Kontekst:** Debug ręczny + test TM `test_configure_ssed`  
**Topologia:** nRF54L15 (leader, ot-daemon + RCP) + nRF52840 (child SED/SSED)

---

## Problem w jednym zdaniu

**Thread i RF działają (SED OK), ale ścieżka CSL na leaderze (54L15 RCP) nie — SSED pada na synchronizacji MLE/CSL i zaplanowanym TX (`TransmitDataCsl` / `CslTxScheduler`).**

---

## Wspólna interpretacja (oba źródła)

| Warstwa | SED | SSED |
|---------|-----|------|
| Attach (Child ID Req/Resp) | OK | OK (początkowo) |
| RF (RSSI ~−50 dBm) | OK | OK |
| Data Poll / indirect TX | OK | Fallback; nie wystarcza |
| CSL info w Enhanced ACK (`CslPeriod`, `CslPhase`) | N/A (`csl period 0`) | Odbierane poprawnie |
| `CSL_ACCURACY` / `CSL_UNCERTAINTY` (Spinel) | N/A | **FAIL** (timeout / framing error) |
| `TransmitDataCsl` / `CslTxScheduler` | N/A | **FAIL** |
| Utrzymanie roli `child` | OK | **FAIL** → `detached` → Parent Request |

**Główny podejrzany:** timestamper / CSL scheduled TX na **RCP nRF54L15** (parent), nie child 52840.

**Asymetria RX vs TX (kluczowy argument):**
- RX: timestamp + parsowanie `CslPeriod`/`CslPhase` z Enhanced ACK — **działa**
- TX: `CslTxScheduler: CSL tx failed` — **nie działa**

---

## ŹRÓDŁO A — logi z konwersacji (sesja ręczna, 2026-08-17)

> **Uwaga:** To osobna sesja niż pliki w `crash/nrf54-leader/rcp_ssed/`.  
> Inne RLOC, IPv6, timestampy ot-daemon (`00:35:` / `00:42:` vs `00:00:` w crash).

### Setup (domyślny z kontekstu rozmowy)

| Rola | Urządzenie | Uwagi |
|------|------------|-------|
| Leader | nRF54L15 + ot-daemon `[566542]` | RCP, kanał operacyjny 24 |
| Child SED | 52840 | `pollperiod 1000`, `csl period 0` |
| Child SSED | 52840 | `csl period 500000` (500 ms), `csl channel 22` |

### A.1 SED — ot-daemon leader (~`00:35:29`)

```
DataPollHandlr: Rx data poll, src:0x7005, qed_msgs:0, rss:-49, ack-fp:0
```

Powtarza się co ~1 s.

| Obserwacja | Interpretacja | Normalne? |
|------------|---------------|-----------|
| `src:0x7005` | Child SED odpytuje parenta `0x7000` | Tak |
| `qed_msgs:0` | Brak danych w kolejce parenta | Tak (idle) |
| `ack-fp:0` | Brak ramki oczekującej w ACK | Tak |
| Interwał ~1 s | Zgodny z `pollperiod 1000` | Tak |

**Sniffer (zdjęcie):** ciągłe **Data Request** `0x7005 → 0x7000` co ~1 s + ACK.  
**Wniosek:** zdrowy idle SED. Brak anomalii.

---

### A.2 SSED — ot-daemon leader (~`00:42:16`)

#### Identyfikatory tej sesji

| Parametr | Wartość |
|----------|---------|
| Child RLOC | `0x7006` |
| Child IPv6 (LL) | `fe80::e403:9804:d2b1:2d44` |
| Child ext addr | `e6039804d2b12d44` |
| Leader IPv6 (LL) | `fe80::88d6:bc83:9f8f:7da8` |
| Child mesh-local | `fdcb:403d:c0f2:682c:258:9493:f550:aa4e` |

#### Sekwencja logów

**1. CSL TX fail (`.155`)**

```
Finishing operation "TransmitDataCsl"
CslTxScheduler: CSL tx to 7006 failed, attempt 3/4
Request to start operation "TransmitDataCsl"
Idle mode: Radio receiving on channel 24
```

Hex TX (skrót): dst ext addr `e6039804d2b12d44`, payload MLE (`MLML`).

| Log | Znaczenie |
|-----|-----------|
| `TransmitDataCsl` | Parent planuje TX w oknie CSL (wymaga timestampera) |
| `attempt 3/4` | 3. nieudana próba w bieżącym okresie CSL |
| `failed` | Ramka nie została skutecznie dostarczona w oknie |

**2. RX Child Update + CSL info (`.260`, +105 ms)**

```
timestamp:2700602805, channel:24, rssi:-50, lqi:184
Timestamp=2700602805 Sequence=92 CslPeriod=3125 CslPhase=2478 TransmitPhase=2478
Receive Child Update Request from child (fe80::e403:9804:d2b1:2d44)
Request to start operation "TransmitDataCsl"
```

| Pole | Wartość | Znaczenie |
|------|---------|-----------|
| `CslPeriod=3125` | 3125 × 160 µs = **500 ms** | OK — zgodne z `csl period 500000` |
| `CslPhase=2478` | faza nasłuchu childa | OK |
| `TransmitPhase=2478` | = CslPhase | Parent wie, kiedy wysłać |
| `timestamp` na RX | 2700602805 µs | Timestamper RCP na **odbiorze** działa |

**3. Spinel / RCP (`.260` – `00:42:18`)**

```
CSL_UNCERTAINTY → OK (uncertainty:20)
CSL_ACCURACY    → Wait for response timeout (2 s)
RCP failure detected → Trying to recover (1/2) ... (2/2)
Wait for response timeout na RESET / PHY_ENABLED
```

| Log | Interpretacja |
|-----|---------------|
| `CSL_UNCERTAINTY` OK | RCP częściowo odpowiada |
| `CSL_ACCURACY` timeout | Parent nie może dokończyć Child Update Response |
| `RCP failure` | RCP przestaje odpowiadać na Spinel |

**Sniffer (zdjęcie):**
- Lawina **Child Update Request** (child → leader)
- Sporadyczne **Child Update Response** (gdy RCP chwilowo odpowiada)
- Na końcu **Parent Request** → child stracił parenta

**Stan childa (wnioskowany):** `child` → **`detached`** (nie `disabled` — to wymaga `thread stop` / `ifconfig down`).

#### Wniosek ze źródła A

- SED: **OK** — poll path wystarcza.
- SSED: **FAIL** na CSL TX + RCP Spinel (`CSL_ACCURACY`).
- Kierunek diagnostyki: **timestamper / delayed TX na RCP 54L15**.

---

## ŹRÓDŁO B — logi z folderu `crash/nrf54-leader/rcp_ssed/`

> **Uwaga:** Run testu TM z **2026-08-17 09:31–09:32**, `test_configure_ssed`.  
> Inna sesja niż źródło A (inne adresy, timestampy `00:00:`).

### Pliki

```
crash/nrf54-leader/rcp_ssed/
├── test_session.log
├── opis_ssed_vs_sed_debug_20260817.md          ← ta notatka
└── meeseeks_BasicTests_rcp_debug_otdaemon_.../
    └── test_configure_ssed/
        ├── ...test_configure_ssed_ot-daemon_1057766367_run_1.log   # leader
        ├── ...test_configure_ssed_cli_1050202559_run_1.log         # child
        ├── counters_node_1__1050202559_run_1.json
        └── TEST_PCAP_...pcap
```

**Test:** `ctf_tm ... test_case_id=test_configure_ssed`  
**Konfiguracja SSED** (`tests_morty/functional/nrf54l15_rcp/tests_nrf54l15_rcp.py`):
- `csl_period=500000`, `csl_timeout=300`, `csl_channel=22`
- `child_timeout=240`, `poll_period=236000` (default SSED)

### Identyfikatory tej sesji

| Parametr | Wartość |
|----------|---------|
| Child RLOC | `0x4401` |
| Child ext addr | `0000001050202559` |
| Leader ext addr | `0000001057766367` |
| Child IPv6 (LL) | `fe80::200:10:5020:2559` |
| Leader IPv6 (LL) | `fe80::200:10:5776:6367` |
| ot-daemon PID | `51128` |
| Timestampy OT | `00:00:09` – `00:00:30` |

---

### B.1 Leader (ot-daemon) — attach OK (~`00:00:15`)

```
Receive Child ID Request
Send Child ID Response (..., 0x4401)
ChildSupervsn: Starting Child Supervision
Rx data poll, src:0x4401, qed_msgs:1, ack-fp:1
TransmitDataIndirect → Sent Child ID Response (indirect via poll)
```

Attach i pierwszy indirect TX przez **data poll** — **OK**.

---

### B.2 Leader — Child Update + CSL (~`00:00:15.467`)

```
Timestamp=17269773 Sequence=232 CslPeriod=3125 CslPhase=2483 TransmitPhase=2483
Receive Child Update Request from child (fe80::200:10:5020:2559)
PROP_VALUE_GET CSL_UNCERTAINTY → (oczekiwanie)
RCP => Framing error 6
(burst STREAM_RAW len=115 — kolejne Child Update Request, ~co 6–10 ms)
Wait for response timeout
RCP failure detected → Trying to recover (1/2)
```

| Obserwacja | Interpretacja |
|------------|---------------|
| `CslPeriod=3125` | 500 ms — OK |
| `Framing error 6` podczas GET `CSL_UNCERTAINTY` | RCP zalany / uszkodzona ramka Spinel |
| Burst RX len=115 | Child retry Child Update Request |
| Recovery 1/2 OK (chwilowo) | Po recovery: `CSL_UNCERTAINTY` OK, `CSL_ACCURACY` OK → `Send Child Update Response` |

**Po krótkiej poprawie (~`00:00:17.490`):** jeden Child Update Response wyszedł, parent dodał child do src match, zaplanował `TransmitDataCsl`.

---

### B.3 Leader — CSL TX fail (~`00:00:17.855` – `00:00:30.898`)

```
Starting operation "TransmitDataCsl"
STREAM_RAW, channel:22, txDelay:2397280, txDelayBase:17269773
radio tx timeout
RCP failure detected → recovery (1/2) ... (2/2) — wszystkie timeout
Too many rcp failures, exiting
```

| Log | Interpretacja |
|-----|---------------|
| `channel:22` | CSL channel childa — poprawnie |
| `txDelay` / `txDelayBase` | Zaplanowany delayed TX (timestamper) |
| `radio tx timeout` (~5 s) | RCP **nie wykonał** TX w czasie |
| `Too many rcp failures, exiting` | ot-daemon pada |

---

### B.4 Child (CLI log) — skutek (~`00:00:09` – `00:00:22`)

```
Role detached -> child                    # attach OK (~9.05 s)
Send Child Update Request to parent
Frame tx attempt 1/16 failed, error:NoAck
...
DataPollSender: Failed to send data poll, error:NoAck
Role child -> detached                    # ~10.82 s (~1.8 s po attach)
AttachState Idle -> Start
Send Parent Request to routers
Attach attempt N unsuccessful
Send Announce on channel 11..26           # scan kanałów
```

| Obserwacja | Interpretacja |
|------------|---------------|
| `NoAck` × 16 | Parent nie odpowiada (RCP zajęty / padnięty) |
| `child -> detached` po ~1.8 s | Szybka utrata parenta |
| `Parent Request` + `Announce` | Re-attach / scan |

**Stan:** `detached`, nie `disabled`.

---

### Wniosek ze źródła B

Ten sam wzorzec co źródło A, z dodatkowymi detalami:
- **`Framing error`** na Spinel przy obsłudze CSL
- Jawny **`txDelay` na channel 22`** + **`radio tx timeout`**
- **`Too many rcp failures, exiting`** — twardy crash daemona
- Child **`detached`** po ~1.8 s od attach

---

## Porównanie obu źródeł

| Aspekt | Źródło A (konwersacja) | Źródło B (crash folder) |
|--------|------------------------|-------------------------|
| Data sesji | 2026-08-17, ręczny debug | 2026-08-17 09:32, TM test |
| Child RLOC | `0x7006` | `0x4401` |
| ot-daemon ts | `00:35:` / `00:42:` | `00:00:09` – `00:00:30` |
| SED baseline | Tak (osobny run, `0x7005`) | Nie w tym folderze (osobny run `cli_ssed/`) |
| `CslPeriod=3125` | Tak | Tak |
| `CSL_ACCURACY` timeout | Tak | Tak (po framing error) |
| `CslTxScheduler failed` | Tak (attempt 3/4) | Tak (+ `radio tx timeout`) |
| `txDelay` / ch 22 w logu | Nie w wklejonym fragmencie | **Tak** (jednoznaczny dowód) |
| ot-daemon exit | RCP recovery fail | `Too many rcp failures, exiting` |
| Child detached | Wnioskowany (Parent Request na snifferze) | **Potwierdzony** w CLI log |

**Wzorzec jest spójny** mimo różnych sesji → problem powtarzalny na 54L15 RCP jako parent.

---

## Co jest normalne vs nienormalne (checklist)

### Normalne (SED i początek SSED)

- Attach: Parent Request → Child ID Response
- `DataPollHandlr: Rx data poll, qed_msgs:0` co `pollperiod`
- Sniffer: Data Request + ACK (SED)
- 1–2× Child Update Request/Response po attach (SSED)
- `CslPeriod=3125`, `TransmitPhase` = `CslPhase`
- RX z `timestamp` i RSSI ~−50 dBm

### Nienormalne (SSED — oba źródła)

- Flood Child Update Request (10+ bez response)
- `CSL_ACCURACY` timeout / `Framing error` na Spinel
- `CslTxScheduler: CSL tx failed`
- `radio tx timeout` na `TransmitDataCsl`
- `RCP failure detected` / ot-daemon exit
- Child: `NoAck` → `child -> detached` → Parent Request
- Ping leader → child: timeout / 100% loss

---

## Hipoteza root cause

**Timestamper / CSL scheduled TX na nRF54L15 RCP (firmware RCP + ot-daemon Spinel path).**

Argumenty:
1. SED działa — ta sama RF, ten sam leader, bez CSL TX.
2. RX timestamp + CSL phase parsing działa.
3. TX `TransmitDataCsl` pada (`CslTxScheduler`, `radio tx timeout`).
4. Spinel `CSL_ACCURACY` pada — bez tego parent nie kończy MLE sync.
5. Powtarzalność w dwóch niezależnych sesjach.

**Nie jest to (słabe hipotezy):**
- Zły child / brak CSL config — `CslPeriod`/`Phase` poprawne.
- Słabe RF — RSSI −50, SED OK.
- Ogólny brak Thread — attach przechodzi.

---

## Następne kroki (diagnostyka)

1. W logu ot-daemon szukać linii: `TransmitDataCsl` + `channel:22` + `txDelay:` + ewentualnie `radio tx timeout`.
2. Porównać wersję **RCP hex** na 54L15 z wersją deklarującą pełne CSL Spinel props.
3. Na child: `state`, `csl`, `parent`. Na leaderze (gdy żyje): `child table`.
4. Sniffer: czy parent w ogóle nadaje na **ch 22** w oknach CSL.
5. Baseline: ten sam child 52840 SSED + **52840 lub 52833 jako leader** — izolacja problemu do 54L15.

---

## Powiązane pliki w repo

| Plik | Opis |
|------|------|
| `notes/nrf54l15_csl_timestamper_handoff.md` | Wcześniejsza analiza CSL (54L15 CLI parent, nie RCP) |
| `tests_morty/functional/nrf54l15_rcp/tests_nrf54l15_rcp.py` | `test_configure_ssed` |
| `crash/nrf54-leader/rcp_ssed/` | Artefakty run TM 2026-08-17 |
