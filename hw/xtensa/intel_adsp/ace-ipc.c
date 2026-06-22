/* CAVS IPC (Inter-Processor Communication) handling
 * IP Region: Legacy host/DSP IPC bridge registers and software mailbox semantics.
 *
 * Copyright (C) 2016 Intel Corporation
 *
 * Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * IPC Architecture Evolution:
 * 
 * ACE3.x (Audio/Context/Engine IP 3.0+):
 *   - Full bidirectional IPC with Target/Initiator separation
 *   - Multiple host IPC instances support (IPCHAC parameter)
 *   - Sideband IPC for peer agents via IOSF (IPCSAC parameter, up to 8 agents)
 *   - Intra-DSP Communication (IDC) for multi-core coordination
 *   - Interrupt routing to specific cores (DxHIPCIE/DxSBIPCIE/DxIDCAIE)
 *   - BUSY/DONE protocol:
 *     Host->DSP: xIPCIDR.BUSY=1 -> DIPCTDR.BUSY=0 -> task -> DIPCTDA.BUSY=0 
 *                (sets xIPCIDA.DONE=1, xIPCIDR.BUSY=0)
 *     DSP->Host: DIPCIDR.BUSY=1 -> xIPCTDA.BUSY=0 -> task completion -> 
 *                DIPCIDA.DONE cleared by host
 *   - Optional L2 local memory windows for large message payloads (>32 DW)
 * 
 * Register Summary:
 *   DIPCTDR: DSP IPC Target Doorbell Request (host->DSP receive)
 *   DIPCTDA: DSP IPC Target Doorbell Acknowledge (DSP ack to host)
 *   DIPCTDD: DSP IPC Target Doorbell Data (host->DSP data payload)
 *   DIPCIDR: DSP IPC Initiator Doorbell Request (DSP->host send)
 *   DIPCIDA: DSP IPC Initiator Doorbell Acknowledge (host ack to DSP)
 *   DIPCIDD: DSP IPC Initiator Doorbell Data (DSP->host data payload)
 *   DIPCCTL8: IPC Control register
 *   DIPCCST: IPC Command/Status (Sideband IPC control)
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/audio/adsp-dev.h"
#include "hw/adsp/shim.h"
#include "hw/adsp/ace.h"
#include "ace-internal.h"
#include "../trace.h"
#include "qobject/qdict.h"
#include "monitor/monitor.h"
#include "exec/cpu-common.h"
extern struct adsp_dev *g_adsp_dev;
#define IPC_DIPCTDR     0x000
#define IPC_DIPCTDA     0x004
#define IPC_DIPCIDR     0x010
#define IPC_DIPCIDA     0x014
#define IPC_DIPCCST     0x020   /* cst: offset per struct intel_adsp_ipc */
#define IPC_DIPCCTL8    0x028   /* ctl: offset per struct intel_adsp_ipc */
/* tdd/idd: data/extension payload words in struct intel_adsp_ipc.
 * unused0[2] at 0x08-0x0f, unused1[2] at 0x18-0x1f, unused2[52] at 0x30-0xff.
 * tdd (host→DSP ext data) at 0x100, idd (DSP→host ext data) at 0x180. */
#define IPC_DIPCTDD     0x100
#define IPC_DIPCIDD     0x180
#define IPC_DIPCT_DSPLRST   (1u << 31) /* BUSY bit in doorbell registers */
#define IPC_DIPCI_DSPRST    (1u << 31) /* BUSY bit (same position for consistency) */
#define IPC_DIPCIE_DONE     (1u << 31) /* DONE bit in acknowledge registers */

#define ace_region(raddr)    info->region[(raddr) >> 2]

static const char *ipc_reg_name(hwaddr addr)
{
    switch (addr) {
    case IPC_DIPCTDR:
        return "DIPCTDR";
    case IPC_DIPCTDA:
        return "DIPCTDA";
    case IPC_DIPCTDD:
        return "DIPCTDD";
    case IPC_DIPCIDR:
        return "DIPCIDR";
    case IPC_DIPCIDA:
        return "DIPCIDA";
    case IPC_DIPCIDD:
        return "DIPCIDD";
    case IPC_DIPCCST:
        return "DIPCCST";
    case IPC_DIPCCTL8:
        return "DIPCCTL8";
    default:
        return "UNKNOWN";
    }
}

/* ACE3.x IPC (ACE 3.0 and later)
 * 
 * ACE3.x introduces enhanced IPC with:
 * - Separate Target/Initiator register pairs for bidirectional communication
 * - BUSY/DONE bit protocol for message handshaking
 * - Support for multiple host IPC instances (IPCHAC parameter)
 * - Sideband IPC for peer agents (IPCSAC parameter, up to 8 agents)
 * - Intra-DSP Communication (IDC) for multi-core coordination
 * - DxHIPCIE/DxSBIPCIE/DxIDCAIE registers for interrupt routing to specific cores
 * 
 * Register naming convention:
 * - DIPCTDR: DSP IPC Target Doorbell Request (host->DSP direction)
 * - DIPCTDA: DSP IPC Target Doorbell Acknowledge  
 * - DIPCTDD: DSP IPC Target Doorbell Data
 * - DIPCIDR: DSP IPC Initiator Doorbell Request (DSP->host direction)
 * - DIPCIDA: DSP IPC Initiator Doorbell Acknowledge
 * - DIPCIDD: DSP IPC Initiator Doorbell Data
 * 
 * BUSY/DONE Protocol:
 * Host->DSP: Host sets BUSY in xIPCIDR -> DSP clears BUSY in DIPCTDR ->
 *            DSP completes task -> DSP clears BUSY in DIPCTDA (sets DONE in xIPCIDA)
 * DSP->Host: DSP sets BUSY in DIPCIDR -> Host clears BUSY in xIPCTDA (sets DONE in DIPCIDA)
 */

static uint64_t ace_ipc_ace3_read(void *opaque, hwaddr addr,
        unsigned size)
{
    struct adsp_io_info *info = opaque;
    uint64_t val = ace_region(addr);

    ace_log( "IPC read: reg=%s addr=0x%lx size=%u val=0x%lx\n",
            ipc_reg_name(addr), (unsigned long)addr, size, (unsigned long)val);

    trace_adsp_dsp_shim_read(addr, val);

    return val;
}

static void ace_ipc_ace3_write(void *opaque, hwaddr addr,
        uint64_t val, unsigned size)
{
    struct adsp_io_info *info = opaque;

    ace_log( "IPC write: reg=%s addr=0x%lx size=%u val=0x%lx\n",
        ipc_reg_name(addr), (unsigned long)addr, size, (unsigned long)val);

    trace_adsp_dsp_shim_write(addr, val);

    /* special case registers */
    switch (addr) {
    case IPC_DIPCTDR:
        /* Target doorbell request - host to DSP.
         * W1C: DSP firmware clears BUSY by writing 1 to bit 31. */
        if (val & IPC_DIPCT_DSPLRST) {
            ace_region(addr) &= ~IPC_DIPCT_DSPLRST;
            /* De-assert the level-2 interrupt line now that BUSY is clear */
            adsp_set_lvl1_irq(info->adsp, IRQ_NUM_EXT_LEVEL2, 0);
        }
        break;

    case IPC_DIPCTDA:
        /* Target doorbell acknowledge - DSP completing host-initiated message
         * Writing 0 to BUSY bit causes xIPCIDA.DONE=1 and xIPCIDR.BUSY=0 */
        /* Preserve the completion word so the host side can read back the DSP's response. */
        ace_region(addr) = val;

        /* Check if DSP is completing a task (clearing BUSY sets DONE for host) */
        if (!(val & IPC_DIPCT_DSPLRST)) {
            trace_adsp_dsp_shim_event("irq: send DONE interrupt to host (task complete)", val);
            adsp_set_lvl1_irq(info->adsp, IRQ_IPC, 1);
        }
        break;

    case IPC_DIPCTDD:
        /* Target doorbell data - data payload from host to DSP */
        /* This register is just the host-to-DSP payload mailbox. */
        ace_region(addr) = val;
        break;

    case IPC_DIPCIDR:
        /* Initiator doorbell request - DSP to host
         * DSP sets BUSY bit to initiate message to host */
        /* Retain the DSP-owned request word exactly as FW programmed it. */
        ace_region(addr) = val;

        if (val & IPC_DIPCI_DSPRST) {
            trace_adsp_dsp_shim_event("irq: send BUSY interrupt to host (DSP->host msg)", val);
            adsp_set_lvl1_irq(info->adsp, IRQ_IPC, 1);

            /* Capture IPC4 reply for ace-ipc-rx before potential auto-ACK. */
            info->adsp->last_ipc4_reply = val & ~IPC_DIPCI_DSPRST;
            qemu_log("IPC_REPLY: header=0x%08x ext=0x%08x\n",
                     info->adsp->last_ipc4_reply, ace_region(IPC_DIPCIDD));

            /* Auto-ACK cavstool commands for standalone QEMU testing */
            uint32_t cmd = val & ~IPC_DIPCI_DSPRST;
            if (cmd <= 12) {
                uint32_t ext = ace_region(IPC_DIPCIDD);
                uint32_t channel = ext & 0xFF;

                /* Call HDA DMA emulation for cavstool commands */
                ace_hda_stream_emulate_cavstool(cmd, channel, ext);

                /* Clear BUSY */
                ace_region(IPC_DIPCIDR) &= ~IPC_DIPCI_DSPRST;
                /* Set DONE */
                ace_region(IPC_DIPCIDA) |= (1 << 31); /* INTEL_ADSP_IPC_DONE */
                /* Trigger interrupt to DSP */
                adsp_set_lvl1_irq(info->adsp, IRQ_IPC, 1);

                /* If it's VALIDATE, we need to send an IPC back */
                if (cmd == 10) { /* IPCCMD_HDA_VALIDATE */
                    ace_region(IPC_DIPCTDD) = 1;
                    ace_region(IPC_DIPCTDR) = 2 | IPC_DIPCT_DSPLRST; /* IPCCMD_RETURN_MSG = 2 */
                    adsp_set_lvl1_irq(info->adsp, IRQ_IPC, 1);
                }
            }
        }
        break;

    case IPC_DIPCIDA:
        /* Initiator doorbell acknowledge - host completing DSP-initiated message
         * Host writes to clear DONE bit after processing DSP message */
        /* DONE is W1C on the DSP-visible side, so preserve payload bits and clear only DONE. */
        ace_region(addr) = val & ~(0x1 << 31);
        if (val & IPC_DIPCIE_DONE) {
            ace_region(addr) &= ~IPC_DIPCIE_DONE;
        }
        break;

    case IPC_DIPCIDD:
        /* Initiator doorbell data - data payload from DSP to host */
        /* Preserve the outbound payload until the host side consumes it. */
        ace_region(addr) = val;
        break;

    case IPC_DIPCCST:
        /* IPC command/status - Sideband IPC control */
        /* Sideband control is stored as programmed so FW can observe its own state. */
        ace_region(addr) = val;
        break;

    case IPC_DIPCCTL8:
        /* IPC control - assume interrupts are not masked atm */
        /* Keep the control programming available for future side-effect modeling. */
        ace_region(addr) = val;
        break;

    default:
        break;
    }
}

const MemoryRegionOps ace_ipc_ace3_io_ops = {
    .read = ace_ipc_ace3_read,
    .write = ace_ipc_ace3_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

void adsp_monitor_ace_ipc_tx(Monitor *mon, const QDict *qdict)
{
    struct adsp_io_info *info = g_adsp_dev ? g_adsp_dev->ipc : NULL;
    uint32_t primary = qdict_get_int(qdict, "primary");
    int has_extension = qdict_haskey(qdict, "extension");
    uint32_t extension = has_extension ? qdict_get_int(qdict, "extension") : 0;
    
    const char *payload_str = qdict_get_try_str(qdict, "payload_bytes");
    int has_sram_addr = qdict_haskey(qdict, "sram_addr");
    uint64_t sram_addr = has_sram_addr ? qdict_get_int(qdict, "sram_addr") : 0;

    if (!info) {
        monitor_printf(mon, "IPC block not initialized.\n");
        return;
    }

    /* Both HFIPC (ACE30) and new IPC (ACE40) expose the DSP-facing host→DSP doorbell
     * at offset 0x0 (DIPCTDR equivalent) with BUSY at bit 31.  The HFIPC block also has
     * a separate host-side register bank at 0x200+, but writing there does NOT update
     * the DSP-facing slot (different indices in the same backing array).  The HMP command
     * simulates the host by writing directly into the DSP-facing slot so firmware can
     * read the message at the offset it actually polls. */
    bool is_hfipc = (info->ops == &ace30_hfipc_io_ops);
    uint32_t dr_reg   = IPC_DIPCTDR;   /* 0x0: DSP-facing doorbell for both HFIPC and new IPC */
    uint32_t dd_reg   = IPC_DIPCTDD;   /* 0x100: tdd — host→DSP extension word (struct intel_adsp_ipc) */
    uint32_t busy_bit = IPC_DIPCT_DSPLRST; /* bit 31 */
    (void)is_hfipc;

    if (ace_region(dr_reg) & busy_bit) {
        monitor_printf(mon, "Error: IPC doorbell is currently BUSY. DSP has not acked the previous message.\n");
        return;
    }

    uint32_t byte_count = 0;
    if (payload_str && payload_str[0] != '\0') {
        if (!has_sram_addr || sram_addr == 0) {
            /* Zephyr's mem_window driver programs DMWBA (base-address) registers
             * at 0x70200 + window*8.  Window 1 is the Host→DSP downlink inbox.
             * Read DMWBA for window 1 to find the SRAM inbox base address.
             * The value has MWE (bit 0) and RO (bit 1) flags in the low bits;
             * the actual 4 KB-aligned base address is in bits [31:12]. */
            uint32_t dmwba_win1 = 0;
            cpu_physical_memory_read(0x70208, &dmwba_win1, 4);  /* DMW base 0x70200 + win1*8 */
            if (dmwba_win1 & 1) {  /* MWE (Memory Window Enable) set? */
                sram_addr = dmwba_win1 & 0xFFFFF000u;
                monitor_printf(mon, "Auto-discovered SRAM Inbox from DMWBA[1]: 0x%" PRIx64 "\n", sram_addr);
            } else {
                /* Fallback: try DTFCXD64 host-window register (legacy path) */
                uint64_t win1 = 0;
                cpu_physical_memory_read(ADSP_ACE30_DSP_HOST_WIN_BASE(0) + DTFCXD64_OFFSET, &win1, 8);
                if (win1 != 0) {
                    sram_addr = win1;
                    monitor_printf(mon, "Auto-discovered SRAM Inbox from DTFCXD64: 0x%" PRIx64 "\n", sram_addr);
                } else {
                    monitor_printf(mon, "Warning: Payload bytes provided but no SRAM inbox window is configured.\n");
                }
            }
        }
        
        if (sram_addr != 0) {
            const char *p = payload_str;
            uint8_t buffer[4096];
            while (*p && byte_count < sizeof(buffer)) {
                unsigned int b = 0;
                if (sscanf(p, "%x", &b) == 1) {
                    buffer[byte_count++] = (uint8_t)b;
                }
                p = strchr(p, ':');
                if (p) p++;
                else break;
            }
            if (byte_count > 0) {
                cpu_physical_memory_write(sram_addr, buffer, byte_count);
                monitor_printf(mon, "Injected %u bytes to SRAM mailbox at 0x%" PRIx64 "\n", byte_count, sram_addr);
            }
        }
    }

    if (has_extension) {
        ace_region(dd_reg) = extension;
    }

    /* Write primary header with BUSY set directly into the IPC block's backing store,
     * then assert the interrupt. The DSP ISR reads the doorbell register via MMIO
     * (which reads from this same backing store) and will see BUSY=1. */
    primary |= busy_bit;
    ace_region(dr_reg) = primary;

    /* Route through the DINT controller so DINT register state is consistent,
     * then unconditionally assert the CPU's level-2 external interrupt line
     * (IRQ_NUM_EXT_LEVEL2 = 4). We bypass the DINT finalstatus gate because
     * firmware may not have unmasked the IPC bit in irq_inten at injection time.
     * adsp_set_lvl1_irq(adsp, IRQ_IPC=0) sets bit 0 of INTSET which is wrong;
     * the level-2 handler listens on bit 4. */
    ace_irq_set(g_adsp_dev, IRQ_IPC, 0);
    adsp_set_lvl1_irq(info->adsp, IRQ_NUM_EXT_LEVEL2, 1);

    /* Clear any stale reply from a previous transaction. */
    g_adsp_dev->last_ipc4_reply = 0;

    monitor_printf(mon, "Injected Host->DSP IPC via %s (Primary: 0x%08x, Extension: 0x%08x).\n",
                   is_hfipc ? "HFIPC_XTDR" : "IPC_DIPCTDR", primary, extension);
}

void adsp_monitor_ace_ipc_rx(Monitor *mon, const QDict *qdict)
{
    struct adsp_io_info *info = g_adsp_dev ? g_adsp_dev->ipc : NULL;
    if (!info) {
        monitor_printf(mon, "IPC_RX: error=block_not_initialized\n");
        return;
    }

    uint32_t tdr = ace_region(IPC_DIPCTDR);
    uint32_t idr = ace_region(IPC_DIPCIDR);
    uint32_t ida = ace_region(IPC_DIPCIDA);
    int busy = !!(tdr & IPC_DIPCT_DSPLRST);

    /* last_ipc4_reply is set when firmware writes idr|BUSY and cleared when
     * the host sends the next IPC via ace-ipc-tx.  It persists even after the
     * firmware ISR clears ida.DONE, so the monitor can always read it. */
    uint32_t reply = g_adsp_dev->last_ipc4_reply;
    int reply_pending = (reply != 0);

    monitor_printf(mon,
        "IPC_RX: busy=%d reply_pending=%d reply=0x%08x tdr=0x%08x idr=0x%08x ida=0x%08x\n",
        busy, reply_pending, reply, tdr, idr, ida);
}
