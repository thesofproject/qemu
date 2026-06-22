/* ACE DINT register virtualization
 * IP Region: DSPx DINT per-core interrupt steering/status window.
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
#include "hw/adsp/ace.h"
#include "hw/adsp/shim.h"
#include "ace-internal.h"
#include "monitor/monitor.h"
#include "monitor/hmp-target.h"

/* Moved from ace.h */
/* IRQ_NUM_EXT_LEVEL* now defined in ace-internal.h */
#define IRQ_HPGPDMA     1
#define IRQ_LPGPDMA0    3
#define IRQ_LPGPDMA1    4
#define IRQ_HPGPDMA0    5
#define IRQ_L2ME        8
#define IRQ_DTS         9
#define IRQ_IDC         10
#define IRQ_DSPGCL      11
#define IRQ_DSPGHOS     12
#define IRQ_DSPGHIS     13
#define IRQ_DSPGLOS     14
#define IRQ_DSPGLIS     15
#define IRQ_DMIC0       16
#define IRQ_SNDW        23

#define ace_region(raddr)    info->region[(raddr) >> 2]


/* from zephyr */
struct dw_ictl_registers {
	uint32_t irq_inten_l;		/* offset 00 */
	uint32_t irq_inten_h;		/* offset 04 */
	uint32_t irq_intmask_l;		/* offset 08 */
	uint32_t irq_intmask_h;		/* offset 0C */
	uint32_t irq_intforce_l;		/* offset 10 */
	uint32_t irq_intforce_h;		/* offset 14 */
	uint32_t irq_rawstatus_l;		/* offset 18 */
	uint32_t irq_rawstatus_h;		/* offset 1c */
	uint32_t irq_status_l;		/* offset 20 */
	uint32_t irq_status_h;		/* offset 24 */
	uint32_t irq_maskstatus_l;		/* offset 28 */
	uint32_t irq_maskstatus_h;		/* offset 2c */
	uint32_t irq_finalstatus_l;	/* offset 30 */
	uint32_t irq_finalstatus_h;	/* offset 34 */
	uint32_t irq_vector;		/* offset 38 */
	uint32_t Reserved1;		/* offset 3c */
	uint32_t irq_vector_0;		/* offset 40 */
	uint32_t Reserved2;		/* offset 44 */
	uint32_t irq_vector_1;		/* offset 48 */
	uint32_t Reserved3;		/* offset 4c */
	uint32_t irq_vector_2;		/* offset 50 */
	uint32_t Reserved4;		/* offset 54 */
	uint32_t irq_vector_3;		/* offset 58 */
	uint32_t Reserved5;		/* offset 5c */
	uint32_t irq_vector_4;		/* offset 60 */
	uint32_t Reserved6;		/* offset 64 */
	uint32_t irq_vector_5;		/* offset 68 */
	uint32_t Reserved7;		/* offset 6c */
	uint32_t irq_vector_6;		/* offset 70 */
	uint32_t Reserved8;		/* offset 74 */
	uint32_t irq_vector_7;		/* offset 78 */
	uint32_t Reserved9;		/* offset 7c */
	uint32_t irq_vector_8;		/* offset 80 */
	uint32_t Reserved10;		/* offset 84 */
	uint32_t irq_vector_9;		/* offset 88 */
	uint32_t Reserved11;		/* offset 8c */
	uint32_t irq_vector_10;		/* offset 90 */
	uint32_t Reserved12;		/* offset 94 */
	uint32_t irq_vector_11;		/* offset 98 */
	uint32_t Reserved13;		/* offset 9c */
	uint32_t irq_vector_12;		/* offset a0 */
	uint32_t Reserved14;		/* offset a4 */
	uint32_t irq_vector_13;		/* offset a8 */
	uint32_t Reserved15;		/* offset ac */
	uint32_t irq_vector_14;		/* offset b0 */
	uint32_t Reserved16;		/* offset b4 */
	uint32_t irq_vector_15;		/* offset b8 */
	uint32_t Reserved17;		/* offset bc */
	uint32_t fiq_inten;		/* offset c0 */
	uint32_t fiq_intmask;		/* offset c4 */
	uint32_t fiq_intforce;		/* offset c8 */
	uint32_t fiq_rawstatus;		/* offset cc */
	uint32_t fiq_status;		/* offset d0 */
	uint32_t fiq_finalstatus;		/* offset d4 */
	uint32_t irq_plevel;		/* offset d8 */
	uint32_t Reserved18;		/* offset dc */
	uint32_t APB_ICTL_COMP_VERSION;	/* offset e0 */
	uint32_t Reserved19[199];
};
/* end of zephyr */

static uint64_t ace3_irq_counts[32] = {0};

static const char *ace3_irq_names[32] = {
    [IRQ_IPC] = "IPC",
    [IRQ_HPGPDMA] = "HPGPDMA",
    [IRQ_LPGPDMA] = "LPGPDMA",
    [IRQ_LPGPDMA0] = "LPGPDMA0",
    [IRQ_LPGPDMA1] = "LPGPDMA1",
    [IRQ_HPGPDMA0] = "HPGPDMA0",
    [IRQ_DWCT0] = "DWCT0",
    [IRQ_DWCT1] = "DWCT1",
    [IRQ_L2ME] = "L2ME",
    [IRQ_DTS] = "DTS",
    [IRQ_IDC] = "IDC",
    [IRQ_DSPGCL] = "DSPGCL",
    [IRQ_DSPGHOS] = "DSPGHOS",
    [IRQ_DSPGHIS] = "DSPGHIS",
    [IRQ_DSPGLOS] = "DSPGLOS",
    [IRQ_DSPGLIS] = "DSPGLIS",
    [IRQ_DMIC0] = "DMIC0",
    [IRQ_SSP0] = "SSP0",
    [IRQ_SSP1] = "SSP1",
    [IRQ_SSP2] = "SSP2",
    [IRQ_SSP3] = "SSP3",
    [IRQ_SSP4] = "SSP4",
    [IRQ_SSP5] = "SSP5",
    [IRQ_SNDW] = "SNDW",
};

/* IRQ descriptor structure */
struct ace_irq_desc {
    int id;          /* IRQ ID */
    int level;       /* interrupt level */
    uint32_t mask;   /* IRQ mask */
    int shift;       /* bit shift for this IRQ */
};

/* IRQ map structure */
struct ace_irq_map {
    int level;       /* interrupt level */
    int irq;         /* QEMU IRQ number */
};

/* ACE3.x - Enhanced interrupt architecture
 * 
 * ACE3.x uses Synopsys DesignWare interrupt controller with:
 * - Separate FIQ (high priority) and IRQ (low priority) paths
 * - Per-core interrupt controllers (up to 5 cores)
 * - Dynamic interrupt routing to any core via DxHIPCIE/DxSBIPCIE/DxIDCAIE
 * - Enhanced interrupt sources including privacy mic, ACPI routing
 * 
 * Interrupt Priority Mapping:
 * Level 2: Low priority general interrupts
 * Level 3: DMA-related interrupts  
 * Level 4: DMA-related interrupts
 * Level 5: Audio link and peripheral interrupts
 * High Priority (FIQ): Timers, time stamping, critical events
 * 
 * Note: Actual interrupt routing is determined at runtime by
 * programming the interrupt controller registers and routing registers.
 */
static const struct ace_irq_desc ace_ace3_irqs[] = {
    /* Level 2 - Core interrupts */
    {IRQ_HPGPDMA, 2, 0xff000000, 24},
    {IRQ_DWCT1, 2, 0x00000800}, // bit 11
    {IRQ_DWCT0, 2, 0x00000400}, // bit 10

    {IRQ_L2ME, 2, 0x00200000},
    {IRQ_DTS, 2, 0x00100000},
    {IRQ_IDC, 2, 0x00000080},  /* IDC routed via DxIDCAIE */
    {IRQ_IPC, 2, 0x00000040},  /* IPC routed via DxHIPCIE/DxSBIPCIE */

    /* Level 3 - High performance DMA */
    {IRQ_DSPGCL, 3, 0x80000000},
    {IRQ_DSPGHOS, 3, 0x7fff0000, 16},
    {IRQ_HPGPDMA0, 3, 0x00008000},
    {IRQ_DSPGHIS, 3, 0x00007fff, 0},

    /* Level 4 - Low power DMA */
    {IRQ_LPGPDMA1, 4, 0x80000000},
    {IRQ_DSPGLOS, 4, 0x7fff0000, 16},
    {IRQ_LPGPDMA0, 4, 0x00008000},
    {IRQ_DSPGLIS, 4, 0x00007fff, 0},

    /* Level 5 - Audio links and peripherals */
    {IRQ_LPGPDMA, 5, 0x00010000},
    /* DWCT removed from Level 5 to prevent NMI Double Exceptions on zephyr */
    {IRQ_SNDW, 5, 0x00000800},  /* SoundWire with privacy support */
    {IRQ_DMIC0, 5, 0x00000080}, /* DMIC with privacy support */
    {IRQ_SSP5, 5, 0x00000020},
    {IRQ_SSP4, 5, 0x00000010},
    {IRQ_SSP3, 5, 0x00000008},
    {IRQ_SSP2, 5, 0x00000004},
    {IRQ_SSP1, 5, 0x00000002},
    {IRQ_SSP0, 5, 0x00000001},
};

static const struct ace_irq_map irq_map[] = {
    {2, IRQ_NUM_EXT_LEVEL2},
    {3, IRQ_NUM_EXT_LEVEL3},
    {4, IRQ_NUM_EXT_LEVEL4},
    {5, IRQ_NUM_EXT_LEVEL5},
};

static void ace_do_set_irq(struct adsp_dev *adsp,
    const struct ace_irq_desc *irq_desc, uint32_t mask)
{
	struct adsp_io_info *info = adsp->dint[0];
    uint64_t time_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - info->adsp->timer[0].start;
    uint64_t now_ticks = (time_ns * info->adsp->timer[0].clk_kHz) / (ACE_TIMER_MULTIPLIER * 1000000);

    /* Use Zephyr structure inject logic */
    ace30_dint_inject_irq(info->adsp, irq_desc->id, true);

    uint32_t irq_id = irq_desc->id;
    uint32_t finalstatus = (irq_id < 32) ? ace_region(offsetof(struct dw_ictl_registers, irq_finalstatus_l)) : 
                                           ace_region(offsetof(struct dw_ictl_registers, irq_finalstatus_h));

    ace_log("IRQ: ASSERT irq=%d (level %d) -> finalstatus=0x%08x (now=0x%016llx)\n",
             irq_desc->id, irq_desc->level, finalstatus, (unsigned long long)now_ticks);

    if (irq_desc->id >= 0 && irq_desc->id < 32) {
        ace3_irq_counts[irq_desc->id]++;
    }

    if (finalstatus & (1U << (irq_id % 32))) {
        adsp_set_lvl1_irq(info->adsp, irq_map[irq_desc->level - 2].irq, 1);
    }
}

/* mask values copied as is */
static void ace_do_clear_irq(struct adsp_dev *adsp,
    const struct ace_irq_desc *irq_desc, uint32_t mask)
{
	struct adsp_io_info *info = adsp->dint[0];
    uint64_t time_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - info->adsp->timer[0].start;
    uint64_t now_ticks = (time_ns * info->adsp->timer[0].clk_kHz) / (ACE_TIMER_MULTIPLIER * 1000000);

    /* Use Zephyr structure clear logic */
    ace30_dint_inject_irq(info->adsp, irq_desc->id, false);

    uint32_t irq_id = irq_desc->id;
    uint32_t finalstatus = (irq_id < 32) ? ace_region(offsetof(struct dw_ictl_registers, irq_finalstatus_l)) : 
                                           ace_region(offsetof(struct dw_ictl_registers, irq_finalstatus_h));

    ace_log("IRQ: CLEAR irq=%d (level %d) -> finalstatus=0x%08x (now=0x%016llx)\n",
             irq_desc->id, irq_desc->level, finalstatus, (unsigned long long)now_ticks);

    if (finalstatus == 0) {
        adsp_set_lvl1_irq(info->adsp, irq_map[irq_desc->level - 2].irq, 0);
    }
}

/* ACE3.x interrupt operations
 * 
 * ACE3.x maintains compatibility with CAVS 1.8 interrupt structure
 * while adding support for:
 * - Synopsys DesignWare interrupt controller integration
 * - Dynamic interrupt routing (DxHIPCIE, DxSBIPCIE, DxIDCAIE)
 * - Per-core interrupt assignment
 * - FIQ/IRQ priority separation
 */
static void ace_irq_ace3_set(struct adsp_dev *adsp, int irq, uint32_t mask)
{
    int i;

    for (i = 0; i < (int)(sizeof(ace_ace3_irqs) / sizeof(ace_ace3_irqs[0])); i++) {
        if (irq == ace_ace3_irqs[i].id)
            ace_do_set_irq(adsp, &ace_ace3_irqs[i], mask);
    }
}

static void ace_irq_ace3_clear(struct adsp_dev *adsp, int irq, uint32_t mask)
{
    int i;

    for (i = 0; i < (int)(sizeof(ace_ace3_irqs) / sizeof(ace_ace3_irqs[0])); i++) {
        if (irq == ace_ace3_irqs[i].id)
            ace_do_clear_irq(adsp, &ace_ace3_irqs[i], mask);
    }
}


void ace_irq_set(struct adsp_dev *adsp, int irq, uint32_t mask)
{
    ace_irq_ace3_set(adsp, irq, mask);
}

void ace_irq_clear(struct adsp_dev *adsp, int irq, uint32_t mask)
{
     ace_irq_ace3_clear(adsp, irq, mask);
}


void ace30_dint_init(struct adsp_dev *adsp, MemoryRegion *parent,
                       struct adsp_io_info *info)
{
    adsp->dint[0] = info;
    ace_log("DINT core %u: Initialized at 0x%x size=0x%x\n", 0, info->space->desc.base, info->space->desc.size);
}

static uint64_t ace30_dint_read(void *opaque, hwaddr addr, unsigned size)
{
    struct adsp_io_info *info = opaque;
    hwaddr aligned_addr = addr & ~3;
    uint32_t byte_offset = addr & 3;
    uint32_t core = addr / sizeof(struct dw_ictl_registers);
    uint32_t word;
    uint64_t val;

    /* Before reading, dynamically compute status registers for dw_ictl */
    uint32_t raw_l = ace_region(offsetof(struct dw_ictl_registers, irq_rawstatus_l));
    uint32_t force_l = ace_region(offsetof(struct dw_ictl_registers, irq_intforce_l));
    uint32_t inten_l = ace_region(offsetof(struct dw_ictl_registers, irq_inten_l));
    uint32_t intmask_l = ace_region(offsetof(struct dw_ictl_registers, irq_intmask_l));
    
    ace_region(offsetof(struct dw_ictl_registers, irq_status_l)) = (raw_l | force_l) & inten_l;
    ace_region(offsetof(struct dw_ictl_registers, irq_finalstatus_l)) = ((raw_l | force_l) & inten_l) & ~intmask_l;

    uint32_t raw_h = ace_region(offsetof(struct dw_ictl_registers, irq_rawstatus_h));
    uint32_t force_h = ace_region(offsetof(struct dw_ictl_registers, irq_intforce_h));
    uint32_t inten_h = ace_region(offsetof(struct dw_ictl_registers, irq_inten_h));
    uint32_t intmask_h = ace_region(offsetof(struct dw_ictl_registers, irq_intmask_h));
    
    ace_region(offsetof(struct dw_ictl_registers, irq_status_h)) = (raw_h | force_h) & inten_h;
    ace_region(offsetof(struct dw_ictl_registers, irq_finalstatus_h)) = ((raw_h | force_h) & inten_h) & ~intmask_h;

    word = ace_region(aligned_addr);

    if (!ace_extract_subword_read(word, size, byte_offset, &val)) {
        ace_log("DINT read: unsupported size %u\n", size);
        val = 0;
    }

    ace_log("DINT read: core=%u addr=0x%lx size=%u val=0x%lx info = %p\n",
             core, (unsigned long)addr, size, (unsigned long)val, info);

    if (aligned_addr == offsetof(struct dw_ictl_registers, irq_status_l)) {
        ace_log("DINT: core %u read irq_status_l = 0x%x\n", core, word);
    } else if (aligned_addr == offsetof(struct dw_ictl_registers, irq_finalstatus_l)) {
        ace_log("DINT: core %u read irq_finalstatus_l = 0x%x\n", core, word);
    } else if (aligned_addr == offsetof(struct dw_ictl_registers, irq_status_h)) {
        ace_log("DINT: core %u read irq_status_h = 0x%x\n", core, word);
    } else if (aligned_addr == offsetof(struct dw_ictl_registers, irq_finalstatus_h)) {
        ace_log("DINT: core %u read irq_finalstatus_h = 0x%x\n", core, word);
    } else if (aligned_addr == offsetof(struct dw_ictl_registers, irq_rawstatus_l)) {
        ace_log("DINT: core %u read irq_rawstatus_l = 0x%x\n", core, word);
    } else if (aligned_addr == offsetof(struct dw_ictl_registers, irq_rawstatus_h)) {
        ace_log("DINT: core %u read irq_rawstatus_h = 0x%x\n", core, word);
    }

    return val;
}

static void ace30_dint_write(void *opaque, hwaddr addr, uint64_t val,
                                unsigned size)
{
    struct adsp_io_info *info = opaque;
    hwaddr aligned_addr = addr & ~3;
    uint32_t byte_offset = addr & 3;
    uint32_t core = addr / sizeof(struct dw_ictl_registers);
    uint32_t old_word = ace_region(aligned_addr);
    uint32_t word;

    ace_log("DINT write: core=%u addr=0x%lx size=%u val=0x%lx info=%p\n",
             core, (unsigned long)addr, size, (unsigned long)val, info);

    if (!ace_merge_subword_write(old_word, val, size, byte_offset, &word)) {
        ace_log("DINT write: unsupported size %u\n", size);
        return;
    }

    /* Handle writes */
    if (aligned_addr == offsetof(struct dw_ictl_registers, irq_inten_l)) {
        ace_log("DINT: core %u write irq_inten_l = 0x%x\n", core, word);
        ace_region(aligned_addr) = word;
    } else if (aligned_addr == offsetof(struct dw_ictl_registers, irq_inten_h)) {
        ace_log("DINT: core %u write irq_inten_h = 0x%x\n", core, word);
        ace_region(aligned_addr) = word;
    } else if (aligned_addr == offsetof(struct dw_ictl_registers, irq_intmask_l)) {
        ace_log("DINT: core %u write irq_intmask_l = 0x%x\n", core, word);
        ace_region(aligned_addr) = word;
    } else if (aligned_addr == offsetof(struct dw_ictl_registers, irq_intmask_h)) {
        ace_log("DINT: core %u write irq_intmask_h = 0x%x\n", core, word);
        ace_region(aligned_addr) = word;
    } else if (aligned_addr == offsetof(struct dw_ictl_registers, irq_intforce_l)) {
        ace_log("DINT: core %u write irq_intforce_l = 0x%x\n", core, word);
        ace_region(aligned_addr) = word;
    } else if (aligned_addr == offsetof(struct dw_ictl_registers, irq_intforce_h)) {
        ace_log("DINT: core %u write irq_intforce_h = 0x%x\n", core, word);
        ace_region(aligned_addr) = word;
    } else if (aligned_addr == offsetof(struct dw_ictl_registers, irq_rawstatus_l) ||
               aligned_addr == offsetof(struct dw_ictl_registers, irq_rawstatus_h) ||
               aligned_addr == offsetof(struct dw_ictl_registers, irq_status_l) ||
               aligned_addr == offsetof(struct dw_ictl_registers, irq_status_h) ||
               aligned_addr == offsetof(struct dw_ictl_registers, irq_finalstatus_l) ||
               aligned_addr == offsetof(struct dw_ictl_registers, irq_finalstatus_h)) {
        /* Read-only status registers, writes do nothing */
    } else {
        ace_region(aligned_addr) = word;
    }
}

const MemoryRegionOps ace30_dint_io_ops = {
    .read = ace30_dint_read,
    .write = ace30_dint_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

void ace30_dint_inject_irq(struct adsp_dev *adsp, uint32_t irq_id, bool assert_irq)
{
    /* Zephyr relies on DINT (at least on Core 0) for typical global APB IRQs */
    struct adsp_io_info *info = adsp->dint[0];
    uint32_t core = 0;
    const char *irq_name = (irq_id < 32 && ace3_irq_names[irq_id]) ? ace3_irq_names[irq_id] : "UNKNOWN";

    /* second level IRQs start at 9 */
   // irq_id += 9;
    
    ace_log("DINT inject: core=%u irq_id=%u (%s) assert_irq=%d info = %p\n",
             core, irq_id, irq_name, assert_irq, info);

    if (!info) return;

    if (irq_id < 32) {
        uint32_t raw_l = ace_region(offsetof(struct dw_ictl_registers, irq_rawstatus_l));
        uint32_t final_l = ace_region(offsetof(struct dw_ictl_registers, irq_finalstatus_l));
        uint32_t force_l = ace_region(offsetof(struct dw_ictl_registers, irq_intforce_l));
        uint32_t inten_l = ace_region(offsetof(struct dw_ictl_registers, irq_inten_l));
        uint32_t intmask_l = ace_region(offsetof(struct dw_ictl_registers, irq_intmask_l));
        
        ace_log("  -> Before: raw=0x%08x force=0x%08x en=0x%08x mask=0x%08x final=0x%08x\n",
                 raw_l, force_l, inten_l, intmask_l, final_l);
        
        if (assert_irq) {
            ace_region(offsetof(struct dw_ictl_registers, irq_rawstatus_l)) |= (1U << irq_id);
        } else {
            ace_region(offsetof(struct dw_ictl_registers, irq_rawstatus_l)) &= ~(1U << irq_id);
        }
        
    } else if (irq_id < 64) {
        uint32_t raw_h = ace_region(offsetof(struct dw_ictl_registers, irq_rawstatus_h));
        uint32_t final_h = ace_region(offsetof(struct dw_ictl_registers, irq_finalstatus_h));
        uint32_t force_h = ace_region(offsetof(struct dw_ictl_registers, irq_intforce_h));
        uint32_t inten_h = ace_region(offsetof(struct dw_ictl_registers, irq_inten_h));
        uint32_t intmask_h = ace_region(offsetof(struct dw_ictl_registers, irq_intmask_h));
        
        ace_log("  -> Before: raw=0x%08x force=0x%08x en=0x%08x mask=0x%08x final=0x%08x\n",
                 raw_h, force_h, inten_h, intmask_h, final_h);
        
        if (assert_irq) {
            ace_region(offsetof(struct dw_ictl_registers, irq_rawstatus_h)) |= (1U << (irq_id - 32));
        } else {
            ace_region(offsetof(struct dw_ictl_registers, irq_rawstatus_h)) &= ~(1U << (irq_id - 32));
        }
    }

    uint32_t raw_l = ace_region(offsetof(struct dw_ictl_registers, irq_rawstatus_l));
    uint32_t force_l = ace_region(offsetof(struct dw_ictl_registers, irq_intforce_l));
    uint32_t inten_l = ace_region(offsetof(struct dw_ictl_registers, irq_inten_l));
    uint32_t intmask_l = ace_region(offsetof(struct dw_ictl_registers, irq_intmask_l));
    
    ace_region(offsetof(struct dw_ictl_registers, irq_status_l)) = (raw_l | force_l) & inten_l;
    ace_region(offsetof(struct dw_ictl_registers, irq_finalstatus_l)) = ((raw_l | force_l) & inten_l) & ~intmask_l;

    uint32_t raw_h = ace_region(offsetof(struct dw_ictl_registers, irq_rawstatus_h));
    uint32_t force_h = ace_region(offsetof(struct dw_ictl_registers, irq_intforce_h));
    uint32_t inten_h = ace_region(offsetof(struct dw_ictl_registers, irq_inten_h));
    uint32_t intmask_h = ace_region(offsetof(struct dw_ictl_registers, irq_intmask_h));
    
    ace_region(offsetof(struct dw_ictl_registers, irq_status_h)) = (raw_h | force_h) & inten_h;
    ace_region(offsetof(struct dw_ictl_registers, irq_finalstatus_h)) = ((raw_h | force_h) & inten_h) & ~intmask_h;

    if (irq_id < 32) {
        ace_log("  -> After:  raw=0x%08x force=0x%08x en=0x%08x mask=0x%08x final=0x%08x\n",
                 ace_region(offsetof(struct dw_ictl_registers, irq_rawstatus_l)),
                 force_l, inten_l, intmask_l,
                 ace_region(offsetof(struct dw_ictl_registers, irq_finalstatus_l)));
    } else if (irq_id < 64) {
        ace_log("  -> After:  raw=0x%08x force=0x%08x en=0x%08x mask=0x%08x final=0x%08x\n",
                 ace_region(offsetof(struct dw_ictl_registers, irq_rawstatus_h)),
                 force_h, inten_h, intmask_h,
                 ace_region(offsetof(struct dw_ictl_registers, irq_finalstatus_h)));
    }
}

extern struct adsp_dev *g_adsp_dev;

void adsp_monitor_ace_irq(Monitor *mon);

void adsp_monitor_ace_irq(Monitor *mon)
{
    struct adsp_dev *adsp = g_adsp_dev;
    if (!adsp || !adsp->dint[0]) {
        return;
    }
    struct adsp_io_info *info = adsp->dint[0];

    monitor_printf(mon, "\nDW APB ICTL Controller State:\n");
    monitor_printf(mon, "  Lower: RAW=0x%08x  STAT=0x%08x  EN=0x%08x  MASK=0x%08x  FINAL=0x%08x\n",
                   ace_region(offsetof(struct dw_ictl_registers, irq_rawstatus_l)),
                   ace_region(offsetof(struct dw_ictl_registers, irq_status_l)),
                   ace_region(offsetof(struct dw_ictl_registers, irq_inten_l)),
                   ace_region(offsetof(struct dw_ictl_registers, irq_intmask_l)),
                   ace_region(offsetof(struct dw_ictl_registers, irq_finalstatus_l)));
    monitor_printf(mon, "  Upper: RAW=0x%08x  STAT=0x%08x  EN=0x%08x  MASK=0x%08x  FINAL=0x%08x\n",
                   ace_region(offsetof(struct dw_ictl_registers, irq_rawstatus_h)),
                   ace_region(offsetof(struct dw_ictl_registers, irq_status_h)),
                   ace_region(offsetof(struct dw_ictl_registers, irq_inten_h)),
                   ace_region(offsetof(struct dw_ictl_registers, irq_intmask_h)),
                   ace_region(offsetof(struct dw_ictl_registers, irq_finalstatus_h)));

    monitor_printf(mon, "\nACE IRQ Ext Counts:\n");
    for (int i = 0; i < 32; i++) {
        if (ace3_irq_counts[i] > 0) {
            monitor_printf(mon, "  IRQ %2d (%10s): %llu\n",
                           i, ace3_irq_names[i] ? ace3_irq_names[i] : "UNKNOWN",
                           (unsigned long long)ace3_irq_counts[i]);
        }
    }
}
