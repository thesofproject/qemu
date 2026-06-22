/* ACE IDC (Inter-DSP Communication) virtualization
 * IP Region: Inter-core IDC message/doorbell registers between DSP instances.
 *
 * Copyright (C) 2024 Intel Corporation
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
 *
 * This provides register-level virtualization for ACE3.x IDC:
 * - Inter-DSP Communication (IDC) for message passing between cores
 *
 * Reference: ACE3.x IP HAS Section 8.5.1 (IDC)
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/audio/adsp-dev.h"
#include "hw/adsp/ace.h"
#include "ace-internal.h"

/* Moved from ace.h */
#define IDC_DAIPCIDR           0x00   /* Agent A IPC Initiator Doorbell Request */
#define IDC_DAIPCIDA           0x04   /* Agent A IPC Initiator Doorbell Ack (DONE status) */
#define IDC_DAIPCIDD           0x08   /* Agent A IPC Initiator Doorbell Data */
#define IDC_DAIPCCTL           0x10   /* Agent A IPC Control */
#define IDC_IPC_BUSY           (1 << 31)  /* BUSY bit - message pending */
#define IDC_IPC_MSG_MASK       0x7FFFFFFF /* Message payload (bits 30:0) */
#define IDC_IPC_DONE           (1 << 31)  /* DONE bit - task completed */
#define IDC_IPCTBIE            (1 << 0)   /* Target BUSY Interrupt Enable */
#define IDC_IPCIDIE            (1 << 1)   /* Initiator DONE Interrupt Enable */

#define ace_region(raddr)    info->region[(raddr) >> 2]

#define IDC_REG_OFFSET_MASK 0x1F

/* IDC state tracking - per core, per direction */
struct idc_state {
    uint32_t initiator_msg;     /* DAIPCIDR - message being sent */
    uint32_t initiator_data;    /* DAIPCIDD - extended data */
    uint32_t initiator_ctl;     /* DAIPCCTL - initiator control */
    uint32_t target_msg;        /* DBIPCTDR - message received */
    uint32_t target_data;       /* DBIPCTDD - extended data */
    uint32_t target_ctl;        /* DBIPCCTL - target control */
};

void ace30_idc_init(struct adsp_dev *adsp, MemoryRegion *parent,
        struct adsp_io_info *info)
{
    /* Start with both halves of the doorbell protocol idle after reset. */
    ace_region(IDC_DAIPCIDR) = 0x0;
    ace_region(IDC_DAIPCIDA) = 0x0;
    ace_region(IDC_DAIPCIDD) = 0x0;
    ace_region(IDC_DAIPCCTL) = 0x0;
    ace_log("IDC: Initialized doorbell registers\n");
}

/* IDC register read handler
 * IDC provides doorbell-based message passing between DSP cores.
 * Each core pair has initiator (Agent A) and target (Agent B) registers.
 */
uint64_t ace_idc_read(void *opaque, hwaddr addr, unsigned size)
{
    struct adsp_io_info *info = opaque;
    uint32_t offset = addr & IDC_REG_OFFSET_MASK;
    uint32_t value = 0;

    /* Determine which core's IDC registers based on base address */
    int core_id = 0;  /* Simplified - would need proper core mapping */
    (void)core_id;  /* Unused in simplified version */
    (void)size;

    switch (offset) {
    case IDC_DAIPCIDR:  /* DAIPCIDR (Agent A) or DBIPCTDR (Agent B) - Doorbell Request */
        value = ace_region(addr);
        ace_log( "IDC: Core %d read doorbell request=0x%08x (msg=0x%lx %s)\n",
                core_id, value, (unsigned long)(value & IDC_IPC_MSG_MASK),
                (value & IDC_IPC_BUSY) ? "BUSY" : "idle");
        break;

    case IDC_DAIPCIDA:  /* DAIPCIDA (Agent A) or DBIPCTDA (Agent B) - Doorbell Ack */
        value = ace_region(addr);
        ace_log( "IDC: Core %d read doorbell ack=0x%08x (%s)\n",
                core_id, value,
                (value & IDC_IPC_DONE) ? "DONE" : "pending");
        break;

    case IDC_DAIPCIDD:  /* DAIPCIDD (Agent A) or DBIPCTDD (Agent B) - Doorbell Data */
        value = ace_region(addr);
        ace_log( "IDC: Core %d read doorbell data=0x%08x\n",
                core_id, value);
        break;

    case IDC_DAIPCCTL:  /* DAIPCCTL (Agent A) or DBIPCCTL (Agent B) - Control */
        value = ace_region(addr);
        ace_log( "IDC: Core %d read control=0x%08x [TBIE=%d IDIE=%d]\n",
                core_id, value,
                !!(value & IDC_IPCTBIE), !!(value & IDC_IPCIDIE));
        break;

    default:
        value = ace_region(addr);
        ace_log( "IDC: Core %d read unknown offset 0x%02x = 0x%08x\n",
                core_id, offset, value);
        break;
    }

    return value;
}

/* IDC register write handler
 * Handles message sending (BUSY bit), acknowledgment (clearing BUSY),
 * and interrupt enable configuration.
 */
void ace_idc_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    struct adsp_io_info *info = opaque;
    uint32_t offset = addr & IDC_REG_OFFSET_MASK;
    uint32_t old_value = ace_region(addr);

    int core_id = 0;  /* Simplified - would need proper core mapping */
    (void)core_id;  /* Unused in simplified version */
    (void)size;

    /* Default IDC behavior is to retain the programmer-visible word exactly as written. */
    ace_region(addr) = val;

    switch (offset) {
    case IDC_DAIPCIDR:  /* DAIPCIDR (Agent A) or DBIPCTDR (Agent B) - Doorbell Request */
        if ((val & IDC_IPC_BUSY) && !(old_value & IDC_IPC_BUSY)) {
            /* Setting BUSY - sending message */
            ace_log( "IDC: Core %d SEND msg=0x%08lx (set BUSY)\n",
                    core_id, (unsigned long)(val & IDC_IPC_MSG_MASK));
        } else if (!(val & IDC_IPC_BUSY) && (old_value & IDC_IPC_BUSY)) {
            /* Clearing BUSY - acknowledgment */
            ace_log( "IDC: Core %d ACK message (clear BUSY, sets DONE)\n", core_id);
        }
        break;

    case IDC_DAIPCIDA:  /* DAIPCIDA (Agent A) or DBIPCTDA (Agent B) - Doorbell Ack */
        if (val & IDC_IPC_DONE) {
            /* W1C - clear DONE bit */
            /* DONE is the consumed-completion latch, so only the acknowledged bit is dropped. */
            ace_region(addr) = old_value & ~IDC_IPC_DONE;
            ace_log( "IDC: Core %d CLEAR DONE bit\n", core_id);
        }
        break;

    case IDC_DAIPCIDD:  /* DAIPCIDD (Agent A) or DBIPCTDD (Agent B) - Doorbell Data */
        ace_log( "IDC: Core %d write doorbell data=0x%08x\n",
                core_id, (uint32_t)val);
        break;

    case IDC_DAIPCCTL:  /* DAIPCCTL (Agent A) or DBIPCCTL (Agent B) - Control */
        ace_log( "IDC: Core %d write control=0x%08x [TBIE=%d IDIE=%d]\n",
                core_id, (uint32_t)val,
                !!(val & IDC_IPCTBIE), !!(val & IDC_IPCIDIE));
        break;

    default:
        ace_log( "IDC: Core %d write unknown offset 0x%02x = 0x%08x\n",
                core_id, offset, (uint32_t)val);
        break;
    }
}

/* Memory region operations for IDC */
const MemoryRegionOps ace_idc_ops = {
    .read = ace_idc_read,
    .write = ace_idc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};
