/* Internal ACE function declarations
 *
 * Copyright (C) 2016 Intel Corporation
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

#ifndef HW_ADSP_ACE_INTERNAL_H
#define HW_ADSP_ACE_INTERNAL_H

#include "hw/audio/adsp-dev.h"
#include "hw/core/boards.h"

#define MAX_HPSRAM_BANKS 36
#define MAX_LPSRAM_BANKS 8
#define HPSRAM_BANK_SIZE (128 * 1024)
#define LPSRAM_BANK_SIZE (8 * 1024)

/*
 * ACE MMIO debug logging
 *
 * ACE register handlers use qemu_log() for diagnostics.
 * To enable logs at runtime:
 *   1) pass -D <logfile> to select the destination file
 *   2) optionally pass -d <items> for other QEMU debug classes
 *
 *
 * Example:
 *   ./build/qemu-system-xtensa -machine adsp_ace30 -nographic \
 *       -D /tmp/ace-mmio.log -d guest_errors
 */

void ace_log_prefix(void);

#define ace_log(fmt, ...) do { \
    ace_log_prefix(); \
    qemu_log(fmt, ##__VA_ARGS__); \
} while(0)

#define ace_log_mask(mask, fmt, ...) do { \
    if (qemu_loglevel_mask(mask)) { \
        ace_log_prefix(); \
        qemu_log_mask(mask, fmt, ##__VA_ARGS__); \
    } \
} while(0)


static inline bool ace_merge_subword_write(uint32_t old_word, uint64_t val,
                                           unsigned size,
                                           uint32_t byte_offset,
                                           uint32_t *new_word)
{
    uint32_t word;

    if (size == 4 && byte_offset == 0) {
        *new_word = (uint32_t)val;
        return true;
    }

    word = old_word;
    switch (size) {
    case 1:
        word &= ~(0xFFu << (byte_offset * 8));
        word |= ((uint32_t)val & 0xFFu) << (byte_offset * 8);
        break;
    case 2:
        word &= ~(0xFFFFu << (byte_offset * 8));
        word |= ((uint32_t)val & 0xFFFFu) << (byte_offset * 8);
        break;
    case 4:
        word = (uint32_t)val;
        break;
    default:
        return false;
    }

    *new_word = word;
    return true;
}

static inline bool ace_extract_subword_read(uint32_t word, unsigned size,
                                            uint32_t byte_offset,
                                            uint64_t *val)
{
    switch (size) {
    case 1:
        *val = (word >> (byte_offset * 8)) & 0xFFu;
        break;
    case 2:
        *val = (word >> (byte_offset * 8)) & 0xFFFFu;
        break;
    case 4:
        *val = word;
        break;
    default:
        return false;
    }

    return true;
}

/* ace-shim.c */

uint64_t ace_set_time(struct adsp_dev *adsp, struct adsp_io_info *info);
void ace_rearm_ext_timer0(struct adsp_dev *adsp, struct adsp_io_info *info);
void ace_rearm_ext_timer1(struct adsp_dev *adsp, struct adsp_io_info *info);
void ace_ext_timer_cb0(void *opaque);
void ace_ext_timer_cb1(void *opaque);
void ace_shim_reset(void *opaque);
uint64_t ace_shim_read(void *opaque, hwaddr addr, unsigned size);
void ace_shim_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
void adsp_ace_shim_init(struct adsp_dev *adsp, MemoryRegion *parent,
        struct adsp_io_info *info);
extern const MemoryRegionOps ace_shim_ops;

/* ace-irq.c */
extern const struct adsp_dev_ops ace_ace3_ops;
extern const MemoryRegionOps ace_irq_io_ops;
/* ace_irq_set and ace_irq_clear are declared in hw/adsp/shim.h */
void ace_irq_init(struct adsp_dev *adsp, MemoryRegion *parent,
        struct adsp_io_info *info);

/* ace-dint.c / ace-ipc.c shared interrupt routing constants */
#define IRQ_NUM_EXT_LEVEL2  4
#define IRQ_NUM_EXT_LEVEL3  6
#define IRQ_NUM_EXT_LEVEL4  7
#define IRQ_NUM_EXT_LEVEL5  8

/* ace-ipc.c */
extern const MemoryRegionOps ace_ipc_ace3_io_ops;
extern const MemoryRegionOps ace_dw_intc_io_ops;


/* ace-low.c */
void ace_idc_dsp_block_init(struct adsp_dev *adsp, MemoryRegion *parent,
    struct adsp_io_info *info);
void ace_hfintip_block_init(struct adsp_dev *adsp, MemoryRegion *parent,
    struct adsp_io_info *info);
void ace_socci_block_init(struct adsp_dev *adsp, MemoryRegion *parent,
    struct adsp_io_info *info);
void ace_hfpmccu_block_init(struct adsp_dev *adsp, MemoryRegion *parent,
    struct adsp_io_info *info);
void ace_hfpmcch_block_init(struct adsp_dev *adsp, MemoryRegion *parent,
    struct adsp_io_info *info);
void ace_secpol_block_init(struct adsp_dev *adsp, MemoryRegion *parent,
    struct adsp_io_info *info);
void ace_tsocfgu_aon_block_init(struct adsp_dev *adsp, MemoryRegion *parent,
    struct adsp_io_info *info);
void ace_dfcapsts_block_init(struct adsp_dev *adsp, MemoryRegion *parent,
    struct adsp_io_info *info);
void ace_adcip_block_init(struct adsp_dev *adsp, MemoryRegion *parent,
    struct adsp_io_info *info);
void ace_adcs_block_init(struct adsp_dev *adsp, MemoryRegion *parent,
    struct adsp_io_info *info);

extern const MemoryRegionOps ace_idc_dsp_block_ops;
extern const MemoryRegionOps ace_hfintip_block_ops;
extern const MemoryRegionOps ace_socci_block_ops;
extern const MemoryRegionOps ace_hfpmccu_block_ops;
extern const MemoryRegionOps ace_hfpmcch_block_ops;
extern const MemoryRegionOps ace_secpol_block_ops;
extern const MemoryRegionOps ace_tsocfgu_aon_block_ops;
extern const MemoryRegionOps ace_dfcapsts_block_ops;
extern const MemoryRegionOps ace_adcip_block_ops;
extern const MemoryRegionOps ace_adcs_block_ops;

/* ace-hostwin.c */

/* Moved from ace.h / ace-hostwin.c */
#define ADSP_ACE30_DSP_HOST_WIN_BASE(x) (0x00071A00 + (x) * 0x20)
#define ADSP_ACE30_DSP_HOST_WIN_SIZE    0x20

#define DTFXCTL_OFFSET        0x00
#define DTFXSTS_OFFSET        0x04
#define DTFXSFFCTL_OFFSET     0x14
#define DTFXSFFSTS_OFFSET     0x18
#define DTFCXD64TS_OFFSET     0x20
#define DTFCXD64_OFFSET       0x28
#define DTFCXD64M_OFFSET      0x30
#define DTFCXD64DMA_OFFSET    0x38

#define DTFXCTL_RW_MASK       0x0000000f
#define DTFXSFFCTL_RW_MASK    0x00001ff8

void ace_hostwin_init(struct adsp_dev *adsp, MemoryRegion *parent,
    struct adsp_io_info *info);
extern const MemoryRegionOps ace_hostwin_io_ops;

/* ace-dmw.c */
void ace_dmw_init(struct adsp_dev *adsp, MemoryRegion *parent,
    struct adsp_io_info *info);
extern const MemoryRegionOps ace_dmw_io_ops;

/* ace-pmccu.c */
void ace_pmccu_init(struct adsp_dev *adsp, MemoryRegion *parent,
    struct adsp_io_info *info);
extern const MemoryRegionOps ace_pmccu_io_ops;

/* ace-l2.c */
void sram_banks_global_init(struct adsp_dev *adsp);
void ace30_dfl2mm_init(struct adsp_dev *adsp, MemoryRegion *parent,
    struct adsp_io_info *info);
void ace30_dfl2hsbpm_init(struct adsp_dev *adsp, MemoryRegion *parent,
    struct adsp_io_info *info);
void ace30_dfl2usbpm_init(struct adsp_dev *adsp, MemoryRegion *parent,
    struct adsp_io_info *info);
extern const MemoryRegionOps ace30_dfl2mm_io_ops;
extern const MemoryRegionOps ace30_dfl2hsbpm_io_ops;
extern const MemoryRegionOps ace30_dfl2usbpm_io_ops;

/* ace-dspcs.c */
void ace30_dspcs_init(struct adsp_dev *adsp, MemoryRegion *parent,
    struct adsp_io_info *info);
extern const MemoryRegionOps ace30_dspcs_io_ops;

void ace_secondary_core_set_running(struct adsp_dev *adsp, uint32_t core_num, 
                                    bool running, const char *reason);

void adsp_monitor_ace_core(Monitor *mon, const QDict *qdict);

void adsp_scan_and_print_memory(Monitor *mon, hwaddr base, uint32_t size,
                                int *tot_pages, int *tot_used, 
                                int state, const char *type, int bank_id);

/* ace-hfimr.c */
void ace30_hfimr_init(struct adsp_dev *adsp, MemoryRegion *parent,
    struct adsp_io_info *info);
extern const MemoryRegionOps ace30_hfimr_io_ops;

/* ace-hfipc.c */
void ace30_hfipc_init(struct adsp_dev *adsp, MemoryRegion *parent,
    struct adsp_io_info *info);
extern const MemoryRegionOps ace30_hfipc_io_ops;

/* ace-dint.c */
void ace30_dint_init(struct adsp_dev *adsp, MemoryRegion *parent,
    struct adsp_io_info *info);
extern const MemoryRegionOps ace30_dint_io_ops;
void ace30_dint_inject_irq(struct adsp_dev *adsp, uint32_t irq_id, bool assert_irq);

/* ace-manifest.c */
struct adsp_fw_desc;  /* Forward declaration */

void copy_man_modules(const struct adsp_desc *board, struct adsp_dev *adsp,
    struct adsp_fw_desc *desc, void *file_start);
void copy_man_to_imr(const struct adsp_desc *board, struct adsp_dev *adsp,
    struct adsp_fw_desc *desc, uint32_t imr_addr);
void adsp_monitor_ace_manifest(Monitor *mon, const QDict *qdict);
void adsp_monitor_ace_ipc(Monitor *mon, const QDict *qdict);
void adsp_monitor_ace_ipc_tx(Monitor *mon, const QDict *qdict);
void adsp_monitor_ace_ipc_rx(Monitor *mon, const QDict *qdict);

/* ace-memory.c */
void *ace_load_firmware(const char *filename);

/* Memory descriptors */
extern struct adsp_mem_desc ace_ace3_mem[];
#define ace_ace3_mem_num 4

extern struct adsp_mem_desc ace_ace15_mem[];
#define ace_ace15_mem_num 4

extern struct adsp_mem_desc ace_ace20_mem[];
#define ace_ace20_mem_num 4

extern const MemoryRegionOps unmapped_ops;

struct adsp_dev *adsp_ace_init(const struct adsp_desc *board,
    MachineState *machine, const MemoryRegionOps *io_ops,
    uint32_t exec_addr, uint32_t imr_addr, uint32_t clk_kHz);

void adsp_machine_class_add_options(MachineClass *mc);


void adsp_ace_dma_init(struct adsp_dev *adsp, MemoryRegion *parent,
                        struct adsp_io_info *info);
extern const MemoryRegionOps ace_hda_stream_ops;
extern const MemoryRegionOps ace_hda_gtw_ops;
uint64_t ace_hda_stream_read(void *opaque, hwaddr addr, unsigned size);
void ace_hda_stream_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
uint64_t ace_gpdma_read(void *opaque, hwaddr addr, unsigned size);
void ace_gpdma_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);

/* IDC and Timer operations */
void ace30_idc_init(struct adsp_dev *adsp, MemoryRegion *parent,
    struct adsp_io_info *info);
uint64_t ace_idc_read(void *opaque, hwaddr addr, unsigned size);
void ace_idc_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
void ace_timer_init(struct adsp_dev *adsp, MemoryRegion *parent,
    struct adsp_io_info *info);
uint64_t ace_timer_read(void *opaque, hwaddr addr, unsigned size);
void ace_timer_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
extern uint64_t ace_timer_ticks_adjustment;
extern const MemoryRegionOps ace_idc_ops;
extern const MemoryRegionOps ace_timer_ops;

void ace_hda_stream_emulate_cavstool(uint32_t cmd, uint32_t channel, uint32_t ext_data);

/* DfPMCCU support */
#endif /* HW_ADSP_ACE_INTERNAL_H */
