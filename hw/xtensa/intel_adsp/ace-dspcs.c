/* ACE DSP core-shim register virtualization
 * IP Region: DSPCS per-core capability/control registers for power-state handshakes.
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
#include "ace-internal.h"

#define ace_region(raddr)    info->region[(raddr) >> 2]

/* from zephyr */
struct dspcs {
	/*
	 * DSPCSx
	 * DSP Core Shim
	 *
	 * These registers are added by Intel outside of the Tensilica Core for general operation
	 * control, such as reset, stall, power gating, clock gating etc.
	 * Note: These registers are accessible through the host space or DSP space depending on
	 * ownership, as governed by SAI and RS.
	 */
	struct {
		uint32_t cap;
		uint32_t ctl;
	} capctl[5];
	uint32_t unused0[6];

	/*
	 * DSPBRx
	 * DSP Boot / Recovery
	 *
	 * These registers are added by Intel outside of the Tensilica Core for boot / recovery
	 * control, such as boot path, watch dog timer etc.
	 */
	struct {
		uint32_t brcap;
		uint32_t wdtcs;
		uint32_t wdtipptr;
		uint32_t unused1;
		uint32_t bctl;
		uint32_t baddr;
		uint32_t battr;
		uint32_t unused2;
	} bootctl[5];
};

#define DSPCS_CTL_SPA BIT(0)
#define DSPCS_CTL_CPA BIT(8)

#define DSPBR_BCTL_BYPROM   BIT(0)
#define DSPBR_BCTL_WAITIPCG BIT(16)
#define DSPBR_BCTL_WAITIPPG BIT(17)

#define DSPBR_BATTR_LPSCTL_RESTORE_BOOT     BIT(12)
#define DSPBR_BATTR_LPSCTL_HP_CLOCK_BOOT    BIT(13)
#define DSPBR_BATTR_LPSCTL_LP_CLOCK_BOOT    BIT(14)
#define DSPBR_BATTR_LPSCTL_L1_MIN_WAY       BIT(15)
#define DSPBR_BATTR_LPSCTL_BATTR_SLAVE_CORE BIT(16)

#define DSPBR_WDT_RESUME          BIT(8)
#define DSPBR_WDT_RESTART_COMMAND 0x76
/* end from zephyr */

#define CXCAP_PGD_SHIFT     28
#define CXCAP_OSEL_SHIFT    27
#define CXCTL_CPA           (1 << 8)
#define CXCTL_SPA           (1 << 0)

void ace30_dspcs_init(struct adsp_dev *adsp, MemoryRegion *parent,
                        struct adsp_io_info *info)
{
    int num_cores = 0;
    int i;
    
    adsp->dspcs = info;

    for (i = 0; i < ARRAY_SIZE(adsp->xtensa); i++) {
        if (!adsp->xtensa[i]) {
            break;
        }
        num_cores++;
    }

    for (i = 0; i < num_cores; i++) {
        hwaddr ctl_addr = offsetof(struct dspcs, capctl[i].ctl);
        /* Reset each core control word so FW has to opt into power-active state. */
        ace_region(ctl_addr) = 0;
    }

    ace_log("DSPCS: Initialized at 0x17D000 for %d cores\n", num_cores);
}

static uint64_t ace30_dspcs_read(void *opaque, hwaddr addr, unsigned size)
{
    struct adsp_io_info *info = opaque;
    hwaddr aligned_addr = addr & ~3;
    uint32_t word = 0;
    uint32_t byte_offset = addr & 3;
    uint64_t val;
    uint32_t core_num = 0;

    if (aligned_addr < offsetof(struct dspcs, unused0)) {
        uint32_t stride = sizeof(((struct dspcs *)0)->capctl[0]);
        core_num = aligned_addr / stride;
        uint32_t reg_offset = aligned_addr % stride;

        if (reg_offset == offsetof(struct dspcs, capctl[0].cap)) {
            uint32_t pgd = (core_num == 0) ? 0 : 3;
            uint32_t osel = 1;
            uint32_t dspctype = 3;
            /* CXCAP advertises immutable core capabilities:
             * - PGD[31:28]: power-gating domain id
             * - OSEL[27]: oscillator selection capability
             * - DSPTYPE[2:0]: DSP class id expected by FW.
             */
            word = (pgd << CXCAP_PGD_SHIFT) |
                   (osel << CXCAP_OSEL_SHIFT) |
                   dspctype;
        } else if (reg_offset == offsetof(struct dspcs, capctl[0].ctl)) {
            word = ace_region(aligned_addr);
        }
    } else if (aligned_addr >= offsetof(struct dspcs, bootctl) &&
               aligned_addr < sizeof(struct dspcs)) {
        uint32_t stride = sizeof(((struct dspcs *)0)->bootctl[0]);
        uint32_t base = offsetof(struct dspcs, bootctl);
        core_num = (aligned_addr - base) / stride;
        word = ace_region(aligned_addr);
    } else {
        word = ace_region(aligned_addr);
    }

    if (!ace_extract_subword_read(word, size, byte_offset, &val)) {
        ace_log( "DSPCS read: unsupported size %u\n", size);
        val = 0;
    }

    if (aligned_addr < offsetof(struct dspcs, unused0)) {
        uint32_t stride = sizeof(((struct dspcs *)0)->capctl[0]);
        uint32_t reg_offset = aligned_addr % stride;
        
        if (reg_offset == offsetof(struct dspcs, capctl[0].ctl)) {
            ace_log("DSPCS: Core %u ctl read: val=0x%x (SPA=%u CPA=%u)\n",
                     core_num, word, !!(word & CXCTL_SPA), !!(word & CXCTL_CPA));
        }
    } else if (aligned_addr >= offsetof(struct dspcs, bootctl) &&
               aligned_addr < sizeof(struct dspcs)) {
        if (aligned_addr == offsetof(struct dspcs, bootctl[core_num].bctl)) {
            ace_log("DSPCS: Core %u bootctl.bctl read: val=0x%x (BYPROM=%u WAITIPCG=%u WAITIPPG=%u)\n",
                     core_num, word,
                     !!(word & DSPBR_BCTL_BYPROM),
                     !!(word & DSPBR_BCTL_WAITIPCG),
                     !!(word & DSPBR_BCTL_WAITIPPG));
        } else if (aligned_addr == offsetof(struct dspcs, bootctl[core_num].battr)) {
            ace_log("DSPCS: Core %u bootctl.battr read: val=0x%x (RESTORE_BOOT=%u HP_CLK_BOOT=%u LP_CLK_BOOT=%u L1_MIN_WAY=%u SLAVE_CORE=%u)\n",
                     core_num, word,
                     !!(word & DSPBR_BATTR_LPSCTL_RESTORE_BOOT),
                     !!(word & DSPBR_BATTR_LPSCTL_HP_CLOCK_BOOT),
                     !!(word & DSPBR_BATTR_LPSCTL_LP_CLOCK_BOOT),
                     !!(word & DSPBR_BATTR_LPSCTL_L1_MIN_WAY),
                     !!(word & DSPBR_BATTR_LPSCTL_BATTR_SLAVE_CORE));
        } else if (aligned_addr == offsetof(struct dspcs, bootctl[core_num].wdtcs)) {
            ace_log("DSPCS: Core %u bootctl.wdtcs read: val=0x%x (RESUME=%u)\n",
                     core_num, word, !!(word & DSPBR_WDT_RESUME));
        } else if (aligned_addr == offsetof(struct dspcs, bootctl[core_num].baddr)) {
            ace_log("DSPCS: Core %u bootctl.baddr read: val=0x%x\n",
                     core_num, word);
        } else if (aligned_addr == offsetof(struct dspcs, bootctl[core_num].wdtipptr)) {
            ace_log("DSPCS: Core %u bootctl.wdtipptr read: val=0x%x\n",
                     core_num, word);
        }
    }

    ace_log(
            "DSPCS read: addr=0x%lx size=%u val=0x%lx (byte_offset=%u)\n",
            (unsigned long)addr, size, (unsigned long)val, byte_offset);
    return val;
}

static void ace30_dspcs_write(void *opaque, hwaddr addr, uint64_t val,
                                unsigned size)
{
    struct adsp_io_info *info = opaque;
    hwaddr aligned_addr = addr & ~3;
    uint32_t byte_offset = addr & 3;
    uint32_t word;
    uint32_t final_val;
    uint32_t core_num = 0;

    ace_log(
            "DSPCS write: addr=0x%lx size=%u val=0x%lx (byte_offset=%u)\n",
            (unsigned long)addr, size, (unsigned long)val, byte_offset);

    if (aligned_addr < offsetof(struct dspcs, unused0)) {
        uint32_t stride = sizeof(((struct dspcs *)0)->capctl[0]);
        core_num = aligned_addr / stride;
        uint32_t reg_offset = aligned_addr % stride;

        if (reg_offset == offsetof(struct dspcs, capctl[0].cap)) {
            ace_log( "DSPCS write: read-only register at offset 0x%lx\n",
                    (unsigned long)aligned_addr);
            return;
        } else if (reg_offset == offsetof(struct dspcs, capctl[0].ctl)) {
            word = ace_region(aligned_addr);
            if (!ace_merge_subword_write(word, val, size, byte_offset, &word)) {
                ace_log( "DSPCS write: unsupported size %u\n", size);
                return;
            }
            final_val = word;

            if (final_val & CXCTL_SPA) {
                /* CXCTL.SPA is the FW request bit; model immediate acceptance by raising CXCTL.CPA. */
                final_val |= CXCTL_CPA;
                if (!(word & CXCTL_SPA)) {
                     /* Transitioning to SPA=1, turn on core */
                     ace_secondary_core_set_running(info->adsp, core_num, true, "DSPCS: capctl.ctl SPA=1");
                }
            } else {
                /* Clearing SPA drops the active-complete indication to emulate power-down handshake. */
                final_val &= ~CXCTL_CPA;
                if (word & CXCTL_SPA) {
                     /* Transitioning to SPA=0, turn off core */
                     ace_secondary_core_set_running(info->adsp, core_num, false, "DSPCS: capctl.ctl SPA=0");
                }
            }

            /* Store the synthesized control value that FW will read back and poll. */
            ace_region(aligned_addr) = final_val;

            ace_log( "DSPCS: Core %u %s (SPA=%u CPA=%u)\n",
                    core_num,
                    (final_val & CXCTL_SPA) ? "POWERED ON" : "POWERED OFF",
                    !!(final_val & CXCTL_SPA),
                    !!(final_val & CXCTL_CPA));
            return;
        }
    } else if (aligned_addr >= offsetof(struct dspcs, bootctl) &&
               aligned_addr < sizeof(struct dspcs)) {
        uint32_t stride = sizeof(((struct dspcs *)0)->bootctl[0]);
        uint32_t base = offsetof(struct dspcs, bootctl);
        core_num = (aligned_addr - base) / stride;

        word = ace_region(aligned_addr);
        if (!ace_merge_subword_write(word, val, size, byte_offset, &word)) {
            ace_log("DSPCS write: unsupported size %u\n", size);
            return;
        }
        ace_region(aligned_addr) = word;

        if (aligned_addr == offsetof(struct dspcs, bootctl[core_num].bctl)) {
            ace_log("DSPCS: Core %u bootctl.bctl write: val=0x%x (BYPROM=%u WAITIPCG=%u WAITIPPG=%u)\n",
                     core_num, word,
                     !!(word & DSPBR_BCTL_BYPROM),
                     !!(word & DSPBR_BCTL_WAITIPCG),
                     !!(word & DSPBR_BCTL_WAITIPPG));
        } else if (aligned_addr == offsetof(struct dspcs, bootctl[core_num].battr)) {
            ace_log("DSPCS: Core %u bootctl.battr write: val=0x%x (RESTORE_BOOT=%u HP_CLK_BOOT=%u LP_CLK_BOOT=%u L1_MIN_WAY=%u SLAVE_CORE=%u)\n",
                     core_num, word,
                     !!(word & DSPBR_BATTR_LPSCTL_RESTORE_BOOT),
                     !!(word & DSPBR_BATTR_LPSCTL_HP_CLOCK_BOOT),
                     !!(word & DSPBR_BATTR_LPSCTL_LP_CLOCK_BOOT),
                     !!(word & DSPBR_BATTR_LPSCTL_L1_MIN_WAY),
                     !!(word & DSPBR_BATTR_LPSCTL_BATTR_SLAVE_CORE));
        } else if (aligned_addr == offsetof(struct dspcs, bootctl[core_num].wdtcs)) {
            ace_log("DSPCS: Core %u bootctl.wdtcs write: val=0x%x (RESUME=%u)\n",
                     core_num, word, !!(word & DSPBR_WDT_RESUME));
        } else if (aligned_addr == offsetof(struct dspcs, bootctl[core_num].baddr)) {
            ace_log("DSPCS: Core %u bootctl.baddr write: val=0x%x\n",
                     core_num, word);
        } else if (aligned_addr == offsetof(struct dspcs, bootctl[core_num].wdtipptr)) {
            ace_log("DSPCS: Core %u bootctl.wdtipptr write: val=0x%x\n",
                     core_num, word);
        } else {
            ace_log("DSPCS: Core %u bootctl unknown write: addr=0x%lx val=0x%x\n",
                     core_num, (unsigned long)aligned_addr, word);
        }
        return;
    }

    word = ace_region(aligned_addr);
    if (!ace_merge_subword_write(word, val, size, byte_offset, &word)) {
        ace_log( "DSPCS write: unsupported size %u\n", size);
        return;
    }
    
    ace_region(aligned_addr) = word;
}

const MemoryRegionOps ace30_dspcs_io_ops = {
    .read = ace30_dspcs_read,
    .write = ace30_dspcs_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};
