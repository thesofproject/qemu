/* Stub header for adsp-dev - minimal definitions for v5.2 compatibility */
#ifndef HW_AUDIO_ADSP_DEV_H
#define HW_AUDIO_ADSP_DEV_H

#include "qemu/osdep.h"
#include "hw/core/qdev.h"
#include "system/memory.h"
#include "qemu/main-loop.h"

#define ADSP_IO_MAX  256
#define ADSP_MAX_CORES 8

struct adsp_dev;
struct adsp_io_info;
struct adsp_reg_space;
struct adsp_mem_desc;
struct adsp_xtensa;  /* Forward declaration - defined in hw/xtensa/common.h */

typedef struct adsp_timer {
    void *timer;
    struct adsp_io_info *info;
    uint64_t clk_kHz;
    uint64_t start;
} adsp_timer;

struct adsp_dev_ops {
    void (*irq_set)(struct adsp_io_info *info, int irq, uint32_t mask);
    void (*irq_clear)(struct adsp_io_info *info, int irq, uint32_t mask);
};

struct adsp_io_info {
    const char *name;
    struct adsp_io_info *next;
    int num;
    struct adsp_dev *adsp;
    uint32_t *region;
    void *private;
    struct adsp_reg_space *space;
    const MemoryRegionOps *ops;
    void (*init)(struct adsp_dev *adsp, MemoryRegion *parent,
                 struct adsp_io_info *info);
};

struct adsp_reg_space {
    const char *name;
    uint32_t desc_size;
    struct {
        uint32_t base;
        uint32_t size;
    } desc;
    MemoryRegion mr;
    struct adsp_io_info *info;
    void (*init)(struct adsp_dev *adsp, MemoryRegion *parent,
                 struct adsp_io_info *info);
    const MemoryRegionOps *ops;
    int irq;
};

struct adsp_mem_desc {
    const char *name;
    uint32_t base;
    uint32_t size;
    void *ptr;
    uint32_t alias;
    MemoryRegion mr;
    bool per_core_non_coherent; /* if true, modelled as per-core non-coherent */
};

struct adsp_desc {
    const char *name;
    int ia_irq;
    int ext_timer_irq;
    uint32_t imr_boot_ldr_offset;
    uint32_t file_offset;
    int num_mem;
    struct adsp_mem_desc *mem_region;
    int num_io;
    struct adsp_reg_space *io_dev;
    struct {
        uint32_t base;
        uint32_t host_offset;
    } mem_zones[16];
    const struct adsp_dev_ops *ops;
};

struct adsp_dev {
    const struct adsp_desc *desc;
    struct adsp_xtensa *xtensa[ADSP_MAX_CORES];
    MemoryRegion *system_memory;
    void *machine_opts;
    const char *kernel_filename;
    const char *rom_filename;
    struct adsp_io_info *shim;
    struct adsp_io_info *ipc;   /* IPC block: hfipc0 on ACE30, ipc-low on ACE40 */
    uint32_t last_ipc4_reply;  /* last DSP->host idr value (reply payload, BUSY cleared) */
    struct adsp_io_info *dspcs;
    struct adsp_timer timer[2];
    struct adsp_io_info *dint[ADSP_MAX_CORES];
    int in_reset;
    int shm_idx;
    void *fw_manifest;
    size_t fw_manifest_size;
    const struct adsp_dev_ops *ops;
    uint32_t clk_kHz;
    struct adsp_hpsram_state *hpsram; /* per-core non-coherent HP-SRAM model */
    QemuThread mtrace_thread;
    bool mtrace_thread_running;
    bool mtrace_thread_stop;
    FILE *mtrace_file;
    char *mtrace_file_path;
    uint8_t *mtrace_slot;
    uint32_t mtrace_buf_size;
    unsigned mtrace_slot_index;
    Notifier mtrace_exit;
};

/*
 * Per-core non-coherent HP-SRAM model.
 *
 * 0xa0020000 (HP-SRAM): each core has its own shadow RAM.
 *   Writes go to the active core's shadow only (not visible to other cores).
 *   Cache writebacks propagate dirty lines from the shadow to the coherent store.
 *   Cache misses fill the shadow from the coherent store.
 *
 * 0x40020000 (coherent alias): shared RAM visible to all cores.
 *   Direct reads/writes always see the latest flushed state.
 */
typedef struct adsp_hpsram_state {
    uint8_t     *per_core[ADSP_MAX_CORES]; /* per-core shadow buffers */
    uint8_t     *coherent;                  /* shared coherent backing */
    uint32_t     base;                      /* HP-SRAM cached address */
    uint32_t     coherent_base;             /* HP-SRAM uncached/coherent address */
    uint32_t     size;
    int          num_cores;
    MemoryRegion coherent_mr;               /* RAM at coherent_base */
    MemoryRegion dispatcher_mr;             /* MMIO dispatcher at base */
} adsp_hpsram_state;

void adsp_set_lvl1_irq(struct adsp_dev *adsp, int irq, int active);
struct adsp_mem_desc *adsp_get_mem_space(struct adsp_dev *adsp, uint32_t addr);
uint32_t adsp_get_ext_man_size(const uint32_t *fw);
int adsp_load_modules(struct adsp_dev *adsp, void *fw_ptr, size_t size);
void adsp_create_memory_regions(struct adsp_dev *adsp);
void adsp_create_io_devices(struct adsp_dev *adsp, const MemoryRegionOps *ops);
void adsp_hpsram_setup(struct adsp_dev *adsp, struct adsp_mem_desc *mem,
                       uint32_t coherent_base, int num_cores);

#endif /* HW_AUDIO_ADSP_DEV_H */
