/* ACE host-facing HFIPC block virtualization
 * IP Region: Host<->DSP mailbox, doorbell, and payload arrays for IPC transport.
 *
 * Copyright (C) 2026 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/audio/adsp-dev.h"
#include "hw/adsp/shim.h"
#include "hw/adsp/ace.h"
#include "ace-internal.h"
#include "monitor/monitor.h"
#include "qobject/qdict.h"

extern struct adsp_dev *g_adsp_dev;

#define ace_region(raddr)    info->region[(raddr) >> 2]

#define HFIPC_XTDR           0x200
#define HFIPC_XTDA           0x204
#define HFIPC_XIDR           0x210
#define HFIPC_XIDA           0x214
#define HFIPC_XCST           0x220
#define HFIPC_XCSR           0x224
#define HFIPC_XCTL           0x228
#define HFIPC_XCAP           0x22c
#define HFIPC_XTDDY_BASE     0x300
#define HFIPC_XIDDY_BASE     0x380
#define HFIPC_DW_ARRAY_COUNT 32

#define HFIPC_BUSY           (1u << 31)
#define HFIPC_DONE           (1u << 31)
#define HFIPC_MSG_MASK       0x7fffffffu
#define HFIPC_CTL_IPCTBIE    (1u << 0)
#define HFIPC_CTL_IPCIDIE    (1u << 1)
#define HFIPC_CTL_IPCCSRIE   (1u << 2)
#define HFIPC_CTL_MASK       (HFIPC_CTL_IPCTBIE | HFIPC_CTL_IPCIDIE | HFIPC_CTL_IPCCSRIE)
#define HFIPC_CAP_PDC_MASK   0x1f
#define HFIPC_CAP_PDC_VAL    0x0

static inline bool hfipc_is_xtddy(hwaddr reg)
{
    return reg >= HFIPC_XTDDY_BASE &&
           reg < (HFIPC_XTDDY_BASE + HFIPC_DW_ARRAY_COUNT * 4);
}

static inline bool hfipc_is_xiddy(hwaddr reg)
{
    return reg >= HFIPC_XIDDY_BASE &&
           reg < (HFIPC_XIDDY_BASE + HFIPC_DW_ARRAY_COUNT * 4);
}

static inline void hfipc_send_irq(struct adsp_io_info *info)
{
    adsp_set_lvl1_irq(info->adsp, IRQ_NUM_EXT_LEVEL2, 1);
}

void ace30_hfipc_init(struct adsp_dev *adsp, MemoryRegion *parent,
                        struct adsp_io_info *info)
{
    int i;

    /* Reset all host-facing IPC mailboxes to the architectural idle state. */
    ace_region(HFIPC_XTDR) = 0x0;
    ace_region(HFIPC_XTDA) = 0x0;
    ace_region(HFIPC_XIDR) = 0x0;
    ace_region(HFIPC_XIDA) = 0x0;
    ace_region(HFIPC_XCST) = 0x0;
    ace_region(HFIPC_XCSR) = 0x0;
    ace_region(HFIPC_XCTL) = 0x0;
    /* XCAP reports the implemented payload-capability fields defined by the ACE docs. */
    ace_region(HFIPC_XCAP) = HFIPC_CAP_PDC_VAL & HFIPC_CAP_PDC_MASK;

    for (i = 0; i < HFIPC_DW_ARRAY_COUNT; i++) {
        /* Clear the inbound payload array that XTDDY reads indirectly expose to FW. */
        ace_region(HFIPC_XIDDY_BASE + (i * 4)) = 0x0;
    }

    ace_log("HFIPC: Initialized at 0x73000 (instance 0)\n");
    adsp->ipc = info;
}

static uint64_t ace30_hfipc_read(void *opaque, hwaddr addr, unsigned size)
{
    struct adsp_io_info *info = opaque;
    hwaddr aligned_addr = addr & ~3;
    uint32_t byte_offset = addr & 3;
    uint32_t word;
    uint64_t val;

    if (aligned_addr == HFIPC_XCAP) {
        word = HFIPC_CAP_PDC_VAL & HFIPC_CAP_PDC_MASK;
    } else if (hfipc_is_xtddy(aligned_addr)) {
        /* XTDDY: host-facing read of the host→DSP payload array.
         * Payload is stored at DSP-facing tdd area (0x100 + idx*4) so
         * firmware can read it via regs->tdd at the same offset. */
        hwaddr idx = (aligned_addr - HFIPC_XTDDY_BASE) >> 2;
        word = ace_region(0x100 + (idx * 4));
    } else {
        word = ace_region(aligned_addr);
    }

    if (!ace_extract_subword_read(word, size, byte_offset, &val)) {
        ace_log( "HFIPC read: unsupported size %u\n", size);
        val = 0;
    }

    ace_log(
            "HFIPC read: reg=0x%lx addr=0x%lx size=%u val=0x%lx (byte_offset=%u)\n",
            (unsigned long)aligned_addr, (unsigned long)addr, size,
            (unsigned long)val, byte_offset);
    return val;
}

static void ace30_hfipc_write(void *opaque, hwaddr addr, uint64_t val,
                                unsigned size)
{
    struct adsp_io_info *info = opaque;
    hwaddr aligned_addr = addr & ~3;
    uint32_t byte_offset = addr & 3;
    uint32_t old_word = ace_region(aligned_addr);
    uint32_t new_word;
    uint32_t ctl;

    if (!ace_merge_subword_write(old_word, val, size, byte_offset,
                                 &new_word)) {
        ace_log( "HFIPC write: unsupported size %u\n", size);
        return;
    }

    ace_log(
            "HFIPC write: reg=0x%lx addr=0x%lx size=%u val=0x%lx (byte_offset=%u)\n",
            (unsigned long)aligned_addr, (unsigned long)addr, size,
            (unsigned long)val, byte_offset);

    switch (aligned_addr) {
    case 0x0:
        /* DSP-facing target doorbell (DIPCTDR equivalent) — W1C.
         * Firmware clears BUSY by writing 1 to bit 31; this de-asserts the interrupt.
         * Use ace_irq_clear so DINT rawstatus is also cleared (not just INTSET). */
        if (new_word & HFIPC_BUSY) {
            ace_region(0x0) &= ~HFIPC_BUSY;
            ace_irq_clear(info->adsp, IRQ_IPC, 0);
        } else {
            ace_region(0x0) = new_word;
        }
        break;
    case 0x04:
        /* DSP-facing target doorbell ack (tda) — firmware writes 0 to ack a host→DSP IPC.
         * Store the payload; no DSP-side interrupt needed in the QEMU model. */
        ace_region(0x04) = new_word & HFIPC_MSG_MASK;
        break;
    case 0x10:
        /* DSP-facing initiator doorbell (idr) — firmware writes msg|BUSY to send DSP→host.
         * Simulate an immediate host ACK: clear BUSY in idr, set ida.DONE, and fire the
         * IPC interrupt so the firmware ISR can clear tx_ack_pending via the ida W1C path. */
        ace_region(0x10) = new_word & HFIPC_MSG_MASK;
        if (new_word & HFIPC_BUSY) {
            /* Capture the reply before auto-ACK so ace-ipc-rx can report it. */
            info->adsp->last_ipc4_reply = new_word;
            qemu_log("IPC_REPLY: header=0x%08x ext=0x%08x\n",
                     info->adsp->last_ipc4_reply, ace_region(0x180));
            ace_region(0x14) = HFIPC_DONE;
            ace_irq_set(info->adsp, IRQ_IPC, 0);
            hfipc_send_irq(info);
        }
        break;
    case 0x14:
        /* DSP-facing initiator done-ack (DIPCIDA equivalent) — W1C.
         * Firmware (and init code) writes 1 to bit 31 to clear DONE.
         * Clearing DONE here also de-asserts the IPC interrupt source. */
        if (new_word & HFIPC_DONE) {
            ace_region(0x14) &= ~HFIPC_DONE;
            ace_irq_clear(info->adsp, IRQ_IPC, 0);
        } else {
            ace_region(0x14) = new_word;
        }
        break;
    case HFIPC_XTDR:
        /* XTDR carries the host-to-DSP request header and BUSY ownership bit. */
        ace_region(HFIPC_XTDR) = new_word;
        ctl = ace_region(HFIPC_XCTL);
        if ((new_word & HFIPC_BUSY) && (ctl & HFIPC_CTL_IPCTBIE)) {
            hfipc_send_irq(info);
        }
        break;
    case HFIPC_XTDA:
        /* XTDA is the DSP's response payload for a host-initiated transaction. */
        ace_region(HFIPC_XTDA) = new_word & HFIPC_MSG_MASK;
        if (!(new_word & HFIPC_BUSY)) {
            uint32_t xtdr = ace_region(HFIPC_XTDR);
            uint32_t xida = ace_region(HFIPC_XIDA);
            uint32_t resp = new_word & HFIPC_MSG_MASK;

            /* Completing the transaction clears BUSY on XTDR and raises DONE on XIDA. */
            ace_region(HFIPC_XTDR) = xtdr & ~HFIPC_BUSY;
            ace_region(HFIPC_XIDA) = (xida | HFIPC_DONE);
            /* Preserve only the response payload bits alongside the synthesized DONE flag. */
            ace_region(HFIPC_XIDA) = (ace_region(HFIPC_XIDA) & HFIPC_DONE) | resp;

            ctl = ace_region(HFIPC_XCTL);
            if (ctl & HFIPC_CTL_IPCIDIE) {
                hfipc_send_irq(info);
            }
        }
        break;
    case HFIPC_XIDR: {
        uint32_t busy = old_word & HFIPC_BUSY;
        uint32_t msg = new_word & HFIPC_MSG_MASK;
        if (new_word & HFIPC_BUSY) {
            busy = HFIPC_BUSY;
        }
        /* XIDR is the DSP-owned initiator header; BUSY transitions drive host notification. */
        ace_region(HFIPC_XIDR) = busy | msg;

        if (busy) {
            uint32_t xtdr = ace_region(HFIPC_XTDR) & HFIPC_BUSY;
            /* Mirror the payload into the peer-visible target register bank. */
            ace_region(HFIPC_XTDR) = xtdr | msg;
        }

        ctl = ace_region(HFIPC_XCTL);
        if (busy && (ctl & HFIPC_CTL_IPCTBIE)) {
            hfipc_send_irq(info);
        }
        break;
    }
    case HFIPC_XIDA:
        /* DONE is W1C from the DSP side, so only acknowledged DONE bits are cleared. */
        ace_region(HFIPC_XIDA) = old_word & ~((uint32_t)new_word & HFIPC_DONE);
        break;
    case HFIPC_XCST:
        /* XCST is sticky status: writes set bits rather than replace the register. */
        ace_region(HFIPC_XCST) = old_word | new_word;
        break;
    case HFIPC_XCSR:
        /* XCSR uses clear-on-write-one semantics for sticky status bits. */
        ace_region(HFIPC_XCSR) = old_word & ~new_word;
        break;
    case HFIPC_XCTL:
        /* Only architecturally defined interrupt-enable bits are writable. */
        ace_region(HFIPC_XCTL) = new_word & HFIPC_CTL_MASK;
        break;
    case HFIPC_XCAP:
        break;
    default:
        if (hfipc_is_xtddy(aligned_addr)) {
            /* XTDDY: host writes host→DSP payload; store at DSP-facing tdd
             * offset (0x100 + idx*4) so firmware reads it via regs->tdd. */
            hwaddr idx = (aligned_addr - HFIPC_XTDDY_BASE) >> 2;
            ace_region(0x100 + (idx * 4)) = new_word;
        } else if (hfipc_is_xiddy(aligned_addr)) {
            /* XIDDY stores the DSP-written outbound payload dwords. */
            ace_region(aligned_addr) = new_word;
        } else {
            /* Other implemented scratch/control registers are retained without extra side effects. */
            ace_region(aligned_addr) = new_word;
        }
        break;
    }
}

const MemoryRegionOps ace30_hfipc_io_ops = {
    .read = ace30_hfipc_read,
    .write = ace30_hfipc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

void adsp_monitor_ace_ipc(Monitor *mon, const QDict *qdict)
{
    struct adsp_io_info *info = g_adsp_dev ? g_adsp_dev->shim : NULL;
    int i;

    if (!info) {
        monitor_printf(mon, "HFIPC block not initialized or unavailable.\n");
        return;
    }

    monitor_printf(mon, "Intel ADSP Host-Facing IPC (HFIPC) State:\n");
    monitor_printf(mon, "----------------------------------------\n");

    monitor_printf(mon, "  Host to DSP (XTDR/XTDA):\n");
    monitor_printf(mon, "    XTDR (Command) : 0x%08x [BUSY=%d]\n", 
                   ace_region(HFIPC_XTDR), !!(ace_region(HFIPC_XTDR) & HFIPC_BUSY));
    monitor_printf(mon, "    XTDA (Response): 0x%08x [BUSY=%d]\n", 
                   ace_region(HFIPC_XTDA), !!(ace_region(HFIPC_XTDA) & HFIPC_BUSY));
    monitor_printf(mon, "\n");

    monitor_printf(mon, "  DSP to Host (XIDR/XIDA):\n");
    monitor_printf(mon, "    XIDR (Command) : 0x%08x [BUSY=%d]\n", 
                   ace_region(HFIPC_XIDR), !!(ace_region(HFIPC_XIDR) & HFIPC_BUSY));
    monitor_printf(mon, "    XIDA (Response): 0x%08x [DONE=%d]\n", 
                   ace_region(HFIPC_XIDA), !!(ace_region(HFIPC_XIDA) & HFIPC_DONE));
    monitor_printf(mon, "\n");

    monitor_printf(mon, "  Interrupt Controls (XCTL/XCST/XCSR):\n");
    monitor_printf(mon, "    XCTL (Enables) : 0x%08x (IPCTBIE=%d IPCIDIE=%d IPCCSRIE=%d)\n",
                   ace_region(HFIPC_XCTL),
                   !!(ace_region(HFIPC_XCTL) & HFIPC_CTL_IPCTBIE),
                   !!(ace_region(HFIPC_XCTL) & HFIPC_CTL_IPCIDIE),
                   !!(ace_region(HFIPC_XCTL) & HFIPC_CTL_IPCCSRIE));
    monitor_printf(mon, "    XCST (Status)  : 0x%08x\n", ace_region(HFIPC_XCST));
    monitor_printf(mon, "    XCSR (Flags)   : 0x%08x\n", ace_region(HFIPC_XCSR));
    monitor_printf(mon, "\n");

    monitor_printf(mon, "  Outbound Payload (XIDDY DSP -> Host):\n");
    for (i = 0; i < HFIPC_DW_ARRAY_COUNT; i += 4) {
        monitor_printf(mon, "    [%02d]: 0x%08x 0x%08x 0x%08x 0x%08x\n", 
            i, 
            ace_region(HFIPC_XIDDY_BASE + ((i+0) * 4)),
            ace_region(HFIPC_XIDDY_BASE + ((i+1) * 4)),
            ace_region(HFIPC_XIDDY_BASE + ((i+2) * 4)),
            ace_region(HFIPC_XIDDY_BASE + ((i+3) * 4)));
    }
}


