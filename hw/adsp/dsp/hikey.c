/* Core DSP support HiKey960.
 *
 * Copyright (C) 2018 John Gunn
 *
 * Author: John Gunn <jgunn0262@gmail.com>
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
#include "qapi/error.h"
#include "qemu-common.h"
#include "qemu/timer.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/hw.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "hw/sysbus.h"
#include "qemu/error-report.h"
#include "qemu/io-bridge.h"

#include "hw/audio/adsp-dev.h"
#include "hw/adsp/shim.h"
#include "hw/adsp/log.h"

#include "hw/adsp/hikey.h"
#include "mbox.h"
#include "hikey.h"
#include "common.h"
#include "manifest.h"

#define MAX_IMAGE_SIZE (1024 * 1024 *4)

static void adsp_reset(void *opaque)
{
}

static uint64_t io_read(void *opaque, hwaddr addr,
        unsigned size)
{
    struct adsp_io_info *info = opaque;
    struct adsp_dev *adsp = info->adsp;
    struct adsp_reg_space *space = info->space;

    log_read(adsp->log, space, addr, size,
        info->region[addr >> 2]);

    return info->region[addr >> 2];
}

/* SHIM IO from ADSP */
static void io_write(void *opaque, hwaddr addr,
        uint64_t val, unsigned size)
{
    struct adsp_io_info *info = opaque;
    struct adsp_dev *adsp = info->adsp;
    struct adsp_reg_space *space = info->space;

    info->region[addr >> 2] = val;

    log_write(adsp->log, space, addr, val, size,
         info->region[addr >> 2]);
}

const MemoryRegionOps hikey_io_ops = {
    .read = io_read,
    .write = io_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void adsp_pm_msg(struct adsp_dev *adsp, struct qemu_io_msg *msg)
{
}

static int bridge_cb(void *data, struct qemu_io_msg *msg)
{
    struct adsp_dev *adsp = (struct adsp_dev *)data;

    switch (msg->type) {
    case QEMU_IO_TYPE_REG:
        /* mostly handled by SHM, some exceptions */
        //adsp_hikey_shim_msg(adsp, msg);
        break;
    case QEMU_IO_TYPE_IRQ:
        //adsp_hikey_irq_msg(adsp, msg);
        break;
    case QEMU_IO_TYPE_PM:
        adsp_pm_msg(adsp, msg);
        break;
    case QEMU_IO_TYPE_DMA:
       // dw_dma_msg(msg);
        break;
    case QEMU_IO_TYPE_MEM:
    default:
        break;
    }

    return 0;
}

enum DRV_HIFI_IMAGE_SEC_LOAD_ENUM {
    DRV_HIFI_IMAGE_SEC_LOAD_STATIC = 0,
    DRV_HIFI_IMAGE_SEC_LOAD_DYNAMIC,
    DRV_HIFI_IMAGE_SEC_UNLOAD,
    DRV_HIFI_IMAGE_SEC_UNINIT,
    DRV_HIFI_IMAGE_SEC_LOAD_BUTT,
};
typedef unsigned char DRV_HIFI_IMAGE_SEC_LOAD_ENUM_UINT8;

enum DRV_HIFI_IMAGE_SEC_TYPE_ENUM {
    DRV_HIFI_IMAGE_SEC_TYPE_CODE = 0,
    DRV_HIFI_IMAGE_SEC_TYPE_DATA,
    DRV_HIFI_IMAGE_SEC_TYPE_BSS,
    DRV_HIFI_IMAGE_SEC_TYPE_BUTT,
};
typedef unsigned char DRV_HIFI_IMAGE_SEC_TYPE_ENUM_UINT8;

struct drv_hifi_image_sec {
    unsigned short sn;
    DRV_HIFI_IMAGE_SEC_TYPE_ENUM_UINT8 type;
    DRV_HIFI_IMAGE_SEC_LOAD_ENUM_UINT8 load_attib;
    unsigned int src_offset;
    unsigned int des_addr;
    unsigned int size;
};

#define HIFI_SEC_MAX_NUM 64

struct drv_hifi_image_head {
    char time_stamp[24];
    unsigned int image_size;
    unsigned int sections_num;
    struct drv_hifi_image_sec sections[HIFI_SEC_MAX_NUM];
};

struct image_partition_table {
    unsigned long phy_addr_start;
    unsigned long phy_addr_end;
    unsigned int size;
    unsigned long remap_addr;
};

static void load_legacy_fmt(const struct adsp_desc *board,
                            struct adsp_dev *adsp, void *img_ptr)
{
    unsigned int i = 0;
    struct drv_hifi_image_head *hifi_img = NULL;
    unsigned long remap_dest_addr;
    struct adsp_mem_desc *mem;

    hifi_img = (struct drv_hifi_image_head *)img_ptr;
    printf("sections :%u, image_size:%u\n", hifi_img->sections_num,
         hifi_img->image_size);

    for (i = 0; i < hifi_img->sections_num; i++) {
        remap_dest_addr = 0;

        printf("section: %u\n", i);
        printf(" addr: 0x%x, load_attib:%u, size:%u, sn:%hu, src_offset:%x, type:%u\n",
            hifi_img->sections[i].des_addr, hifi_img->sections[i].load_attib,
            hifi_img->sections[i].size, hifi_img->sections[i].sn,
            hifi_img->sections[i].src_offset, hifi_img->sections[i].type);

        remap_dest_addr = (unsigned long)hifi_img->sections[i].des_addr;

        if (hifi_img->sections[i].type != DRV_HIFI_IMAGE_SEC_TYPE_BSS) {

            if (hifi_img->sections[i].load_attib ==
                (unsigned char)DRV_HIFI_IMAGE_SEC_UNLOAD) {
                printf("unload section\n");
                continue;
            }

             mem = adsp_get_mem_space(adsp, remap_dest_addr);
             if (!mem) {
                 printf("no mem for 0x%lx\n", remap_dest_addr);
                 continue;
             }
             printf("     mem %s base 0x%lx size 0x%lx\n", mem->name, mem->base, mem->size);
             printf("    copy dest off 0x%lx src off 0x%x size 0x%x\n", remap_dest_addr - mem->base,
                        hifi_img->sections[i].src_offset,
                        hifi_img->sections[i].size);
             memcpy(mem->ptr + (remap_dest_addr - mem->base), (void *)hifi_img +
                        hifi_img->sections[i].src_offset,
                        hifi_img->sections[i].size);
        } else {
            printf("   bss\n");
        }
    }

}

static struct adsp_dev *adsp_init(const struct adsp_desc *board,
    MachineState *machine, const char *name)
{
    struct adsp_dev *adsp;
    void *img_ptr;
    int n, size;
    uint32_t id;

    adsp = g_malloc(sizeof(*adsp));
    adsp->log = log_init(NULL);    /* TODO: add log name to cmd line */
    adsp->desc = board;
    adsp->shm_idx = 0;
    adsp->system_memory = get_system_memory();
    adsp->machine_opts = qemu_get_machine_opts();
    adsp->cpu_model = machine->cpu_model;
    adsp->kernel_filename = qemu_opt_get(adsp->machine_opts, "kernel");

    /* initialise CPU */
    if (!adsp->cpu_model) {
        adsp->cpu_model = XTENSA_DEFAULT_CPU_MODEL;
    }

    for (n = 0; n < smp_cpus; n++) {

        adsp->xtensa[n] = g_malloc(sizeof(struct adsp_xtensa));
        adsp->xtensa[n]->cpu = XTENSA_CPU(cpu_create(machine->cpu_type));

        if (adsp->xtensa[n]->cpu == NULL) {
            error_report("unable to find CPU definition '%s'",
                adsp->cpu_model);
            exit(EXIT_FAILURE);
        }

        adsp->xtensa[n]->env = &adsp->xtensa[n]->cpu->env;
        adsp->xtensa[n]->env->sregs[PRID] = n;

        qemu_register_reset(adsp_reset, adsp->xtensa[n]->cpu);

        /* Need MMU initialized prior to ELF loading,
        * so that ELF gets loaded into virtual addresses
        */
        cpu_reset(CPU(adsp->xtensa[n]->cpu));
    }

    adsp_create_memory_regions(adsp);
    adsp_create_io_devices(adsp, &hikey_io_ops);

    /* reset all devices to init state */
    qemu_devices_reset();

    /* initialise bridge to x86 host driver */
    qemu_io_register_child(name, &bridge_cb, (void*)adsp);

    /* load binary file if one is specified on cmd line otherwise finish */
    if (adsp->kernel_filename == NULL) {
        printf(" ** Hikey 960 Xtensa HiFi3 DSP initialised.\n"
            " ** Waiting for host to load firmware...\n");
        return adsp;
    }

    printf("now loading:\n kernel %s\n", adsp->kernel_filename);

    /* load the binary image and copy to SRAM */
    img_ptr = g_malloc(MAX_IMAGE_SIZE);
    size = load_image_size(adsp->kernel_filename, img_ptr,
        MAX_IMAGE_SIZE);

    id = *((uint32_t*)img_ptr);

    /* is file using legacy format or SOF format */
    if (id == 0x3a464948) {

         printf("Legacy Header 0x%x found\n", id);

         /* use lecagy fmt */
         load_legacy_fmt(board, adsp, img_ptr);

    } else {

        printf("SOF Header 0x%x\n", id);

        /* use SOF format */
        adsp_load_modules(adsp, img_ptr, size);
    }

    return adsp;
}

/* this option can be used to debug some memory spaces so that trace can be seen */
#define SPACE_TRACE     0

/* hikey devices */
static struct adsp_reg_space hikey_io[] = {
        {.name = "hifi-uart", .reg_count = 0, .reg = NULL,
            .desc = {.base = ADSP_HIKEY_HIFI_UART_BASE, .size = ADSP_HIKEY_HIFI_UART_SIZE},},
        {.name = "icc-debug", .reg_count = 0, .reg = NULL,
            .desc = {.base = ADSP_HIKEY_ICC_DEBUG_BASE, .size = ADSP_HIKEY_ICC_DEBUG_SIZE},},
        {.name = "ddr-sechead", .reg_count = 0, .reg = NULL,
            .desc = {.base = ADSP_HIKEY_DDR_SEC_HEAD_BASE, .size = ADSP_HIKEY_DDR_SEC_HEAD_SIZE},},
        {.name = "sound-trigger", .reg_count = 0, .reg = NULL,
            .desc = {.base = ADSP_HIKEY_SOUND_TRIGGER_BASE, .size = ADSP_HIKEY_SOUND_TRIGGER_SIZE},},
        {.name = "codec-dma-config", .reg_count = 0, .reg = NULL,
            .desc = {.base = ADSP_HIKEY_CODEC_DMA_CONF_BASE, .size = ADSP_HIKEY_CODEC_DMA_CONF_SIZE},},
#if SPACE_TRACE
        {.name = "music-data", .reg_count = 0, .reg = NULL,
            .desc = {.base = ADSP_HIKEY_MUSIC_DATA_BASE, .size = ADSP_HIKEY_MUSIC_DATA_SIZE},},
        {.name = "pcm-data", .reg_count = 0, .reg = NULL,
            .desc = {.base = ADSP_HIKEY_PCM_DATA_BASE, .size = ADSP_HIKEY_PCM_DATA_SIZE},},
        {.name = "panic-stack", .reg_count = 0, .reg = NULL,
            .desc = {.base = ADSP_HIKEY_PANIC_STACK_BASE, .size = ADSP_HIKEY_PANIC_STACK_SIZE},},
        {.name = "flag-data", .reg_count = 0, .reg = NULL,
            .desc = {.base = ADSP_HIKEY_FLAG_DATA_BASE, .size = ADSP_HIKEY_FLAG_DATA_SIZE},},
        {.name = "ap-nv", .reg_count = 0, .reg = NULL,
            .desc = {.base = ADSP_HIKEY_AP_NV_BASE, .size = ADSP_HIKEY_AP_NV_SIZE},},
        {.name = "ap-hifimb", .reg_count = 0, .reg = NULL,
            .desc = {.base = ADSP_HIKEY_AP_HIFIMB_BASE, .size = ADSP_HIKEY_AP_HIFIMB_SIZE},},
        {.name = "codec-dmabuf", .reg_count = 0, .reg = NULL,
            .desc = {.base = ADSP_HIKEY_CODEC_DMA_BUF_BASE, .size = ADSP_HIKEY_CODEC_DMA_BUF_SIZE},},
        {.name = "pcm-upload", .reg_count = 0, .reg = NULL,
            .desc = {.base = ADSP_HIKEY_PCM_UPLOAD_BASE, .size = ADSP_HIKEY_PCM_UPLOAD_SIZE},},
        {.name = "share", .reg_count = 0, .reg = NULL,
            .desc = {.base = ADSP_HIKEY_SHARE_BASE, .size = ADSP_HIKEY_SHARE_SIZE},},
        {.name = "unsec-rsvd", .reg_count = 0, .reg = NULL,
            .desc = {.base = ADSP_HIKEY_UNSEC_RSVD_BASE, .size = ADSP_HIKEY_UNSEC_RSVD_SIZE},},
        {.name = "hifi-run", .reg_count = 0, .reg = NULL,
            .desc = {.base = ADSP_HIKEY_HIFI_RUN_BASE, .size = ADSP_HIKEY_HIFI_RUN_SIZE},},
        {.name = "ocram-back", .reg_count = 0, .reg = NULL,
            .desc = {.base = ADSP_HIKEY_OCRAM_BACK_BASE, .size = ADSP_HIKEY_OCRAM_BACK_SIZE},},
        {.name = "tcm-back", .reg_count = 0, .reg = NULL,
            .desc = {.base = ADSP_HIKEY_TCM_BACK_BASE, .size = ADSP_HIKEY_TCM_BACK_SIZE},},
        {.name = "img-back", .reg_count = 0, .reg = NULL,
            .desc = {.base = ADSP_HIKEY_IMG_BACK_BASE, .size = ADSP_HIKEY_IMG_BACK_SIZE},},
#endif
};

static struct adsp_mem_desc hikey_mem[] = {
#if !SPACE_TRACE
    {.name = "music-data", .base = ADSP_HIKEY_MUSIC_DATA_BASE,
        .size = ADSP_HIKEY_MUSIC_DATA_SIZE},
    {.name = "pcm-data", .base = ADSP_HIKEY_PCM_DATA_BASE,
        .size = ADSP_HIKEY_PCM_DATA_SIZE,},
    {.name = "panic-stack", .base = ADSP_HIKEY_PANIC_STACK_BASE,
        .size = ADSP_HIKEY_PANIC_STACK_SIZE},
    {.name = "flag-data", .base = ADSP_HIKEY_FLAG_DATA_BASE,
        .size = ADSP_HIKEY_FLAG_DATA_SIZE},
    {.name = "ap-nv", .base = ADSP_HIKEY_AP_NV_BASE,
        .size = ADSP_HIKEY_AP_NV_SIZE},
    {.name = "ap-hifimb", .base = ADSP_HIKEY_AP_HIFIMB_BASE,
        .size = ADSP_HIKEY_AP_HIFIMB_SIZE},
    {.name = "codec-dmabuf", .base = ADSP_HIKEY_CODEC_DMA_BUF_BASE,
        .size = ADSP_HIKEY_CODEC_DMA_BUF_SIZE},
    {.name = "pcm-upload", .base = ADSP_HIKEY_PCM_UPLOAD_BASE,
        .size = ADSP_HIKEY_PCM_UPLOAD_SIZE},
    {.name = "share", .base = ADSP_HIKEY_SHARE_BASE,
        .size = ADSP_HIKEY_SHARE_SIZE},
    {.name = "unsec-rsvd", .base = ADSP_HIKEY_UNSEC_RSVD_BASE,
        .size = ADSP_HIKEY_UNSEC_RSVD_SIZE},
    {.name = "hifi-run", .base = ADSP_HIKEY_HIFI_RUN_BASE,
        .size = ADSP_HIKEY_HIFI_RUN_SIZE},
    {.name = "ocram-back", .base = ADSP_HIKEY_OCRAM_BACK_BASE,
        .size = ADSP_HIKEY_OCRAM_BACK_SIZE},
    {.name = "tcm-back", .base = ADSP_HIKEY_TCM_BACK_BASE,
        .size = ADSP_HIKEY_TCM_BACK_SIZE},
    {.name = "img-back", .base = ADSP_HIKEY_IMG_BACK_BASE,
        .size = ADSP_HIKEY_IMG_BACK_SIZE},
#endif
    {.name = "iram", .base = ADSP_HIKEY_HOST_RUN_ITCM_BASE,
        .size = ADSP_HIKEY_HOST_RUN_ITCM_SIZE},
    {.name = "dram", .base = ADSP_HIKEY_HOST_RUN_DTCM_BASE,
        .size = ADSP_HIKEY_HOST_RUN_DTCM_SIZE,},
    {.name = "imr", .base = ADSP_HIKEY_HOST_HIFI_RUN_BASE,
        .size = 1024 * 1024 * 16,},
};

/* Hikey 960 */
static const struct adsp_desc hikey_dsp_desc = {
    .ia_irq = IRQ_NUM_EXT_IPC,
    .ext_timer_irq = IRQ_NUM_TIMER1,

    .num_mem = ARRAY_SIZE(hikey_mem),
    .mem_region = hikey_mem,

    .num_io = ARRAY_SIZE(hikey_io),
    .io_dev = hikey_io,

    .iram_base = ADSP_HIKEY_HOST_RUN_ITCM_BASE,
    .dram_base = ADSP_HIKEY_HOST_RUN_DTCM_BASE,
};

static void hikey_adsp_init(MachineState *machine)
{
    struct adsp_dev *adsp;

    adsp = adsp_init(&hikey_dsp_desc, machine, "hikey");
    if (!adsp)
        printf("failed to init\n");

    //adsp->ext_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, &cavs_ext_timer_cb, adsp);
    //adsp->ext_clk_kHz = 2500;
}

static void xtensa_hikey960_machine_init(MachineClass *mc)
{
    mc->desc = "Hikey 960 HiFi3";
    mc->is_default = true;
    mc->init = hikey_adsp_init;
    mc->max_cpus = 1;
    mc->default_cpu_type = XTENSA_DEFAULT_CPU_TYPE;
}

DEFINE_MACHINE("adsp_hikey", xtensa_hikey960_machine_init)
