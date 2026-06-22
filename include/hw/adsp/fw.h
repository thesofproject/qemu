/* Stub header for adsp firmware - minimal definitions for v5.2 compatibility */
#ifndef HW_ADSP_FW_H
#define HW_ADSP_FW_H

#include "qemu/osdep.h"

/* Firmware block types */
#define SOF_FW_BLK_TYPE_INVALID	0xffffffff
#define SOF_FW_BLK_TYPE_START	0x0
#define SOF_FW_BLK_TYPE_RSRVD0	0x1
#define SOF_FW_BLK_TYPE_ROM	0x2
#define SOF_FW_BLK_TYPE_IMR	0x3
#define SOF_FW_BLK_TYPE_RSRVD6	0x6
#define SOF_FW_BLK_TYPE_IRAM	0x7
#define SOF_FW_BLK_TYPE_DRAM	0x8
#define SOF_FW_BLK_TYPE_SRAM	0x9

#define SND_SOF_FW_SIG_SIZE 4
#define SND_SOF_FW_SIG "$SST"

struct snd_sof_blk_hdr {
    uint32_t type;
    uint32_t size;
    uint32_t offset;
    uint32_t host_offset;
} __attribute__((packed));

struct snd_sof_mod_hdr {
    uint32_t num_blocks;
    uint32_t size;
    uint32_t type;
    struct snd_sof_blk_hdr blocks[];
} __attribute__((packed));

struct snd_sof_fw_header {
    char sig[SND_SOF_FW_SIG_SIZE];
    uint32_t file_size;
    uint32_t num_modules;
    uint32_t abi;
} __attribute__((packed));

#endif /* HW_ADSP_FW_H */
