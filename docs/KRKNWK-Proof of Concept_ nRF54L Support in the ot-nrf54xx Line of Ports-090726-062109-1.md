## Proof of Concept: nRF54L Support in the ot-nrf54xx Line of Ports 

Purpose: This page defines the scope, hypothesis, deliverables, and success criteria for a proof of concept to evaluate bare-metal nRF54L support using a newer nrfx baseline, without adopting Nordic Connect SDK (NCS) or Zephyr. 

The ot-nrf528xx repository provides bare-metal platform drivers and build integration for running OpenThread on Nordic nRF52811, nRF52833, and nRF52840 SoCs. The port uses a CMake-based toolchain, OpenThread as a submodule, and a vendored set of Nordic components derived from the legacy nRF5 SDK, including nrfx , MDK, platform-specific radio drivers, optional SoftDevice support, CMSIS, and related libraries. 

The long-term goal of this effort is to extend that same architectural approach to the nRF54L series and related nRF54 devices: deliver OpenThread platform support that is independent of NCS and Zephyr RTOS, while remaining familiar to developers who already use ot-nrf528xx for nRF52 products such as RCP, full Thread stack on SoC, and Spinel transport. 

This document describes the purpose and scope of the proof of concept (POC). The POC is not the full OpenThread port. Its role is to de-risk the foundational decision of how to obtain and integrate low-level Nordic software for nRF54L without depending on NCS. 

## Background and Project Goal 

The current nRF52-focused port is built around a legacy generation of nrfx bundled under 

third_party/NordicSemiconductor/nrfx . That version supports older Nordic device families such as nRF51 and nRF52, but it does not include the device support, HAL, MDK, or driver coverage required for nRF54L. 

The desired outcome is to preserve the strengths of the current ot-nrf528xx model: 

Bare-metal bring-up 

- CMake-based build flow 

- No Zephyr RTOS dependency 

- Direct control over vendored platform layers 

A developer experience consistent with existing nRF52 OpenThread ports 

Key idea: The POC is focused on the low-level software baseline only. It is intended to confirm whether nRF54L can be supported in the same style as ot-nrf528xx before investing in full OpenThread platform work. 

## The Core Problem 

The challenge is not a routine driver upgrade. The current ot-nrf528xx foundation is tightly coupled to an older Nordic software layout and assumptions inherited from the legacy nRF5 SDK. 

nRF54L introduces changes that the existing tree does not model, including: 

- Different peripheral instances 

- Updated clock and power architecture 

- Different radio interface expectations 

Potentially different startup and toolchain requirements 

At the same time, Nordic software that fully supports nRF54L is primarily distributed through the NCS ecosystem, where newer nrfx revisions are maintained alongside Zephyr, board support, and a larger dependency graph. 

|Issue|Current State|Impact|Why It Matters|
|---|---|---|---|
|Legacy nrfx baseline|Bundled ot-nrf528xx<br>nrfx does not support<br>nRF54L|Current platform<br>cannot access<br>nRF54L hardware<br>correctly|Basic device support<br>is missing|
|Upgrade complexity|Platform code<br>depends on old SDK<br>structure and<br>assumptions|In-place upgrade<br>would require<br>significant refactoring|Thisis not a simple<br>version bump|
|NCS availability<br>model|Known-good nRF54L<br>supportis packaged<br>within NCS workflows|Adoptingit wholesale<br>would pullin Zephyr-<br>orientedinfrastructure|That conflicts with the<br>existing bare-metal<br>approach|



The problem can be summarized in two parts: 

1. The existing ot-nrf528xx foundation cannot talk to nRF54L hardware using its vendored nrfx . 

2. The proven nRF54L-ready nrfx exists in the NCS ecosystem, but adopting NCS as-is would undermine the lightweight, Zephyr-free architecture that makes ot-nrf528xx appealing. 

## POC Hypothesis 

## nrfx 

## nrfx_glue 

This hypothesis is grounded in how nrfx  is designed. It is intended to function as a standalone driver layer, with host environments supplying a thin integration layer such as: 

nrfx_glue macros 

Logging bindings 

IRQ mapping and handlers 

Include path configuration 

Although NCS uses nrfx together with Zephyr, nrfx  itself is not inherently Zephyr-specific. 

If the hypothesis holds: a follow-on phase can focus on OpenThread platform bring-up, radio integration, transport layers, and repository structure rather than first solving basic driver and build-system feasibility. 

## Purpose and Scope of the POC 

The POC exists to answer a small number of high-value technical questions early, with limited scope and clear success criteria. 

## Primary objectives 

## 1. Validate standalone nrfx integration in OpenThread RCP build 

Demonstrate that an nRF54L-capable nrfx revision builds and runs on selected nRF54L hardware using only bare-metal tooling such as GNU Arm toolchain, CMake or equivalent, startup code from MDK, and project-local nrfx_glue , with no Zephyr kernel, no NCS west manifest, and no hard dependency on Zephyr device tree. 

## 2. Identify the minimal Nordic dependency surface 

Determine which Nordic components are actually required for nRF54L support, such as MDK, HAL, SoC headers, clock or power helpers, and any memory or TrustZone-related constraints where applicable. Also identify which NCS shims can be omitted or reimplemented locally in the same style as ot-nrf528xx. 

## 3. Characterize gaps versus the nRF52 port 

Document the differences that matter for a future OpenThread port, especially around radio driver source, alarm or timer mechanisms, entropy or TRNG, flash layout, UART or USB for 

Spinel, FEM control, and any multicore or coprocessor topology relevant to the chosen nRF54L target. 

## 4. Prove build and debug workflow 

Show that developers can build, flash, and debug the image using familiar tools such as nrfutil 

device and J-Link, while also obtaining basic runtime observability through RTT or UART. 

## 5. Support a go/no-go decision 

Produce concrete evidence and a concise list of blockers, if any, to determine whether a full OpenThread-style port is practical on top of the new nrfx baseline. 

## Out of scope 

The POC is intentionally limited. It should not expand into a full productization effort 

unless a minimal stepping stone is needed for basic bring-up, such as UART logging. 

- Full OpenThread stack 

- Feature parity with ot-nrf528xx, including all transport modes, board variants, or SoftDevicerelated capabilities 

- Production-ready security, OTA, or certification outputs 

- Refactoring or replacing the existing nRF52 tree in ot-nrf528xx 

- Publishing an official upstream OpenThread platform before a follow-on implementation phase 

## Relationship to ot-nrf528xx and Expected Outputs 

The current nRF52 port is organized roughly into three layers: 

- Common platform sources such as alarm, radio glue, transport, flash, and crypto 

- Per-SoC CMake fragments for nRF52840, nRF52833, and nRF52811 

- third_party/NordicSemiconductor content including legacy SDK slices, older nrfx , 

- and radio drivers 

A successful POC does not need to replicate that full structure. Instead, it should prove that the bottom layer can be re-established for nRF54L using a newer nrfx outside NCS. 

The output should also capture which ot-nrf528xx modules are likely: 

- Reusable with minor changes 

- In need of nRF54-specific adaptation 

- In need of a complete rewrite, especially radio-related components 

It should additionally help answer a structural question: whether both nRF52 and nRF54 families belong in one repository or whether a sibling project such as ot-nrf54xxx would be cleaner. 

## POC deliverables 

- Minimal firmware image running on nRF54L hardware, such as OpenThread RCP 

- CMake-based or equivalent documented build flow listing the exact nrfx and MDK sources and include paths, with no Zephyr or NCS runtime dependency 

- Documented nrfx_glue implementation and IRQ handler wiring for the chosen toolchain 

- Written findings covering required dependencies, key risks, estimated OpenThread bring-up effort, and recommendation to proceed or pivot 

## Reference material 

- ot-nrf528xx: current bare-metal OpenThread port for nRF528xx 

- sdk-mrt or equivalent NCS workspace: source of nrfx and MDK revisions known to support nRF54L 

- nrfx integration documentation: guidance for standalone use through nrfx_glue and 

- include path configuration in bare-metal or RTOS environments 

The POC may use sdk-mrt as a read-only reference for selecting the correct nrfx commit, MDK files, and integration patterns, but it should not become a runtime or build-time dependency. 

## Success criteria 

|Criterion|Definition of Success|Status Meaning|
|---|---|---|
|Build and link|New nRF54L-capable<br>RCP firmware<br>builds and linksin a<br>bare-metal project<br>without NCS or Zephyr|REQUIRED|
|Hardware execution|Theimage runs on<br>target silicon and<br>demonstrates basic<br>peripheral access<br>through<br>nrfx APIs|CRITICAL|
|Porting clarity|Major blockers for a full<br>OpenThread-style port<br>areidentified,with no|DECISION INPUT|



**==> picture [129 x 62] intentionally omitted <==**

unknown mandatory Zephyr dependency for low-level drivers 

**==> picture [130 x 62] intentionally omitted <==**

Summary: Supporting nRF54L in the spirit of ot-nrf528xx requires a new Nordic low-level software baseline because the repository's legacy nrfx cannot target nRF54L. This POC determines whether an NCS-era nrfx can be integrated directly in a bare-metal environment, preserving the ot-nrf528xx development model. 

