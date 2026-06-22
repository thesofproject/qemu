/* ACE L2 memory and SRAM power-management virtualization
 * IP Region: DfL2* banks for SRAM power state, policy, and L2 MMIO exposure.
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
#include "qemu/timer.h"
#include "monitor/monitor.h"
#include "monitor/hmp-target.h"
#include "system/memory.h"
#include "system/address-spaces.h"
#include "qobject/qdict.h"
#include "hw/audio/adsp-dev.h"
#include "hw/adsp/ace.h"
#include "ace-internal.h"

/* Moved from ace.h */
#define ADSP_ACE30_DFL2MM_DFL2MCAP      0x00
#define ADSP_ACE30_DFL2MM_DFL2MPAT      0x04

#define ace_region(raddr)    info->region[(raddr) >> 2]

#define POWER_TRANSITION_DELAY_NS 0

static hwaddr hp_sram_base;
static hwaddr lp_sram_base;

static const struct adsp_mem_desc *ace_mem_desc_by_name(struct adsp_dev *adsp,
                                                        const char *name)
{
    int i;

    for (i = 0; i < adsp->desc->num_mem; i++) {
        if (strcmp(adsp->desc->mem_region[i].name, name) == 0) {
            return &adsp->desc->mem_region[i];
        }
    }

    return NULL;
}

typedef enum {
    SRAM_POWER_OFF = 0,
    SRAM_POWERING_ON = 1,
    SRAM_POWER_ON = 2,
    SRAM_POWERING_OFF = 3,
} SRAMPowerState;

static const char *sram_state_name(SRAMPowerState state)
{
    switch (state) {
    case SRAM_POWER_OFF:
        return "OFF";
    case SRAM_POWERING_ON:
        return "POWERING_ON";
    case SRAM_POWER_ON:
        return "ON";
    case SRAM_POWERING_OFF:
        return "POWERING_OFF";
    default:
        return "UNKNOWN";
    }
}

static const char *sram_pm_block_name(bool is_hpsram)
{
    return is_hpsram ? "DfL2HSBPM" : "DfL2USBPM";
}

static hwaddr lp_sram_bank_base(int bank_id);
static size_t lp_sram_bank_size(int bank_id);

static void sram_bank_wipe(bool is_hpsram, int bank_id)
{
    hwaddr base_addr;
    hwaddr size;

    if (is_hpsram) {
        base_addr = hp_sram_base + (bank_id * HPSRAM_BANK_SIZE);
        size = HPSRAM_BANK_SIZE;
    } else {
        base_addr = lp_sram_bank_base(bank_id);
        size = lp_sram_bank_size(bank_id);
    }
    
    address_space_set(&address_space_memory, base_addr, 0xff, size, MEMTXATTRS_UNSPECIFIED);
}

static hwaddr lp_sram_bank_base(int bank_id)
{
    return lp_sram_base + (bank_id * LPSRAM_BANK_SIZE);
}

static size_t lp_sram_bank_size(int bank_id)
{
    return LPSRAM_BANK_SIZE;
}

typedef struct {
    SRAMPowerState state;
    uint32_t pgctl;
    uint32_t rmctl;
    QEMUTimer *transition_timer;
    struct adsp_dev *adsp;
    struct adsp_io_info *io_info;
    int bank_id;
    bool is_hpsram;
} SRAMBankPowerState;

static SRAMBankPowerState hpsram_banks[MAX_HPSRAM_BANKS];
static SRAMBankPowerState lpsram_banks[MAX_LPSRAM_BANKS];
static int hpsram_bank_count = 36;
static int lpsram_bank_count = 8;

static void update_bank_from_pgctl(SRAMBankPowerState *bank);

#define L2LMCAP     ADSP_ACE30_DFL2MM_DFL2MCAP
#define L2MPAT      ADSP_ACE30_DFL2MM_DFL2MPAT

/*
 * HP SRAM PM register layout (array of structs), per user/FW contract:
 * struct ace_hpsram_regs {
 *   uint8_t HSxPGCTL;   // +0
 *   uint8_t HSxRMCTL;   // +1
 *   uint8_t reserved[2];// +2..+3
 *   uint8_t HSxPGISTS;  // +4
 *   uint8_t reserved1[3];//+5..+7
 * };
 */
#define HPSRAM_REG_STRIDE     0x8
#define HPSRAM_PGCTL_OFF      0x0
#define HPSRAM_RMCTL_OFF      0x1
#define HPSRAM_PGISTS_OFF     0x4
#define SRAM_PGCTL_MASK       0x1

/*
 * LP SRAM PM register layout mirrors HP layout as an array of structs:
 * struct ace_lpsram_regs {
 *   uint8_t USxPGCTL;   // +0
 *   uint8_t USxRMCTL;   // +1
 *   uint8_t reserved[2];// +2..+3
 *   uint8_t USxPGISTS;  // +4
 *   uint8_t reserved1[3];//+5..+7
 * };
 *
 * LP bank array base is 0x40 in this PM block.
 */
#define LPSRAM_REG_BASE       0x40
#define LPSRAM_REG_STRIDE     0x8
#define LPSRAM_PGCTL_OFF      0x0
#define LPSRAM_RMCTL_OFF      0x1
#define LPSRAM_PGISTS_OFF     0x4

#define L2MCAP_L2HSS_SHIFT   0
#define L2MCAP_L2USS_SHIFT   8
#define L2MCAP_L2HSBS_SHIFT  12
#define L2MCAP_L2HS2S_SHIFT  16
#define L2MCAP_L2USBS_SHIFT  24
#define L2MCAP_L2SE_SHIFT    29
#define L2MCAP_EL2SE_SHIFT   30

static uint32_t ace_l2mcap_value(void)
{
    return (hpsram_bank_count << L2MCAP_L2HSS_SHIFT) |
           (lpsram_bank_count << L2MCAP_L2USS_SHIFT) |
           (4u << L2MCAP_L2HSBS_SHIFT) |
           (13u << L2MCAP_L2USBS_SHIFT);
}

static inline uint8_t *pm_region8(struct adsp_io_info *info)
{
    return (uint8_t *)info->region;
}

static bool hp_reg_decode(hwaddr addr, int *bank_id, unsigned *field)
{
    int bank = addr / HPSRAM_REG_STRIDE;
    unsigned off = addr % HPSRAM_REG_STRIDE;

    if (bank < 0 || bank >= hpsram_bank_count) {
        return false;
    }

    *bank_id = bank;
    *field = off;
    return true;
}

static bool lp_reg_decode(hwaddr addr, int *bank_id, unsigned *field)
{
    hwaddr rel;
    int bank;
    unsigned off;

    if (addr < LPSRAM_REG_BASE) {
        return false;
    }

    rel = addr - LPSRAM_REG_BASE;
    bank = rel / LPSRAM_REG_STRIDE;
    off = rel % LPSRAM_REG_STRIDE;

    if (bank < 0 || bank >= lpsram_bank_count) {
        return false;
    }

    *bank_id = bank;
    *field = off;
    return true;
}

static uint64_t pm_read_bytes(struct adsp_io_info *info, hwaddr addr,
                              unsigned size)
{
    uint8_t *r8 = pm_region8(info);
    uint64_t val = 0;
    unsigned i;
    uint32_t region_size = info->space->desc.size;

    if (size == 0 || addr >= region_size || addr + size > region_size) {
        qemu_log("DfL2PM read: out-of-range addr=0x%lx size=%u region=0x%x\n",
                 (unsigned long)addr, size, region_size);
        return 0;
    }

    for (i = 0; i < size; i++) {
        val |= ((uint64_t)r8[addr + i]) << (8 * i);
    }
    return val;
}

static bool hp_field_is_writable(unsigned field)
{
    return field == HPSRAM_PGCTL_OFF || field == HPSRAM_RMCTL_OFF;
}

static bool lp_field_is_writable(unsigned field)
{
    return field == LPSRAM_PGCTL_OFF || field == LPSRAM_RMCTL_OFF;
}

static void write_hpsram_pm(struct adsp_io_info *info, hwaddr addr,
                            uint64_t val, unsigned size)
{
    uint8_t *r8 = pm_region8(info);
    unsigned i;
    uint32_t region_size = info->space->desc.size;

    if (size == 0) {
        return;
    }

    for (i = 0; i < size; i++) {
        hwaddr a = addr + i;
        int bank_id;
        unsigned field;
        uint8_t byte_val;
        SRAMBankPowerState *bank;

        if (a >= region_size) {
            qemu_log("DfL2HSBPM write: out-of-range byte addr=0x%lx region=0x%x\n",
                     (unsigned long)a, region_size);
            continue;
        }

        if (!hp_reg_decode(a, &bank_id, &field)) {
            continue;
        }

        if (!hp_field_is_writable(field)) {
            qemu_log("DfL2HSBPM write ignored: bank=%d field=0x%x (RO/reserved)\n",
                     bank_id, field);
            continue;
        }

        bank = &hpsram_banks[bank_id];
        byte_val = (val >> (8 * i)) & 0xFF;

        if (field == HPSRAM_PGCTL_OFF) {
            SRAMPowerState old_state = bank->state;
            uint8_t pgctl = byte_val & SRAM_PGCTL_MASK;

            bank->pgctl = pgctl;
            r8[a] = pgctl;
            update_bank_from_pgctl(bank);

            qemu_log("DfL2HSBPM: bank=%d HSxPGCTL=0x%02x state=%s->%s\n",
                     bank_id, pgctl,
                     sram_state_name(old_state),
                     sram_state_name(bank->state));
        } else if (field == HPSRAM_RMCTL_OFF) {
            bank->rmctl = byte_val;
            r8[a] = byte_val;
        }
    }
}

static void write_lpsram_pm(struct adsp_io_info *info, hwaddr addr,
                            uint64_t val, unsigned size)
{
    uint8_t *r8 = pm_region8(info);
    unsigned i;
    uint32_t region_size = info->space->desc.size;

    if (size == 0) {
        return;
    }

    for (i = 0; i < size; i++) {
        hwaddr a = addr + i;
        int bank_id;
        unsigned field;
        uint8_t byte_val;
        SRAMBankPowerState *bank;

        if (a >= region_size) {
            qemu_log("DfL2USBPM write: out-of-range byte addr=0x%lx region=0x%x\n",
                     (unsigned long)a, region_size);
            continue;
        }

        if (!lp_reg_decode(a, &bank_id, &field)) {
            continue;
        }

        if (!lp_field_is_writable(field)) {
            qemu_log("DfL2USBPM write ignored: bank=%d field=0x%x (RO/reserved)\n",
                     bank_id, field);
            continue;
        }

        bank = &lpsram_banks[bank_id];
        byte_val = (val >> (8 * i)) & 0xFF;

        if (field == LPSRAM_PGCTL_OFF) {
            SRAMPowerState old_state = bank->state;
            uint8_t pgctl = byte_val & SRAM_PGCTL_MASK;

            bank->pgctl = pgctl;
            r8[a] = pgctl;
            update_bank_from_pgctl(bank);
            qemu_log("DfL2USBPM: bank=%d USxPGCTL=0x%02x state=%s->%s\n",
                     bank_id, pgctl,
                     sram_state_name(old_state),
                     sram_state_name(bank->state));
        } else if (field == LPSRAM_RMCTL_OFF) {
            bank->rmctl = byte_val;
            r8[a] = byte_val;
        }
    }
}

static void hp_set_pgists(struct adsp_io_info *info, int bank_id, bool off)
{
    uint8_t *r8 = pm_region8(info);
    hwaddr pgists = bank_id * HPSRAM_REG_STRIDE + HPSRAM_PGISTS_OFF;

    /* PGISTS exposes the hardware-observed power-gated state for this HP SRAM bank. */
    r8[pgists] = off ? 1 : 0;
    qemu_log( "DfL2HSBPM: bank=%d HSxPGISTS=0x%x at offset 0x%lx\n",
        bank_id, r8[pgists], (unsigned long)pgists);
}

static void lp_set_pgists(struct adsp_io_info *info, int bank_id, bool off)
{
    uint8_t *r8 = pm_region8(info);
    hwaddr pgists = LPSRAM_REG_BASE +
                    bank_id * LPSRAM_REG_STRIDE +
                    LPSRAM_PGISTS_OFF;

    /* LP SRAM uses the same status convention so FW can poll bank-level power state. */
    r8[pgists] = off ? 1 : 0;
    qemu_log( "DfL2USBPM: bank=%d USxPGISTS=0x%x at offset 0x%lx\n",
        bank_id, r8[pgists], (unsigned long)pgists);
}

static void init_hpsram_pm_region(struct adsp_io_info *info)
{
    uint8_t *r8 = pm_region8(info);
    int i;

    for (i = 0; i < hpsram_bank_count; i++) {
        hwaddr base = i * HPSRAM_REG_STRIDE;

        /* Reset bank control registers so every HP bank starts powered and unretained. */
        r8[base + HPSRAM_PGCTL_OFF] = 0;
        r8[base + HPSRAM_RMCTL_OFF] = 0;
        r8[base + HPSRAM_PGISTS_OFF] = 0;
        hpsram_banks[i].io_info = info;
    }
}

static void init_lpsram_pm_region(struct adsp_io_info *info)
{
    uint8_t *r8 = pm_region8(info);
    int i;

    for (i = 0; i < lpsram_bank_count; i++) {
        hwaddr base = LPSRAM_REG_BASE + i * LPSRAM_REG_STRIDE;

        /* Reset LP bank controls and advertise no bank-level power-gate pending at boot. */
        r8[base + LPSRAM_PGCTL_OFF] = 0;
        r8[base + LPSRAM_RMCTL_OFF] = 0;
        r8[base + LPSRAM_PGISTS_OFF] = 0;
        lpsram_banks[i].io_info = info;
    }
}

void sram_banks_global_init(struct adsp_dev *adsp)
{
    const struct adsp_mem_desc *hp_mem = ace_mem_desc_by_name(adsp, "hp-sram");
    const struct adsp_mem_desc *lp_mem = ace_mem_desc_by_name(adsp, "lp-sram");
    int i;

    if (hp_mem) {
        hp_sram_base = hp_mem->base;
        hpsram_bank_count = MIN(hp_mem->size / HPSRAM_BANK_SIZE, MAX_HPSRAM_BANKS);
    }

    if (lp_mem) {
        lp_sram_base = lp_mem->base;
        lpsram_bank_count = MIN(lp_mem->size / LPSRAM_BANK_SIZE, MAX_LPSRAM_BANKS);
    }

    for (i = 0; i < hpsram_bank_count; i++) {
        hpsram_banks[i].state = SRAM_POWER_ON;
        hpsram_banks[i].pgctl = 0;
        hpsram_banks[i].rmctl = 0;
        hpsram_banks[i].transition_timer = NULL;
        hpsram_banks[i].adsp = adsp;
        hpsram_banks[i].io_info = NULL;
        hpsram_banks[i].bank_id = i;
        hpsram_banks[i].is_hpsram = true;
        sram_bank_wipe(true, i);
    }

    for (i = 0; i < lpsram_bank_count; i++) {
        lpsram_banks[i].state = SRAM_POWER_ON;
        lpsram_banks[i].pgctl = 0;
        lpsram_banks[i].rmctl = 0;
        lpsram_banks[i].transition_timer = NULL;
        lpsram_banks[i].adsp = adsp;
        lpsram_banks[i].io_info = NULL;
        lpsram_banks[i].bank_id = i;
        lpsram_banks[i].is_hpsram = false;
        sram_bank_wipe(false, i);
    }
}

static void sram_bank_log_power_event(struct adsp_dev *adsp, bool is_hpsram,
                                      int bank_id, bool power_on)
{
    hwaddr base_addr;

    if (!adsp || !adsp->system_memory) {
        return;
    }

    if (is_hpsram) {
        base_addr = hp_sram_base + (bank_id * HPSRAM_BANK_SIZE);
    } else {
        base_addr = lp_sram_bank_base(bank_id);
    }

    qemu_log( "%s power-%s: %s bank %d at 0x%lx size=0x%zx\n",
            sram_pm_block_name(is_hpsram),
            power_on ? "on" : "off",
            is_hpsram ? "HP" : "LP", bank_id, base_addr,
            is_hpsram ? (size_t)HPSRAM_BANK_SIZE : lp_sram_bank_size(bank_id));
}

static void sram_power_transition_complete(void *opaque)
{
    SRAMBankPowerState *bank = (SRAMBankPowerState *)opaque;
    struct adsp_io_info *info = bank->io_info;
    struct adsp_dev *adsp = bank->adsp;

        qemu_log(
            "%s TIMER FIRED: type=%s bank=%d state=%s pgctl=0x%x rmctl=0x%x\n",
            sram_pm_block_name(bank->is_hpsram),
            bank->is_hpsram ? "HP" : "LP", bank->bank_id,
            sram_state_name(bank->state), bank->pgctl, bank->rmctl);

    if (!info) {
        qemu_log( "Error: SRAM power transition timer fired but io_info is NULL\n");
        return;
    }

    if (bank->state == SRAM_POWERING_ON) {
        bank->state = SRAM_POWER_ON;

        sram_bank_log_power_event(adsp, bank->is_hpsram, bank->bank_id, true);

        if (bank->is_hpsram) {
            hp_set_pgists(info, bank->bank_id, false);
            qemu_log( "DfL2HSBPM: bank=%d state=ON HSxPGISTS=0\n", bank->bank_id);
        } else {
            lp_set_pgists(info, bank->bank_id, false);
            qemu_log( "DfL2USBPM: bank=%d state=ON USxPGISTS=0\n", bank->bank_id);
        }
    } else if (bank->state == SRAM_POWERING_OFF) {
        bank->state = SRAM_POWER_OFF;

        sram_bank_log_power_event(adsp, bank->is_hpsram, bank->bank_id, false);

        if (bank->is_hpsram) {
            hp_set_pgists(info, bank->bank_id, true);
            qemu_log( "DfL2HSBPM: bank=%d state=OFF HSxPGISTS=1\n", bank->bank_id);
            sram_bank_wipe(true, bank->bank_id);
        } else {
            lp_set_pgists(info, bank->bank_id, true);
            qemu_log( "DfL2USBPM: bank=%d state=OFF USxPGISTS=1\n", bank->bank_id);
            sram_bank_wipe(false, bank->bank_id);
        }
    }
}

void ace30_dfl2mm_init(struct adsp_dev *adsp, MemoryRegion *parent,
                         struct adsp_io_info *info)
{
    uint32_t l2mcap = ace_l2mcap_value();

    /* DfL2MCAP publishes the SRAM topology that FW uses to size bank-management loops. */
    ace_region(L2LMCAP) = l2mcap;

        qemu_log(
            "DfL2MM: Initialized L2LMCAP=0x%x (HSS=%d USS=%d HSBS=%d USBS=%d)\n",
            ace_region(L2LMCAP), hpsram_bank_count, lpsram_bank_count, 4, 13);
}

static uint64_t ace30_dfl2mm_read(void *opaque, hwaddr addr, unsigned size)
{
    struct adsp_io_info *info = opaque;
    hwaddr aligned_addr = addr & ~3;
    uint32_t word = ace_region(aligned_addr);
    uint32_t byte_offset = addr & 3;
    uint64_t val;

    if (!ace_extract_subword_read(word, size, byte_offset, &val)) {
        qemu_log( "DfL2MM read: unsupported size %u\n", size);
        val = 0;
    }

    qemu_log( "DfL2MM read: addr=0x%lx size=%u val=0x%lx (byte_offset=%u)\n",
            (unsigned long)addr, size, (unsigned long)val, byte_offset);
    return val;
}

static void ace30_dfl2mm_write(void *opaque, hwaddr addr, uint64_t val,
                                 unsigned size)
{
    uint32_t byte_offset = addr & 3;

    qemu_log( "DfL2MM write: addr=0x%lx size=%u val=0x%lx (byte_offset=%u)\n",
            (unsigned long)addr, size, (unsigned long)val, byte_offset);
}

const MemoryRegionOps ace30_dfl2mm_io_ops = {
    .read = ace30_dfl2mm_read,
    .write = ace30_dfl2mm_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

void ace30_dfl2hsbpm_init(struct adsp_dev *adsp, MemoryRegion *parent,
                          struct adsp_io_info *info)
{
    init_hpsram_pm_region(info);

        qemu_log( "DfL2HSBPM: initialized HP SRAM regs=%d as struct array\n",
            hpsram_bank_count);
}

static uint64_t ace30_dfl2hsbpm_read(void *opaque, hwaddr addr, unsigned size)
{
    struct adsp_io_info *info = opaque;
    uint64_t val = pm_read_bytes(info, addr, size);

    qemu_log( "DfL2HSBPM read: addr=0x%lx size=%u val=0x%lx\n",
            (unsigned long)addr, size, (unsigned long)val);
    return val;
}

static void update_bank_from_pgctl(SRAMBankPowerState *bank)
{
    uint64_t now_ns;
    uint64_t deadline_ns;
    bool was_pending = false;
    bool immediate = POWER_TRANSITION_DELAY_NS == 0;

    if (!immediate && !bank->transition_timer) {
        bank->transition_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                              sram_power_transition_complete,
                                              bank);
        qemu_log(
            "%s TIMER CREATE: type=%s bank=%d\n",
            sram_pm_block_name(bank->is_hpsram),
                bank->is_hpsram ? "HP" : "LP", bank->bank_id);
    } else if (!immediate) {
        was_pending = timer_pending(bank->transition_timer);
    }

    now_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    deadline_ns = now_ns + POWER_TRANSITION_DELAY_NS;

        qemu_log(
            "%s UPDATE: type=%s bank=%d pgctl=0x%x state=%s timer_pending=%d\n",
            sram_pm_block_name(bank->is_hpsram),
            bank->is_hpsram ? "HP" : "LP", bank->bank_id, bank->pgctl,
            sram_state_name(bank->state), was_pending ? 1 : 0);

    if (bank->pgctl == 0 && bank->state != SRAM_POWER_ON &&
        bank->state != SRAM_POWERING_ON) {
        SRAMPowerState old_state = bank->state;

        bank->state = SRAM_POWERING_ON;
        if (immediate) {
            qemu_log(
                "%s COMPLETE: type=%s bank=%d transition=%s->%s immediate\n",
                sram_pm_block_name(bank->is_hpsram),
                bank->is_hpsram ? "HP" : "LP", bank->bank_id,
                sram_state_name(old_state), sram_state_name(bank->state));
            sram_power_transition_complete(bank);
        } else {
            timer_mod(bank->transition_timer, deadline_ns);
            qemu_log(
                "%s TIMER ARM: type=%s bank=%d transition=%s->%s at=%llu (now=%llu)\n",
                sram_pm_block_name(bank->is_hpsram),
                    bank->is_hpsram ? "HP" : "LP", bank->bank_id,
                    sram_state_name(old_state), sram_state_name(bank->state),
                    (unsigned long long)deadline_ns,
                    (unsigned long long)now_ns);
        }
    } else if (bank->pgctl != 0 && bank->state != SRAM_POWER_OFF &&
               bank->state != SRAM_POWERING_OFF) {
        SRAMPowerState old_state = bank->state;

        bank->state = SRAM_POWERING_OFF;
        if (immediate) {
            qemu_log(
                "%s COMPLETE: type=%s bank=%d transition=%s->%s immediate\n",
                sram_pm_block_name(bank->is_hpsram),
                bank->is_hpsram ? "HP" : "LP", bank->bank_id,
                sram_state_name(old_state), sram_state_name(bank->state));
            sram_power_transition_complete(bank);
        } else {
            timer_mod(bank->transition_timer, deadline_ns);
            qemu_log(
                "%s TIMER ARM: type=%s bank=%d transition=%s->%s at=%llu (now=%llu)\n",
                sram_pm_block_name(bank->is_hpsram),
                    bank->is_hpsram ? "HP" : "LP", bank->bank_id,
                    sram_state_name(old_state), sram_state_name(bank->state),
                    (unsigned long long)deadline_ns,
                    (unsigned long long)now_ns);
        }
    } else {
        qemu_log(
            "%s TIMER SKIP: type=%s bank=%d state=%s pgctl=0x%x\n",
            sram_pm_block_name(bank->is_hpsram),
                bank->is_hpsram ? "HP" : "LP", bank->bank_id,
                sram_state_name(bank->state), bank->pgctl);
    }
}

static void ace30_dfl2hsbpm_write(void *opaque, hwaddr addr, uint64_t val,
                                  unsigned size)
{
    struct adsp_io_info *info = opaque;

    qemu_log( "DfL2HSBPM write: addr=0x%lx size=%u val=0x%lx\n",
            (unsigned long)addr, size, (unsigned long)val);

    write_hpsram_pm(info, addr, val, size);
}

const MemoryRegionOps ace30_dfl2hsbpm_io_ops = {
    .read = ace30_dfl2hsbpm_read,
    .write = ace30_dfl2hsbpm_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

void ace30_dfl2usbpm_init(struct adsp_dev *adsp, MemoryRegion *parent,
                          struct adsp_io_info *info)
{
    init_lpsram_pm_region(info);
    qemu_log( "DfL2USBPM: Initialized at 0x71D80\n");
}

static uint64_t ace30_dfl2usbpm_read(void *opaque, hwaddr addr, unsigned size)
{
    struct adsp_io_info *info = opaque;
    uint64_t value = pm_read_bytes(info, addr, size);
    qemu_log( "DfL2USBPM read: addr=0x%lx size=%u value=0x%lx\n",
        (unsigned long)addr, size, (unsigned long)value);
    return value;
}

static void ace30_dfl2usbpm_write(void *opaque, hwaddr addr, uint64_t val,
                                  unsigned size)
{
    struct adsp_io_info *info = opaque;

    qemu_log( "DfL2USBPM write: addr=0x%lx size=%u val=0x%lx\n",
            (unsigned long)addr, size, (unsigned long)val);

    write_lpsram_pm(info, addr, val, size);
}

const MemoryRegionOps ace30_dfl2usbpm_io_ops = {
    .read = ace30_dfl2usbpm_read,
    .write = ace30_dfl2usbpm_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

void adsp_scan_and_print_memory(Monitor *mon, hwaddr base, uint32_t size, int *tot_pages, int *tot_used,
                                int state, const char *type, int bank_id)
{
    int num_pages = size / 4096;
    int used = 0;
    uint32_t buf[1024]; /* 4KB buffer */
    g_autofree char *scan_str = g_malloc(num_pages + 1);
    int i, j;

    for (i = 0; i < num_pages; i++) {
        hwaddr page_addr = base + (i * 4096);
        int page_used_words = 0;

        /* Pre-fill with 0xFF in case address_space_read fails or partially reads */
        memset(buf, 0xff, sizeof(buf));
        address_space_read(&address_space_memory, page_addr, MEMTXATTRS_UNSPECIFIED,
                           buf, sizeof(buf));
        for (j = 0; j < 1024; j++) {
            if (buf[j] != 0xffffffff) {
                page_used_words++;
            }
        }
        
        if (page_used_words > 0) {
            int usage_hex = (page_used_words * 15) / 1024;
            if (usage_hex == 0) {
                usage_hex = 1; /* Make sure even a single used word shows up as at least '1' */
            }
            scan_str[i] = "0123456789abcdef"[usage_hex];
            used++;
        } else {
            scan_str[i] = '0';
        }
    }
    scan_str[num_pages] = '\0';
    
    if (used == 0 && state == SRAM_POWER_ON) {
        monitor_printf(mon, "    Usage: (%d/%d pages used) *** WARNING: ON BUT UNUSED ***\n", used, num_pages);
        qemu_log("WARNING: ADSP ACE %s SRAM Bank %d is POWER_ON but completely unused!\n", type, bank_id);
    } else {
        monitor_printf(mon, "    Usage: (%d/%d pages used)\n", used, num_pages);
    }

    for (i = 0; i < num_pages; i += 64) {
        int chunk_size = (num_pages - i > 64) ? 64 : (num_pages - i);
        monitor_printf(mon, "      0x%08lx: [%.*s]\n", (unsigned long)(base + i * 4096), chunk_size, &scan_str[i]);
    }
    
    if (tot_pages) *tot_pages += num_pages;
    if (tot_used) *tot_used += used;
}

void hmp_info_ace_sram(Monitor *mon, const QDict *qdict)
{
    bool scan = qdict_get_try_bool(qdict, "scan", false);
    int i;
    int hp_on = 0, hp_off = 0, hp_trans = 0;
    int lp_on = 0, lp_off = 0, lp_trans = 0;
    int hp_tot_pages = 0, hp_tot_used = 0;
    int lp_tot_pages = 0, lp_tot_used = 0;

    monitor_printf(mon, "Intel ADSP ACE HP SRAM Banks (%d total):\n", hpsram_bank_count);
    for (i = 0; i < hpsram_bank_count; i++) {
        SRAMBankPowerState *bank = &hpsram_banks[i];
        hwaddr base = hp_sram_base + (i * HPSRAM_BANK_SIZE);
        monitor_printf(mon, "  HP Bank %2d: Base 0x%08lx, Size 128KB, State: %-12s (PGCTL=0x%02x, RMCTL=0x%02x)\n",
                       i, (unsigned long)base, sram_state_name(bank->state), bank->pgctl, bank->rmctl);
        if (scan) {
            adsp_scan_and_print_memory(mon, base, HPSRAM_BANK_SIZE, &hp_tot_pages, &hp_tot_used,
                                bank->state, "HP", i);
        }
        if (bank->state == SRAM_POWER_ON) { hp_on++; }
        else if (bank->state == SRAM_POWER_OFF) { hp_off++; }
        else { hp_trans++; }
    }
    monitor_printf(mon, "  HP Summary: %d ON, %d OFF", hp_on, hp_off);
    if (hp_trans) { monitor_printf(mon, ", %d Transitioning", hp_trans); }
    if (scan && hp_tot_pages > 0) {
        monitor_printf(mon, ", Mem Usage: %d / %d pages used (%.1f%%)", hp_tot_used, hp_tot_pages, (float)hp_tot_used * 100.0f / hp_tot_pages);
    }
    monitor_printf(mon, "\n");

    monitor_printf(mon, "\nIntel ADSP ACE LP SRAM Banks (%d total):\n", lpsram_bank_count);
    for (i = 0; i < lpsram_bank_count; i++) {
        SRAMBankPowerState *bank = &lpsram_banks[i];
        hwaddr base = lp_sram_bank_base(i);
        monitor_printf(mon, "  LP Bank %2d: Base 0x%08lx, Size   8KB, State: %-12s (PGCTL=0x%02x, RMCTL=0x%02x)\n",
                       i, (unsigned long)base, sram_state_name(bank->state), bank->pgctl, bank->rmctl);
        if (scan) {
            adsp_scan_and_print_memory(mon, base, LPSRAM_BANK_SIZE, &lp_tot_pages, &lp_tot_used,
                                bank->state, "LP", i);
        }
        if (bank->state == SRAM_POWER_ON) { lp_on++; }
        else if (bank->state == SRAM_POWER_OFF) { lp_off++; }
        else { lp_trans++; }
    }
    monitor_printf(mon, "  LP Summary: %d ON, %d OFF", lp_on, lp_off);
    if (lp_trans) { monitor_printf(mon, ", %d Transitioning", lp_trans); }
    if (scan && lp_tot_pages > 0) {
        monitor_printf(mon, ", Mem Usage: %d / %d pages used (%.1f%%)", lp_tot_used, lp_tot_pages, (float)lp_tot_used * 100.0f / lp_tot_pages);
    }
    monitor_printf(mon, "\n");
}

