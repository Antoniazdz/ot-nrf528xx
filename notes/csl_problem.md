# nRF54L15 — handoff: CSL / timestamper (SED vs SSED)

**Data:** 2026-08-14  
**Kontekst:** Walidacja timestampera na nRF54L15 ot-cli-ftd (bare-metal CLI, bez RCP)  
**Status:** Reprodukcja manualna gotowa; testy TM SSED/CSL nadal FAIL — wymagają doprecyzowania kryteriów pass

---

## Problem (1 zdanie)

**Ping leader → child działa stabilnie jako SED (`csl period 0`), ale jako SSED (`csl period 1000000` = 1 s) pass rate ~30% — wzorzec burst PASS / długie serie FAIL wskazuje na CSL sync / timestamper na parent (54L15), nie na join ani ogólną łączność Thread.**

---

## Sprzęt i firmware

| Rola | Board | Firmware | UART | Prefix CLI |
|------|-------|----------|------|------------|
| **DUT (leader/parent)** | nRF54L15 DK | `hexy/54/ot-cli-ftd.hex` (własny build z timestamperem) lub TM: `ref_objects/ncs_hex/ncs_v2.5.0_thread_cli_test_singleprotocol_nrf54l15dk_nrf54l15_cpuapp.hex` | 115200 | brak (`>`) |
| **Peer (child SED/SSED)** | nRF52840 DK | `ref_objects/ncs_hex/ncs_v2.5.0_thread_cli_test_singleprotocol_nrf52840dk_nrf52840.hex` | 115200 | brak (`>`) na bare-metal |
| **Sniffer** | nRF52840 dongle | opcjonalnie | — | — |

**Uwagi setup:**
- Commissioner **tylko na leaderze** (54L15) — nie włączać na obu boardach jednocześnie.
- `csl` / `csl period` na **leaderze (54L15)** → `Error 7: InvalidArgs` — **oczekiwane** (CSL konfiguruje się na **child**, nie na routerze).
- Parent widzi CSL childa w `child table` (kolumna `CSL`), nie przez lokalne `csl` na DUT.

---

## Konfiguracja manualna (scenariusz referencyjny)

### 54L15 — leader

```
factoryreset
dataset init new
dataset networkkey 00112233445566778899aabbccddeeff
dataset panid 0xabcd
dataset channel 24
dataset commit active
mode rdn
ifconfig up
thread start
state          → leader
ipaddr mleid    → zapisz (adres childa do ping)
```

### 52840 — child (SED lub SSED)

**SED (CSL off):**
```
factoryreset
# join do sieci leadera (dataset / commissioner)
mode -
childtimeout 240
pollperiod 1000          # 1 s — szybsze okna poll niż domyślne 236 s
csl period 0
ifconfig up
thread start
state          → child
csl            → period: 0us
```

**SSED (CSL 1 s):**
```
# jak wyżej, ale przed thread start:
csl period 1000000
csl channel 0
csl timeout 300
mode -
childtimeout 240
pollperiod 236000        # typowe dla SSED w testach TM
```

### Ping (kierunek testowany)

**Leader → child** (CSL indirect TX — wrażliwy kierunek timestampera):

```
ping <child_mleid> 32 1 1 64 15
```

Parametry: size=32, count=1, interval=1 s, hoplimit=64, timeout=15 s.

---

## Wyniki manualne (użytkownik, 2026-08-14)

### A. `csl period 0` (zwykły SED)

| Metryka | Wynik |
|---------|-------|
| Liczba pingów | **10** |
| Pass | **10/10** (100%) |
| Kierunek | leader (54L15) → child (52840) |

**Wniosek:** SED / Data Poll / indirect TX — **stabilne**. Join i ogólna łączność Thread OK.

### B. `csl period 1000000` (1 s, SSED)

| Metryka | Wynik |
|---------|-------|
| Liczba pingów | **10** (pojedyncze komendy, jedna po drugiej) |
| Pass | **3/10** (~30%) |
| Fail | **7/10** — 100% packet loss @ timeout 15 s |

**Szczegóły PASS:**

| icmp_seq | RTT |
|----------|-----|
| 27 | 99 ms |
| 28 | 671 ms |
| 32 | 864 ms |

**Wzorzec:** 2× PASS → 3× FAIL → 1× PASS → 4× FAIL (burst success, potem długie serie porażek).

**Wniosek:** Problem **izolowany do CSL/SSED**. Nie join, nie RF — child w `child table` z `CSL=1`, wcześniejsze pingi przechodzą.

---

## Interpretacja techniczna

1. **SED vs SSED:** Jedyna istotna różnica to `csl period` — ten sam hardware, ta sama topologia.
2. **RTT przy PASS (99 / 671 / 864 ms):** Różne opóźnienia w ramach okna CSL 1 s — spodziewane przy CSL; **nie** wyjaśnia 100% loss przez 15 s (≈15 okien CSL).
3. **Serie FAIL:** Sugerują **utratę synchronizacji CSL** parent↔child albo błąd schedulingu TX CSL na **parent (54L15)** — główny kandydat: **timestamper / CSL TX w firmware 54L15**.
4. **`csl` na leaderze:** InvalidArgs — nie mylić z brakiem CSL; child ma CSL, parent scheduluje CSL indirect.

---

## Stan testów TM (Morty)

**Pliki:** `tests_morty/functional/tests_nrf54l15_cli.py`, `nrf54l15_cli_setup.py`

**Filtr uruchomienia:**
```bash
export BOARD='-a dut.dev.attr_zephyr_board=nrf54l15dk_nrf54l15_cpuapp'
export CLI='-a thread_design=cli'
ctf_tm -c session -p tests_morty/functional -e "test_case_id=TEST ${BOARD} ${CLI}"
```

| Test | Status (ostatnio) | Uwagi |
|------|-------------------|-------|
| `test_setup_rcp_network`, `test_commission_board` | PASS | — |
| `test_commission_sed`, `test_configure_sed` | PASS | SED z `poll_period=1000` |
| `test_configure_ssed` | FAIL | 5× `_ping_leader_to_ssed()`, `sleep(2)` po config |
| `test_commission_ssed` | FAIL/ERROR | `verify_connection=False`; JLink -254 przy pętli sesji |
| `test_csl_ping_counters` | FAIL | oczekuje `TxCslTotal` lub sam ping |

**Istniejąca logika SSED w TM:**
- `_ping_leader_to_ssed()` — tylko DUT→child, timeout 15 s, 3 retry
- `SSED_CSL_PERIOD_US = 1_000_000`, `SSED_CSL_CHANNEL = 0`
- Pętla 5 pingów bez progu pass rate — **1 FAIL = FAIL testu** (zbyt restrykcyjne vs ~30% manual pass rate)

**Infrastruktura:**
- Brak RCP hex dla 54L15 w `test_objects/rcp/debug/` → testy RCP bez `thread_design=cli` nie wystartują.
- Pętla `ctf_tm -c session` bez recover → JLink `-254`.
- `${CLI}` musi być exportowane — inaczej brak `-athread_design=cli`.

---

## Zadania dla następnego agenta

### Priorytet 1 — doprecyzować testy SSED/CSL w TM

1. **`sleep(5–10)`** po `configure_ssed` / `commission_ssed` przed pierwszym pingiem (sync CSL).
2. **Seria pingów z progiem** zamiast „wszystkie muszą przejść”:
   - np. 10 pingów co 1.5 s (`ping ... 32 10 1.5 64 15`) lub pętla z `interval >= csl_period`;
   - pass jeśli ≥ **70%** success (dopasować do oczekiwań produktowych — na razie manualnie 30% = FAIL funkcjonalny, ale test nie powinien flaky failować na pojedynczym pingu).
3. Opcjonalnie: **`test_ssed_ping_pass_rate`** — osobny test raportujący % zamiast binary pass/fail.
4. Po serii: **`counters mac`** / `TxCslTotal` na DUT jeśli dostępne.

### Priorytet 2 — domknąć reprodukcję manualną

```bash
# Na child (52840) — SSED
csl period 1000000

# Na leader — seria z odstępem
ping <mleid> 32 10 1.5 64 15

# Diagnostyka
child table
counters mac
```

Porównać pass rate z serią „ping jeden po drugim bez przerwy” (już ~3/10).

### Priorytet 3 — firmware / issue

- Upewnić się, że TM testuje **`hexy/54/ot-cli-ftd.hex`** (env var w `nrf54l15_cli_setup.py`?) jeśli timestamper jest tylko w własnym buildzie.
- Zebrać logi + wyniki do **issue Nordic**: SED 10/10 vs SSED 3/10, wzorzec burst fail, 54L15 jako parent.

### Nie robić / ostrożnie

- Nie uruchamiać wielu sesji TM z rzędu bez power-cycle / recover JLink.
- Nie testować `csl period` na leaderze — to nie wyłącza CSL childa.
- Wyzerowanie CSL: na **child** `csl period 0` + ewentualnie reattach; pełny reset: `factoryreset` obu.

---

## Kluczowe ścieżki

| Plik | Opis |
|------|------|
| `tests_morty/functional/tests_nrf54l15_cli.py` | Testy 54L15 CLI, SSED helpers |
| `tests_morty/functional/nrf54l15_cli_setup.py` | DUT/tester iface, atrybuty TM |
| `bts/test_base/sub_test_base/thread_base_test_case.py` | `configure_sed`, `configure_ssed`, `commission_board` |
| `tools/scripts/run_nrf54l15_cli_tests.bash` | Skrypt batch testów |
| `hexy/54/ot-cli-ftd.hex` | Firmware użytkownika (timestamper) |
| `notes/nrf54l15_session_20260806_090809.md` | Wcześniejsza sesja (RCP cliff — **inny** problem niż CSL CLI) |

---

## Tekst do issue (szkic)

```
Platform: nRF54L15 DK (ot-cli-ftd, parent/leader) + nRF52840 DK (ot-cli-ftd, child)
Test: ping leader → child, `ping <mleid> 32 1 1 64 15`

CSL period 0 (SED, pollperiod 1000 ms):  10/10 ping OK
CSL period 1000000 µs (SSED):            3/10 ping OK (RTT 99 ms, 671 ms, 864 ms)
                                         7/10 ping 100% loss @ 15 s timeout
Pattern: burst PASS then long FAIL streaks — CSL sync / parent CSL TX scheduling suspect

Child visible in child table with CSL=1; SED path works; failure isolated to CSL enabled.
```

---

## Historia rozmowy

Pełny kontekst: agent transcript `0b65fcac-3144-428c-9958-313c50f32051` (Cursor).
