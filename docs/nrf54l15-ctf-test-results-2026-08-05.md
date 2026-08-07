# Wyniki testów CTF: nRF54L15 RCP (bare-metal)

> Data sesji: 2026-08-05, 2026-08-06  
> DUT: nRF54L15 DK (`nrf54l15dk_nrf54l15_cpuapp`) — bare-metal RCP  
> Węzły pomocnicze: nRF52840 (ot-daemon / framework Morty)  
> Domyślna konfiguracja: RCP debug, ot-daemon, Skyworks FEM, operational dataset  
> Powiązanie: [Proof of Concept: nRF54L Support in the ot-nrf54xx Line of Ports](KRKNWK-Proof%20of%20Concept_%20nRF54L%20Support%20in%20the%20ot-nrf54xx%20Line%20of%20Ports-090726-062109-1.md)  
> Handoff: [nrf54l15-handoff-2026-08-05.md](nrf54l15-handoff-2026-08-05.md)  
> Sesja archiwizacyjna 2026-08-06 09:08: [nrf54l15_session_20260806_090809.md](nrf54l15_session_20260806_090809.md) (`/tmp/54l15_session_20260806_090809/`)

---

## Podsumowanie

| # | Test | Klasa / suite | Wynik | Czas |
|---|------|---------------|-------|------|
| 1 | `test_commission_board` | commissioning | **PASS** | — |
| 2 | `test_commission_sed` | commissioning | **PASS** | — |
| 3 | `test_board_reset_without_host_restart` | reset / MRT-797 | **PASS** | — |
| 4 | `test_alternate_rloc16_during_role_transition` | role transition | **FAIL** | ~45 s |
| 5 | `test_router_id_mask` | `RouterIdMaskTests` | **OK** | ~353 s |
| 6 | `test_send_address_query` | `AddressQueryTests` | **OK** | ~46 s |
| 7 | `test_send_common_diag_commands` | `BasicDiagTests` | **OK** | ~31 s |
| 8 | `test_energy_scan` | `EnergyScanTests` | **OK** | ~47 s |
| 9 | `test_short_address_filtering` | `RcpRxFrameFilteringTests` | **SKIPPED** | ~0.03 s |
| 10 | `test_setup_rcp_network` | smoke | **PASS** | ~28 s |
| 11 | `test_app_layer_lost_pings` | `test_ping_pass_rate.py` | **FAIL** | ~41 s–900 s |
| 12 | `cliff_debug` Test 0 — 1000 B @ 0.1 s | performance / cliff_debug | **ERROR** | 129 s |
| 13 | `cliff_debug` Test 1 — 1000 B @ 2 s | performance / cliff_debug | **PASS** | 155 s |
| 14 | `cliff_debug` Test 2 — 512 B @ 0.1 s | performance / cliff_debug | **ERROR** | 87 s |
| 15 | `test_IPv6_fragmentation_for_ping` | `IPv6FragmentationTests` | **FAIL** | 35 s |
| 16 | `test_IPv6_fragmentation_for_ping_with_sed` | `IPv6FragmentationTests` | **PASS** / ERROR† | 61 s / 46 s |
| 17 | `test_IPv6_fragmentation_for_thput` | `IPv6FragmentationTests` | **FAIL**‡ | 63 s / 56 s |

‡ TM: FAIL. Funkcja OK (thput + PCAP); padło na crash RCP w tearDown.
| 18 | `cliff_debug` `debug_120s_1000B_0_1s` (sesja 09:08) | performance / cliff_debug | **ERROR** | ~70 s |

† Run 08:44 PASS; run 09:08 ERROR — parser pinga (`received > transmitted` na SED), stack OK.

**Bilans:** 11 PASS/OK, 4 FAIL, 3 ERROR, 1 SKIPPED.

**Główny open issue:** degradacja TX RCP 54L15 pod sustained duże pakiety (ICMP rate, UDP iperf, ping 1700 B→router). Reprodukowalny cliff: **75× OK → fail @ ping #76** (sesja 09:08). Szczegóły: [handoff](nrf54l15-handoff-2026-08-05.md), [cliff_debug](#grupa-f--cliff_debug-2026-08-06), [IPv6 fragmentation](#grupa-g--ipv6-fragmentation-2026-08-06), [sesja 09:08](#grupa-h--sesja-archiwizacyjna-2026-08-06-0908).

---

## Wspólny setup

| Element | Wartość |
|---------|---------|
| DUT | nRF54L15 DK — bare-metal RCP (`build-nrf54l15-uart`, ot-daemon/ot-ctl) |
| Węzły pomocnicze | nRF52840 (leader, router, joiner, commissioner — zależnie od testu) |
| Host | ot-daemon + ot-ctl |
| FEM | Skyworks (`fem_type=skyworks` w konfiguracji frameworku) |
| Sieć | Operational dataset (commissioning / Thread network) |
| Uruchamianie (Morty) | `ctf_tm -c session -p tests_morty/ -e 'test_case_id=*<test>* -a dut.dev.attr_zephyr_board=nrf54l15dk_nrf54l15_cpuapp'` |

### Fixy w frameworku testów (sesja commissioning + reset)

Wprowadzone podczas pierwszej grupy testów (commissioning / reset):

- `scan_retries=None` przy commissioning — brak CLI `joiner scanretries` w ot-daemon
- `fem_type=skyworks` w testach `RxSecurityMismatch*` — poprawny wybór hex RCP
- usunięty zduplikowany hex nRF54L15 (identyczny build)

---

## Wyniki szczegółowe

### Grupa A — commissioning, SED, recovery po resecie (DUT + nRF52840)

**Setup:** bare-metal RCP na 54L15, ot-daemon/ot-ctl; nody 52840; operational dataset.

#### 1. `test_commission_board` — PASS

- DUT (54L15): leader + commissioner
- node_1 (52840): joiner przez commissioning
- Ping DUT ↔ node_1: OK

#### 2. `test_commission_sed` — PASS

- Jak wyżej; joiner jako SED (`mode n`, pollperiod)
- Ping DUT ↔ node_1: OK

#### 3. `test_board_reset_without_host_restart` — PASS (MRT-797)

- DUT (54L15): router; node_1 (52840): leader
- Pin reset DUT przy działającym ot-daemon (bez restartu hosta)
- Recovery do sieci + ping przed i po resecie: OK

**Wniosek grupy A:** RCP na nRF54L15 obsługuje sieć (dataset), commissioning, role commissioner/leader/router oraz recovery po pin resecie bez restartu hosta.

---

### Grupa B — przejście roli i alternate RLOC16

#### 4. `test_alternate_rloc16_during_role_transition` — FAIL (~45 s)

**Komenda / setup:** DUT nRF54L15 RCP; node_1/2 nRF52840; `rcp/debug/ot-daemon/skyworks`

**Co testuje:** awans REED → router + alternate RLOC16 (stary child RLOC16 trzymany ~6 s)

**Topologia:** node_1 = leader, node_2 = router, DUT = REED (`routereligible off → on`)

| Aspekt | Wynik |
|--------|-------|
| Topologia, awans child → router (~5 s) | ✓ OK |
| `RxAddressFiltered` 0 → 0 | ✓ OK |
| RLOC child `0xac01` → router `0xe400`, alt=`0xac01`, wygasł po 5.33 s | ✓ OK |
| Ping leader → DUT (20 tx / 0 rx) | ✗ **100% loss** (`tests_basic.py:403`) |

**Wniosek:** mechanizm alternate RLOC16 działa poprawnie; fail dotyczy wyłącznie łączności ICMP (ping) w trakcie awansu roli — wymaga dalszej analizy (timing window, routing podczas transition).

---

### Grupa C — MLE, CoAP, diag, energy scan (Morty / tests_morty)

Wspólne uwagi nieblokujące dla testów 5–8:

- brak liczników vendor na węzłach testowych
- `get vendor:fault:info` → `InvalidCommand` (oczekiwane dla bare-metal RCP)

#### 5. `test_router_id_mask` — OK (~353 s)

**Topologia:** Leader (DUT) — Router1 — Router2 (+ sniffer)

**Przetestowano:**

- Route64 TLV w pakietach MLE Advertisement
- Po utworzeniu sieci: 3 wpisy w Route64 TLV, ID mask zgodny z router table
- Po zatrzymaniu Router2: wpis usunięty z TLV (2 routery), ID mask zaktualizowany
- Weryfikacja MLE adv: PASSED (pakiet #208)

#### 6. `test_send_address_query` — OK (~46 s)

**Topologia:** Leader (DUT) — Router (+ sniffer)

**Przetestowano:**

- Address query (CoAP) wywołane pingiem DUT → router
- Address query: URI `/a/aq`, payload zawiera IP węzła
- Address notification: URI `/a/an`, payload zawiera IP węzła
- Sieć Thread utworzona poprawnie (leader + router)

#### 7. `test_send_common_diag_commands` — OK (~31 s)

**Setup:** DUT + 1× node + sniffer

**Przetestowano:**

- Podstawowe komendy diag na DUT (`diag start/stop` w setUp/tearDown)
- `diag channel`: kanał 24 zgodny z konfiguracją
- `diag send`: wysłano 20/20 pakietów (diag stats)
- `diag power`: −20 dBm i 1 dBm — wartości zgodne z oczekiwanymi (Skyworks FEM)
- `diag repeat`: transmisja powtarzalna, >200 pakietów wysłanych

#### 8. `test_energy_scan` — OK (~47 s)

**Topologia:** Leader (node_1) — Router (DUT) (+ sniffer)

**Przetestowano:**

- Energy scan inicjowany przez commissionera (node_1) skierowany na DUT
- Parametry: kanały 11–16, count=2, period=32, duration=200
- Odpowiedź energy scan otrzymana w ~1 s (log: `Output: Energy:...`)
- Sieć Thread utworzona poprawnie, link propagation OK

---

### Grupa D — filtrowanie Rx (deprecated)

#### 9. `test_short_address_filtering` — SKIPPED (~0.03 s)

**Setup:** RCP debug, wpan (nie ot-daemon); suite oznaczony jako DEPRECATED

**Status:**

- Test **SKIPPED** — brak uruchomienia na hardware
- Powód: feature nie zaimplementowany ([chirp 90108167](https://chirp.apple.com/problem/90108167))

**Planowany zakres (gdyby suite był aktywny):**

- filtrowanie ramek Rx po short address na DUT
- weryfikacja STREAM_PCAP vs STREAM_RAW dla broadcast, własnego i obcego short address

---

### Grupa E — performance ping (open issue)

#### 10. `test_setup_rcp_network` — PASS (~28 s)

Smoke: DUT leader + node MED/child; ping OK obie strony.

#### 11. `test_app_layer_lost_pings` — FAIL

**Plik:** `tests_morty/performance/test_ping_pass_rate.py` (metoda ≠ nazwa pliku)  
**Parametry:** 900 s, ping 1000 B, timeout 2 s, pass rate ≥ 99%  
**Topologia:** DUT leader (54L15), node router (52840)

| Obserwacja | Wartość |
|------------|---------|
| Pingi przed crashem | 2–100+ (niestabilne) |
| Mechanizm failu | `radio tx timeout` → RCP failure (1/2, 2/2) → `RecoverFromRcpFailure()` |
| Repro ręczne | 5× ping 1000 B OK (~230 ms), ping #6+ → 100% loss, brak recovery |

**Wniosek:** bug/stabilność TX RCP 54L15 pod sustained ICMP 1000 B; nie MTU (pojedynczy 1000 B OK). Pełna chronologia runów, logi i artefakty: [handoff §3.1–§6](nrf54l15-handoff-2026-08-05.md).

---

### Grupa F — cliff_debug (2026-08-06)

**Sesja:** 2026-08-06 08:15–08:21  
**Komenda:**

```bash
ctf_tm -c session -p tests_morty/performance \
  -e 'test_case_id=*cliff_debug* -a dut.dev.attr_zephyr_board=nrf54l15dk_nrf54l15_cpuapp'
```

**Setup:** nRF54L15 DUT (1057766367) + nRF52840 node, RCP debug + ot-daemon Skyworks  
**Wynik sesji:** 3 testy / 388 s → 1 PASS, 2 ERROR (exit 20)

| Test | Parametry | Wynik | Czas | Kluczowe liczby |
|------|-----------|-------|------|-----------------|
| 0 | 1000 B @ 0.1 s | **ERROR** | 129 s | ~210 pingów, 200 OK / 2 fail; cliff po ~48 s (ping #52) |
| 1 | 1000 B @ 2 s | **PASS** | 155 s | 53/53 OK (100%), brak faili |
| 2 | 512 B @ 0.1 s | **ERROR** | 87 s | ~100 pingów, 75 OK / 2 fail; cliff po ~32 s (ping #5) |

#### Test 0 — 1000 B @ 0.1 s → ERROR (129 s)

- ~210 pingów, 200 OK / 2 fail (ostatni log przed crashem)
- Pierwszy fail: ping #52 (~48 s)
- Probe po failu: node↔DUT 16 B / 1000 B OK (łączność wraca po cliff)
- Cliff: `radio tx timeout` ×3 (44 s, 92 s, 113 s)
- Koniec: `RcpCmdError` ping #211 — `ot-ctl ipaddr mleid` brak odpowiedzi
- Artefakty: `/tmp/ping_debug_54l15_20260806_081742`

#### Test 1 — 1000 B @ 2 s → PASS (155 s)

- 53/53 OK (100%), brak faili, brak cliff
- 120 s ciągłego obciążenia bez problemu
- Artefakty: `/tmp/ping_debug_54l15_20260806_082017`

#### Test 2 — 512 B @ 0.1 s → ERROR (87 s)

- ~100 pingów, 75 OK / 2 fail
- Pierwszy fail: ping #5 (~32 s) — wcześniejszy cliff niż przy 1000 B
- Probe po failu: node↔DUT 16 B / 1000 B OK
- Cliff: `radio tx timeout` ×4
- Koniec: `RcpCmdError` ping #101 — `ot-ctl ipaddr mleid` brak odpowiedzi
- Artefakty: `/tmp/ping_debug_54l15_20260806_082144`

**Wnioski cliff_debug (08:15–08:21):**

- Wysoki rate (0.1 s): cliff po kilkudziesięciu pingach (1000 B) lub bardzo wcześnie (512 B, ping #5)
- Niski rate (2 s): 120 s bez problemu (53×1000 B OK) — **workaround rate**
- ERROR = padnięcie ot-ctl/netns po degradacji RCP, nie assert testu
- Probe po failu potwierdza: sieć wraca po cliff, ale host/RCP degraduje się kumulatywnie aż do martwego netns

**Potwierdzenie sesji 09:08 (Run 1):** reprodukowalny cliff z pełną diagnostyką — **75× ping 1000 B OK** (RTT ~214–231 ms) → fail @ **ping #76** → `radio tx timeout` @ ~53.5 s → `RecoverFromRcpFailure()` @ ~61.6 s. PCAP 252 KB. Artefakty: `/tmp/54l15_session_20260806_090809/run1_cliff_debug/`, `/tmp/ping_debug_54l15_20260806_091000/`. Nadaje się do issue Nordica.

---

### Grupa G — IPv6 Fragmentation (2026-08-06)

Dwa runy tego samego dnia — wcześniejszy (08:44, jedna sesja TM) i powtórka z archiwizacją (09:08–09:21, `--skip-erase`).

**Komenda:**

```bash
ctf_tm -c session -p tests_morty/functional \
  -e 'test_case_id=*IPv6_fragmentation* -a dut.dev.attr_zephyr_board=nrf54l15dk_nrf54l15_cpuapp'
```

**Setup:**

| Rola | Urządzenie |
|------|------------|
| DUT (leader) | nRF54L15 (1057766367), RCP debug, ot-daemon |
| Node (router/SED) | nRF52840 (1050202559), ot-daemon |
| Sniffer | nRF52840 (15AAA4D56C0B3DBD) |

#### Run 08:44 — pierwsza sesja TM

**Wynik sesji:** 3 testy / 176 s → 1 PASS, 2 FAIL (exit 20)  
**Kolejność:** ping → ping_sed → thput

| Test | Czas | Status | Gdzie / dlaczego padło |
|------|------|--------|------------------------|
| `fragmentation_for_ping` | 35 s | **FAIL** | Ping 1700 B — brak ICMP reply (1600 B OK) |
| `fragmentation_for_ping_with_sed` | 61 s | **PASS** | 1600 B i 1700 B × 5 OK (SED) |
| `fragmentation_for_thput` | 63 s | **FAIL** | `RecoverFromRcpFailure` na DUT w tearDown |

#### Run 09:08–09:21 — sesja archiwizacyjna (szczegóły w [Grupa H](#grupa-h--sesja-archiwizacyjna-2026-08-06-0908))

| Test | Status | Kluczowa różnica vs 08:44 |
|------|--------|---------------------------|
| `fragmentation_for_ping` (2a) | **FAIL** | 1600 B OK (377 ms, frag TX+RX); 1700 B — fragmenty TX idą, brak kompletnego echo |
| `fragmentation_for_ping` (2b) | **FAIL setUp** | Martwy RCP po thput — nie powtórzenie 1700 B |
| `fragmentation_for_ping_with_sed` | **ERROR** | 1600 B × 5: **5 TX / 7 RX** — parser fail; stack/radio OK |
| `fragmentation_for_thput` | **FAIL** | Thput 58.8 kbit/s OK, 6 frag w PCAP; crash ~13 s od iperf |

#### 15. `test_IPv6_fragmentation_for_ping` — FAIL

**Cel:** ping DUT→router z fragmentacją IPv6 (1600 B i 1700 B)

| Krok | Wynik |
|------|--------|
| Setup: topologia leader+router, ping 64 B obustronnie | ✓ OK |
| Ping 1600 B | ✓ OK (~377–425 ms); Fragment 1 (1232) + Fragment 2 (376) TX+RX |
| Ping 1700 B | ✗ FAIL — fragmenty TX (1232 + 476), częściowo RX z routera, brak kompletnego echo |

**Przyczyna:** `PingError: ICMP reply not received` (`tests_ipv6_fragmentation.py:223`)  
**Uwagi:** padło na drugim rozmiarze (1700 B), nie na pierwszym (1600 B); brak crash RCP w tym teście

#### 16. `test_IPv6_fragmentation_for_ping_with_sed` — PASS (08:44) / ERROR (09:08)

**Cel:** ping DUT→SED z fragmentacją (1600 B i 1700 B, count=5)

| Run | Wynik |
|-----|--------|
| 08:44 | ✓ PASS — 1600 B × 5 i 1700 B × 5 OK (~7 s każdy) |
| 09:08 | ✗ ERROR — 1600 B: 5 TX / 7 RX (RTT 664–3859 ms); `Received more packets than transmitted`; 1700 B nie doszedł |

**Interpretacja:** duże pingi do SED **działają na poziomie radia/stacka** (indirect TX, data poll, `TransmitDataIndirect`). Fail 09:08 to **parser pinga** (duplikaty echo przy pollingu SED), nie brak łączności.

#### 17. `test_IPv6_fragmentation_for_thput` — FAIL (funkcja OK, crash RCP)

**Cel:** UDP iperf 1600 B / 25 s DUT→router; test sprawdza **trzy niezależne rzeczy**.

| # | Co test weryfikuje | Próg |
|---|-------------------|------|
| A | Throughput iperf > 0 | avg kbit/s > 0 |
| B | Fragmentacja IPv6 w PCAP | ≥1 pakiet z `ipv6.fraghdr` |
| C | Brak crashu RCP (crash observer w tearDown) | brak `RecoverFromRcpFailure()` |

**Werdykt ogólny:** A ✓, B ✓, **C ✗** → TM raportuje **FAIL**, choć sam transfer i fragmentacja działają.

---

**Run 08:44** (~63 s)

| Aspekt | OK? | Szczegóły |
|--------|-----|-----------|
| Sieć / setup | ✓ | Topologia leader+router; pingi setup po początkowych failach → OK |
| iperf start | ✓ | UDP `-l 1600 -b 150k -t 25`, DUT→node |
| **A — throughput** | ✓ | avg **38.23 kbit/s** (> 0) |
| iperf jakość | ✗ | duża utrata (np. 96% w sec 8–9); wiele interwałów 0 kbit/s |
| **B — PCAP fragmentacja** | ✓ | **20** pakietów z fragmentacją IPv6 |
| **C — brak crashu RCP** | ✗ | `RecoverFromRcpFailure()` ~24 s od startu iperf (~51 s uptime ot-daemon) |
| **Wynik TM** | **FAIL** | tearDown: `AssertionError: Crash observed on boards: dut_0/leader/1057766367` |

---

**Run 09:08** (~56 s) — sesja archiwizacyjna, `--skip-erase`

| Aspekt | OK? | Szczegóły |
|--------|-----|-----------|
| Sieć / setup | ⚠ | 2× fail node→DUT w setup ping, potem DUT→node OK |
| iperf start | ✓ | ten sam profil UDP 1600 B |
| **A — throughput** | ✓ | avg **58.8 kbit/s** (> 0) — tylko **sec 1** aktywna (5 datagramów) |
| iperf jakość | ✗ | sec 2–27: **0 transfer** — klient umarł po crashu RCP |
| **B — PCAP fragmentacja** | ✓ | **6** pakietów z `ipv6.fraghdr` |
| **C — brak crashu RCP** | ✗ | `radio tx timeout` ~28 s uptime → `RecoverFromRcpFailure` ~36 s (~13 s od startu iperf) |
| **Wynik TM** | **FAIL** | ten sam crash observer w tearDown |

---

**Co to znaczy w praktyce**

| Warstwa | Status | Komentarz |
|---------|--------|-----------|
| OpenThread / IPv6 fragmentation | **OK** | Duże UDP 1600 B idą, fragmentacja widoczna w PCAP |
| iperf / app layer (krótko) | **OK** | Throughput assert przechodzi — transfer się zaczął |
| RCP firmware / radio TX | **BUG** | Ten sam cliff co ping 1000 B: sustained TX → `radio tx timeout` |
| Wynik test frameworka | **FAIL** | Observer wykrywa crash po zakończeniu iperf |

**Nie mylić z:** brakiem fragmentacji (działa) ani fail assertu throughput (przeszedł).

**Artefakty:** 08:44 — `outcomes/pcap/*fragmentation_for_thput.pcap`; 09:08 — `/tmp/54l15_session_20260806_090809/run4_ipv6_thput/`

**Wnioski IPv6 fragmentation (cała Grupa G):**

- Fragmentacja **1600 B OK** (router); **1700 B** — TX fragmentów jest, brak echo (router only)
- Do SED duże pingi idą przez air; TM może raportować ERROR przez parser (5 TX / 7 RX)
- Sustained UDP 1600 B → ten sam RCP crash co cliff_debug
- **Przed kolejnymi testami:** power-cycle obu płytek (RCP martwy po cliff/thput)

**Artefakty:**

- 08:44: `outcomes/results/results_antonia-Legion.nordicsemi.no_20260806084423.json`, PCAP `*IPv6_fragmentation_for_thput.pcap`
- 09:08: `/tmp/54l15_session_20260806_090809/` — `run2_ipv6_ping_router/`, `run3_ipv6_ping_sed/`, `run4_ipv6_thput/`

---

### Grupa H — sesja archiwizacyjna 2026-08-06 09:08

Pełna notatka: [nrf54l15_session_20260806_090809.md](nrf54l15_session_20260806_090809.md)  
**Folder:** `/tmp/54l15_session_20260806_090809/` (wskaźnik: `/tmp/54l15_last_session.txt`)  
**Platforma:** antonia-Legion.nordicsemi.no  
**Firmware:** `test_objects/rcp/debug/thread_rcp_ftd_skyworks_nrf54l15dk_nrf54l15_cpuapp.hex`

| Run | Folder | Test | TM | Uwagi |
|-----|--------|------|-----|-------|
| 1 | `run1_cliff_debug/` | `cliff_debug` `debug_120s_1000B_0_1s` | ERROR | 75 OK → #76 fail; pełne logi + PCAP 252 KB |
| 2a | `run2_ipv6_ping_router/` | IPv6 ping → router | FAIL | **1600 OK, 1700 FAIL** — główny artefakt |
| 2b | — | IPv6 ping → router (retry) | FAIL setUp | Martwy RCP po Run 4, nie 1700 B |
| 3 | `run3_ipv6_ping_sed/` | IPv6 ping → SED | ERROR | Fragmentacja OK, parser fail |
| 4 | `run4_ipv6_thput/` | iperf thput | FAIL | Thput+PCAP OK, RCP crash |

**Mapa pułapek archiwizacji:**

- `run2_cliff_debug/` — duplikat Run 1 + JSON z 2. próby IPv6 ping (setUp fail)
- `run3_ipv6_ping_router/`, `run4_ipv6_ping_router/` — duplikaty / zła nazwa

**Known issues frameworka (nie bug RCP):**

- `_probe_connectivity_on_failure()` pada na martwym netns → cliff debug kończy się ERROR zamiast FAIL
- tearDown: `vendor:fault:info` → `InvalidCommand` (szum na bare-metal)
- Parser pinga: `Received more packets than transmitted` na SED z duplikatami echo

**Archiwizacja do issue:**

```bash
tar czf /tmp/54l15_logs_for_issue.tar.gz -C /tmp 54l15_session_20260806_090809
```

---

## Wnioski ogólne

### Działa (potwierdzone testami)

- Formowanie i utrzymanie sieci Thread z operational dataset
- Commissioning (board + SED), commissioner, leader, router
- Recovery po pin resecie DUT bez restartu ot-daemon (MRT-797)
- MLE Route64 TLV i router ID mask przy zmianie topologii
- CoAP address query / notification
- Komendy diag (channel, send, power, repeat) z FEM Skyworks
- Energy scan inicjowany przez commissionera
- Alternate RLOC16 podczas awansu REED → router (RLOC transition, timeout alt RLOC)
- Ping 1000 B @ rate 2 s — 53/53 OK (cliff_debug 2026-08-06)
- IPv6 fragmentation ping **1600 B** — OK (router: frag 1232+376 TX+RX; SED: indirect TX OK)
- IPv6 fragmentation w PCAP — potwierdzona (6–20 fragmented packets, zależnie od runu)

### Wymaga uwagi / FAIL

- **RCP cliff (priorytet):** degradacja TX pod sustained duże pakiety. Reprodukowalny: **75× OK → fail @ ping #76** (1000 B @ 0.1 s, sesja 09:08). Rate 0.1 s → cliff; rate 2 s → 53×1000 B OK. Mechanizm: `radio tx timeout` → `RecoverFromRcpFailure()` @ `radio_spinel.cpp:2068`.
- **`test_IPv6_fragmentation_for_ping`:** ping **1700 B** DUT→router — fragmenty TX idą, brak kompletnego echo (1600 B OK)
- **`test_IPv6_fragmentation_for_ping_with_sed`:** stack OK; TM może raportować ERROR przez parser (`5 TX / 7 RX` — duplikaty echo SED)
- **`test_IPv6_fragmentation_for_thput`:** asserty A (thput) i B (PCAP frag) **OK**; fail tylko na C (crash RCP w tearDown) — to bug firmware, nie brak fragmentacji
- **`test_alternate_rloc16_during_role_transition`:** ping leader → DUT w oknie awansu roli — 100% loss; wspólny root cause z TX RCP

### Known issues frameworka (nie bug RCP)

- `_probe_connectivity_on_failure()` → ERROR zamiast FAIL po crashu RCP
- Parser pinga: `Received more packets than transmitted` na SED
- `vendor:fault:info` → `InvalidCommand` w tearDown (bare-metal)

### Nie testowane / poza zakresem

- `test_short_address_filtering` — suite deprecated, feature Rx short-address filtering niezaimplementowany
- `test_csma_ca_*` i inne `vendor:*` — nie dla bare-metal RCP
- Liczniki vendor / `vendor:fault:info` — niedostępne na bare-metal RCP (oczekiwane, nieblokujące)

### Następne kroki

Pełna lista priorytetów (PCAP, izolacja rate/size, zmiany w teście auto, szablon issue): [handoff §9](nrf54l15-handoff-2026-08-05.md#9-następne-kroki-dla-agenta-priorytet).

Skrót:

1. ~~Analiza PCAP~~ → artefakty gotowe w `/tmp/54l15_session_20260806_090809/` — `run1_cliff_debug`, `run2_ipv6_ping_router`, `run4_ipv6_thput`
2. ~~Izolacja rate~~ → rate 2 s OK; cliff @ 0.1 s reprodukowalny (75 pingów)
3. **Złożyć issue Nordic** z Run 1 + Run 2a + Run 4 ([szablon](nrf54l15-handoff-2026-08-05.md#10-szablon-issue-copy-paste))
4. Poprawić `_probe_connectivity_on_failure()` i parser pinga SED w test-fw
5. Cliff debug wariant `debug_120s_1000B_2s` — potwierdzenie workaround rate
6. Debug `test_alternate_rloc16` — sniffer w oknie ~6 s alternate RLOC
7. **Przed testami:** power-cycle płytek po cliff/thput
