/* ACE hardware register definitions for v5.2 compatibility */
#ifndef HW_ADSP_ACE_H
#define HW_ADSP_ACE_H

/* QEMU specific timing configurations */
#define ACE_TIMER_MULTIPLIER 5

/* ROM */
#define ADSP_ACE_DSP_ROM_BASE 0x9f000000
#define ADSP_ACE_DSP_ROM_SIZE 0x2000

/* ACE30 Memory Map */
#define ADSP_ACE30_DSP_LP_SRAM_BASE     0xa0000000  /* Starts after HP SRAM */
#define ADSP_ACE30_DSP_IMR_BASE         0xa1000000

/* ACE40 (NVPS) Memory Map */

/* ACE40 IO blocks used by the current simulator model. */
#define ADSP_ACE40_DSP_DMW_STRIDE               0x08
#define ADSP_ACE40_DSP_DMW_COUNT                16

/* ACE30 IO */
/*
 * DSP Memory Space naming (simple, non-formula entries) aligned to docs.
 */

/* DMA */

/* Backward-compatible aliases for previous _LOW_ names. */


/* DMW — DSP Memory Window x (DSPMEM.DMW, ACE3.x IP Registers §2.8.65)
 * DfDMWCP.PTR = 0x070200; stride 0x08/window; up to DMWC=16 windows.
 * Per-window: DMWXBA@+0x00 (base+config), DMWXLO@+0x04 (limit/curtain). */
#define ADSP_ACE30_DSP_DMW_STRIDE       0x08
#define ADSP_ACE30_DSP_DMW_COUNT        16


/* DfPMCCU — DSP Power Management and Clock Control Unit (DSPMEM.DfPMCCU, §2.8.73–74)
 * Base: 0x071B00 (= DFCAPSTS_BASE); size 0x100 (next block DfCW at 0x71C00).
 * Combines DfPMCCU (ULP, RO capability/status) and DfPMCCU_AON (ULP_AON, RW control). */
/* ACE30 L2 HP SRAM Bank Power Management registers (DfL2HSBPMPTR.PTR=0x17A800). */

/* HDA Gateway */
/* DSP Gateway Host Input Streams (DGHIS — SNDW gateway per FW, DSPMEM §2.8.84)
 * Base: 0x72C00 (= DGHIS); 8 streams × 0x40 = 0x200 (ends 0x72DFF).
 * Used by SoundWire PCM capture streams. */
/* Reserved gateway window between DGHIS and HFIPC (0x72E00-0x72FFF).
 * Keep this region mapped so firmware probes do not hit unmapped space. */
/* DSP Gateway Link Output Streams (DGLOS — link/ALH gateway out per FW, DSPMEM §2.8.81)
 * Base: 0x79400; 8 streams × 0x40 = 0x200 (between SSP2@0x79000 and SSP3@0x7A000).
 * Used by SoundWire/SSP audio output via ALH. */
/* Reserved gateway window between ALH-out and DMIC-in (0x79600-0x797FF).
 * Keep this region mapped so firmware probes do not hit unmapped space. */
/* DSP Gateway Link Input Streams (DGLIS — DMIC/link-in gateway per FW, DSPMEM §2.8.82)
 * Base: 0x79800; 8 streams × 0x40 = 0x200.
 * Used by DMIC and SoundWire/SSP audio capture via ALH. */
/* Reserved gateway window after DMIC-in (0x79A00-0x79BFF).
 * Keep this region mapped so firmware probes do not hit unmapped space. */

/* ACE30 Host Firmware IPC (DfHIPCCP.PTR=0x073000, SIZE=0x9 → 0x1000 bytes) */

/* ACE30 DINT per-core blocks (DSPMEM.DINT, DfINTCP.PTR=0x091000, stride=0x100) */
/* ACE30 IDC per-core blocks (DSPMEM.IDCC, DfIDCCP.PTR=0x092000, stride=0x400) */

/* ACE30 Host Firmware IMR registers */
#define ADSP_ACE30_DSP_HFIMR_BASE       0x000162000
/* ACE30 DSP Core Status registers (DfDSP2CCP.PTR=0x178D00) */
/* ACE30 L2 ULP SRAM Bank Power Management registers (DfL2USBPMPTR target). */
/* ACE30 L2 Memory Management registers (HSTfMEM2.DfL2MM shadow) */
/* Backward-compatible aliases for previous non-doc-aligned names. */
/* Historical alias kept for existing users. */
/* ACE30 L2 HP SRAM TLB registers (DfL2HSTLBCP.PTR=0x17E000, SIZE=0xA -> 0x2000) */

/* ACE3.x DfL2MM register offsets */

/* ACE3.x DfL2MM fields */




/* Manifest offset */

/* IRQ numbers */

/* Device IRQs */
#define IRQ_IPC         0
#define IRQ_LPGPDMA     2

#define IRQ_SSP0        17
#define IRQ_SSP1        18
#define IRQ_SSP2        19
#define IRQ_SSP3        20
#define IRQ_SSP4        21
#define IRQ_SSP5        22

/* correct for ace */
#define IRQ_DWCT0       10
#define IRQ_DWCT1       11

/* IPC registers - legacy CAVS */

/* IPC registers - CAVS 1.8+ (Cannonlake/Icelake/Tigerlake) and ACE3.x
 * 
 * Register naming for ACE3.x IPC:
 * - DIPCTDR: DSP IPC Target Doorbell Request (host->DSP, receive side)
 * - DIPCTDA: DSP IPC Target Doorbell Acknowledge (DSP acknowledges host message)
 * - DIPCTDD: DSP IPC Target Doorbell Data (host->DSP data payload)
 * - DIPCIDR: DSP IPC Initiator Doorbell Request (DSP->host, send side)
 * - DIPCIDA: DSP IPC Initiator Doorbell Acknowledge (host acknowledges DSP message)
 * - DIPCIDD: DSP IPC Initiator Doorbell Data (DSP->host data payload)
 * - DIPCCTL8: IPC Control register
 * - DIPCCST: IPC Command/Status (Sideband IPC control)
 * 
 * ACE3.x introduces BUSY/DONE handshaking protocol:
 * - Initiator sets BUSY bit when sending message
 * - Target clears BUSY bit to acknowledge receipt
 * - Target clears BUSY in acknowledge register to set DONE (task complete)
 * - Initiator clears DONE bit after reading completion status
 */

/* IPC bits - Common across CAVS versions */

#if 0
/* IRQ ILRSD macros */
#endif

/* ACE3.x Power Management Registers (SHIM registers)
 * These registers control power gating domains and DSP core power states.
 * Reference: ACE3.x IP HAS Section 8.5 "Power Management"
 */

/* ACE3.x Discovery Registers */


/* PWRCTL (Power Write Control) - Offset 0x90
 * Controls power gating for various DSP subsystem domains.
 * Each bit enables (1) or gates (0) power for its respective domain.
 */

/* PWRSTS (Power Status) - Offset 0x92
 * Read-only register indicating current power status of domains.
 * Bits mirror PWRCTL layout: 1=powered on, 0=powered off/gated.
 */

/* DSSCS (DSP Subsystem Control/Status) - Offset 0xF0
 * Global DSP subsystem control for D-state transitions.
 */

/* DSPCxCTL (DSP Core x Control) - Offsets 0x100-0x110
 * Per-core control registers for boot, reset, and ROM bypass.
 * Boot sequence: Set PWRCTL.WPDSPxPG=1, poll PWRSTS.DSPxPGS=1,
 *                then set DSPCxCTL.SPA=1, poll DSPCxCTL.CPA=1.
 */

/* ACE3.x DMA Registers
 * ACE IP includes multiple DMA engines for audio and data transfers:
 * - HD Audio Stream DMA: For Host/Link audio streaming
 * - General Purpose DMA (GPDMA): For DSP I/O peripherals (I2C, UART, etc)
 * Reference: ACE3.x IP HAS Section 8.5.2 "DMA Engines"
 */

/* HD Audio Stream DMA Control - Per stream registers
 * Input/Output streams for HD Audio host and link DMA operations.
 * Stream count defined by HSTISC/HSTOSC parameters.
 */

/* HD Audio Stream Control Register (SDxCTL) bits */

/* HD Audio Stream Status Register (SDxSTS) bits */

/* General Purpose DMA (GPDMA) Registers
 * Up to 4 GPDMA controllers with up to 8 channels each.
 * Used for DSP I/O peripheral data transfers (I2C, UART, SPI, etc).
 * Parameters: GPDMACC (controller count), GPDMACxCHC (channel count per controller)
 */

/* GPDMA Channel registers (per channel, per controller) */

/* GPDMA Control register bits */

/* GPDMA Configuration register bits */

/* DMA Interconnect and Power Management */

/* DMA Global Control bits */

/* ACE3.x Inter-DSP Communication (IDC) Registers
 * IDC provides message passing between Tensilica cores using doorbell interrupts.
 * Each core has "Agent A" (initiator) and "Agent B" (target) register sets.
 * Typically DSP Core 0 acts as IDC service center using all Agent A registers.
 * Reference: ACE3.x IP HAS Section 8.5.1 "Inter-DSP Communication"
 * 
 * IDC Message Flow (Core 0 -> Core 1):
 * 1. Core 0 writes message to DAIPCIDR (through Core 1's IDC registers)
 * 2. Core 0 can write extended data to DAIPCIDD for QW (quad-word) messages
 * 3. Core 0 sets BUSY bit in DAIPCIDR to generate interrupt to Core 1
 * 4. Core 1 reads message from DBIPCTDR and optional DBIPCTDD
 * 5. Core 1 clears BUSY bit in DBIPCTDR to acknowledge
 * 6. Clearing BUSY sets DONE bit in DAIPCIDA
 * 7. Core 0 polls DONE or waits for DONE interrupt (if IPCIDIE enabled)
 */

/* IDC Agent A - Initiator Registers (DSP->DSP send side) */

/* IDC Agent B - Target Registers (DSP->DSP receive side) */

/* IDC Doorbell Request register bits (DAIPCIDR, DBIPCTDR) */

/* IDC Doorbell Acknowledge register bits (DAIPCIDA, DBIPCTDA) */

/* IDC Control register bits (DAIPCCTL, DBIPCCTL) */

/* IDC routing control (SHIM registers) - per core routing */

/* ACE3.x Wall Clock and Timers
 * Provides 64-bit free-running wall clock counter and programmable timers.
 * DSP Wall Clock (DSPWC): Increments on XTAL oscillator or resume clock
 * Timers: Compare DSPWC against future values and generate interrupts
 * RTC Wall Clock: Real-time passage tracking using RTC clock source
 * Reference: ACE3.x IP HAS Section 8.5.7 "DSP Wall Clock and Timers"
 * 
 * Clock Sources:
 * - XTAL Oscillator: 19.2 / 24 / 24.576 / 38.4 MHz (audio operations)
 * - Resume Clock: 32.768 KHz crystal (low power sensing, WoV)
 * - RTC Clock: Real-time tracking through S0i3/S3 states
 */

/* DSP Wall Clock Registers (SHIM region) */
#define SHIM_DSPWC             0x20   /* DSP Wall Clock Counter (64-bit) */

/* DSP Wall Clock Control (DSPWCCTL) bits */

/* RTC Wall Clock Registers - typically at higher offsets */

/* Timer Registers - Two programmable timers
 * Note: SHIM_DSPWCTT0C (0x30), SHIM_DSPWCTT1C (0x38), SHIM_DSPWCTTCS (0x40) defined in shim.h
 */

/* Timer Control/Status bits (SHIM_DSPWCTTCS) */

/* Time Stamping Control - Synchronization with SoC ART */

/* Time Stamp Capture Registers */

/* Watch Dog Timer Registers */

/* Clock Control and Status Registers (SHIM space 0x78-0x7C) */
/* Note: SHIM_CLKCTL also defined in shim.h for compatibility */
#define SHIM_CLKCTL            0x78    /* Clock Control */
#define SHIM_CLKSTS            0x7C    /* Clock Status */

/* CLKCTL - Clock Control Register Bits */

/* CLKSTS - Clock Status Register Bits */

/* Host IPC Interrupt Routing Registers (per-core, SHIM space) */
/* DxHIPCIE - DSP x Host IPC Interrupt Enable (routes host IPC to specific core) */
/* DxSBIPCIE - DSP x Sideband IPC Interrupt Enable (routes sideband IPC to core) */

/* HIPCIE/SBIPCIE - IPC Interrupt Enable bits (one bit per IPC instance) */

/* HIPCIS - Host IPC Interrupt Status (read-only, shows which IPC fired) */

/* SoC Power State and Resource Request Registers (SHIM space) */

/* SPSREQ - SoC Power State Request Register Bits */

/* SPSRSP - SoC Power State Response Register Bits */

/* Latency Tolerance Reporting Register (SHIM space) */

/* LTRC - Latency Tolerance Reporting Control Bits */

/* L2 Local Memory Configuration and Protection (SHIM space) */

/* L2LMCAP - L2 Local Memory Capabilities Bits */

/* L2MPAT - L2 Memory Protection Attributes Bits */

#endif /* HW_ADSP_ACE_H */
