/* CAVS DMA register operations
 * IP Region: HDA stream DMA engines and general-purpose DMA controller windows.
 *
 * Copyright (C) 2026 Intel Corporation
 *
 * Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * ACE3.x DMA Architecture:
 * 
 * The ACE IP includes multiple DMA engines for efficient data movement:
 * 
 * 1. HD Audio Stream DMA:
 *    - Host DMA: Transfers between system memory (DDR) and L2 SRAM
 *      * Input streams: Capture audio from system to L2
 *      * Output streams: Playback audio from L2 to system
 *      * Stream count: HSTISC (input), HSTOSC (output)
 *    - Link DMA: Transfers between L2 SRAM and audio link interfaces
 *      * Supports HD Audio Link, SoundWire, USB Audio Offload
 *      * Stream-based with Buffer Descriptor List (BDL)
 *      * Position tracking via LPIB (Link Position In Buffer)
 *    - Coupled/Decoupled modes for flexible scheduling
 *    - FIFO buffering with configurable depths
 *    - Interrupt generation on completion (IOC)
 * 
 * 2. General Purpose DMA (GPDMA):
 *    - Up to 4 controllers (parameter GPDMACC)
 *    - Up to 8 channels per controller (GPDMACxCHC)
 *    - DesignWare DMAC architecture
 *    - Memory-to-memory and memory-to-peripheral transfers
 *    - Used for DSP I/O peripherals: I2C, UART, SPI, GPIO
 *    - Scatter-gather support via linked lists
 *    - Multi-master support (AHB/AXI)
 *    - Hardware handshaking for peripherals
 *    - Power domain awareness (gated-IOx domains)
 * 
 * DMA Programming Model:
 * 
 * HD Audio Stream DMA:
 *   1. Reset stream: Set SDxCTL.SRST=1, poll until ready
 *   2. Program BDL: Write BDPL/BDPU with buffer descriptor list address
 *   3. Program CBL: Set cyclic buffer length
 *   4. Program LVI: Set last valid index in BDL
 *   5. Set stream tag and format
 *   6. Enable interrupts: IOC, FIFO error, descriptor error
 *   7. Run stream: Set SDxCTL.RUN=1
 *   8. Monitor LPIB for position tracking
 *   9. Service interrupts on completion
 *   10. Stop: Clear SDxCTL.RUN=0
 * 
 * GPDMA:
 *   1. Configure channel: Set SAR, DAR, transfer size
 *   2. Set control: Transfer width, burst size, address increment
 *   3. Set configuration: Priority, flow control, handshaking
 *   4. Enable channel: Set CTL.CH_EN=1
 *   5. Monitor completion via interrupt or polling
 *   6. Disable channel: Clear CTL.CH_EN=0
 * 
 * Reference: ACE3.x IP HAS Section 8.5.2 "DMA Engines"
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "system/memory.h"
#include "system/address-spaces.h"
#include "hw/audio/adsp-dev.h"
#include "hw/adsp/shim.h"
#include "hw/adsp/ace.h"
#include "ace-internal.h"
#include "hw/dma/hda-dma.h"

/* Moved from ace.h */
#define HDA_STREAM_CTL_OFFSET(n)   (0x80 + (n) * 0x20)  /* Stream Control */
#define HDA_STREAM_STS_OFFSET(n)   (0x83 + (n) * 0x20)  /* Stream Status */
#define HDA_STREAM_LPIB_OFFSET(n)  (0x84 + (n) * 0x20)  /* Link Position */
#define HDA_STREAM_CBL_OFFSET(n)   (0x88 + (n) * 0x20)  /* Cyclic Buffer Length */
#define HDA_STREAM_LVI_OFFSET(n)   (0x8C + (n) * 0x20)  /* Last Valid Index */
#define HDA_STREAM_BDPL_OFFSET(n)  (0x98 + (n) * 0x20)  /* BDL Pointer Lower */
#define HDA_STREAM_BDPU_OFFSET(n)  (0x9C + (n) * 0x20)  /* BDL Pointer Upper */
#define HDA_CTL_STRM_RUN       (1 << 1)   /* Stream Run (1=run, 0=stop) */
#define HDA_CTL_STRM_RESET     (1 << 0)   /* Stream Reset */
#define HDA_CTL_IOC_ENABLE     (1 << 2)   /* Interrupt On Completion Enable */
#define HDA_CTL_STREAM_TAG_SHIFT 20       /* Stream Tag (bits 23:20) */
#define HDA_CTL_STREAM_TAG_MASK  (0xF << 20)
#define HDA_STS_FIFO_READY     (1 << 5)   /* FIFO Ready */
#define HDA_STS_DESC_ERROR     (1 << 4)   /* Descriptor Error */
#define HDA_STS_FIFO_ERROR     (1 << 3)   /* FIFO Error */
#define HDA_STS_BCIS           (1 << 2)   /* Buffer Completion Interrupt Status */
#define GPDMA_CTL_BASE(ctrl)       (0x71C00 + (ctrl) * 0x100)  /* GPDMA Control Base */
#define GPDMA_CTL_CH_EN        (1 << 0)   /* Channel Enable */
#define GPDMA_CFG_CH_PRIOR_SHIFT 5        /* Channel Priority */
#define GPDMA_CFG_CH_SUSP      (1 << 8)   /* Channel Suspend */
#define GPDMA_CFG_HS_SEL_DST   (1 << 10)  /* Destination Handshaking Select */
#define GPDMA_CFG_HS_SEL_SRC   (1 << 11)  /* Source Handshaking Select */

#define ace_region(raddr)    info->region[(raddr) >> 2]

#define HDA_MAX_STREAMS     16
#define HDA_MAX_BDL_ENTRIES 256

/*
 * DMA simulation timing.
 * Real hardware: 48 frames per 1ms (48kHz).
 * Simulation: run ~100x slower → one period every 100ms.
 */
#define DMA_SIM_PERIOD_MS   100

/*
 * HDA BDL (Buffer Descriptor List) entry layout.
 * Each entry is 16 bytes; address is physical, size is in bytes.
 */
typedef struct {
    uint32_t addr_lo;
    uint32_t addr_hi;
    uint32_t size;
    uint32_t ioc;   /* bit 0: interrupt on completion */
} QEMU_PACKED HDABdlEntry;

/*
 * Per-stream file backing state.
 * QEMU_HDA_IN_<n>  = path of file to read from (file → DSP, e.g. capture source)
 * QEMU_HDA_OUT_<n> = path of file to write to  (DSP → file, e.g. playback sink)
 */
typedef struct {
    FILE   *in_file;   /* source: file → DSP buffer */
    FILE   *out_file;  /* sink:   DSP buffer → file */
    char   *in_path;
    char   *out_path;
} HDAStreamFile;

/*
 * Per-stream periodic DMA state.
 * Drives continuous streaming by advancing one BDL entry per timer tick.
 */
typedef struct {
    QEMUTimer  *timer;
    struct adsp_io_info *info;
    unsigned    stream_num;
    uint32_t    current_bdl_idx;  /* next BDL entry to process */
    bool        running;
} HDAStreamPeriodic;

static HDAStreamFile hda_stream_files[HDA_MAX_STREAMS];
static HDAStreamPeriodic hda_stream_periodic[HDA_MAX_STREAMS];

/* Not prototyped by headers pulled into this file. */
extern void qemu_add_exit_notifier(Notifier *notify);
extern void qemu_remove_exit_notifier(Notifier *notify);

static void hda_stream_files_cleanup(Notifier *notifier, void *data)
{
    unsigned i;

    for (i = 0; i < HDA_MAX_STREAMS; i++) {
        if (hda_stream_files[i].in_file) {
            fclose(hda_stream_files[i].in_file);
            hda_stream_files[i].in_file = NULL;
        }
        if (hda_stream_files[i].out_file) {
            fclose(hda_stream_files[i].out_file);
            hda_stream_files[i].out_file = NULL;
        }
        g_clear_pointer(&hda_stream_files[i].in_path, g_free);
        g_clear_pointer(&hda_stream_files[i].out_path, g_free);
    }
    qemu_remove_exit_notifier(notifier);
}

static Notifier hda_file_exit_notifier;

static void hda_stream_files_init(void)
{
    char env_name[32];
    const char *path;
    bool any = false;
    unsigned i;

    for (i = 0; i < HDA_MAX_STREAMS; i++) {
        snprintf(env_name, sizeof(env_name), "QEMU_HDA_IN_%u", i);
        path = g_getenv(env_name);
        if (path && *path) {
            hda_stream_files[i].in_path = g_strdup(path);
            hda_stream_files[i].in_file = fopen(path, "rb");
            if (!hda_stream_files[i].in_file) {
                error_report("HDA stream %u: cannot open input file '%s': %s",
                             i, path, strerror(errno));
            } else {
                ace_log("HDA Stream %u: DMA input backed by %s\n", i, path);
                any = true;
            }
        }

        snprintf(env_name, sizeof(env_name), "QEMU_HDA_OUT_%u", i);
        path = g_getenv(env_name);
        if (path && *path) {
            hda_stream_files[i].out_path = g_strdup(path);
            hda_stream_files[i].out_file = fopen(path, "wb");
            if (!hda_stream_files[i].out_file) {
                error_report("HDA stream %u: cannot open output file '%s': %s",
                             i, path, strerror(errno));
            } else {
                ace_log("HDA Stream %u: DMA output backed by %s\n", i, path);
                any = true;
            }
        }
    }

    if (any) {
        hda_file_exit_notifier.notify = hda_stream_files_cleanup;
        qemu_add_exit_notifier(&hda_file_exit_notifier);
    }
}

/*
 * Execute a file-backed DMA transfer for stream @stream_num.
 *
 * Called when the firmware sets RUN=1.  Walks all BDL entries [0..LVI],
 * reads/writes each buffer through the physical address space, then
 * updates LPIB and sets BCIS so firmware sees the completion.
 */
static void hda_stream_xfer(struct adsp_io_info *info, unsigned stream_num)
{
    HDAStreamFile *sf = &hda_stream_files[stream_num];
    HDABdlEntry entry;
    uint64_t bdl_addr, buf_addr;
    uint32_t lvi, cbl, total;
    uint8_t *buf;
    unsigned i;

    if (!sf->in_file && !sf->out_file) {
        cbl  = ace_region(HDA_STREAM_CBL_OFFSET(stream_num));
        ace_region(HDA_STREAM_LPIB_OFFSET(stream_num)) = cbl;
        ace_region(HDA_STREAM_STS_OFFSET(stream_num))  |= HDA_STS_BCIS;
        return;
    }

    bdl_addr = ((uint64_t)ace_region(HDA_STREAM_BDPU_OFFSET(stream_num)) << 32)
               | ace_region(HDA_STREAM_BDPL_OFFSET(stream_num));
    lvi  = ace_region(HDA_STREAM_LVI_OFFSET(stream_num)) & 0xFF;
    cbl  = ace_region(HDA_STREAM_CBL_OFFSET(stream_num));

    if (!bdl_addr || !cbl) {
        ace_log("HDA Stream %u: xfer skipped — bdl=0x%016" PRIx64 " cbl=%u\n",
                 stream_num, bdl_addr, cbl);
        return;
    }

    total = 0;
    for (i = 0; i <= lvi && i < HDA_MAX_BDL_ENTRIES; i++) {
        address_space_read(&address_space_memory,
                           bdl_addr + i * sizeof(entry),
                           MEMTXATTRS_UNSPECIFIED,
                           &entry, sizeof(entry));

        buf_addr = ((uint64_t)le32_to_cpu(entry.addr_hi) << 32)
                   | le32_to_cpu(entry.addr_lo);
        uint32_t buf_size = le32_to_cpu(entry.size);

        if (!buf_addr || !buf_size) {
            continue;
        }

        buf = g_malloc(buf_size);

        if (sf->in_file) {
            /* file → DSP buffer */
            size_t n = fread(buf, 1, buf_size, sf->in_file);
            if (n < buf_size) {
                memset(buf + n, 0, buf_size - n);
                if (feof(sf->in_file)) {
                    rewind(sf->in_file);
                }
            }
            address_space_write(&address_space_memory, buf_addr,
                                MEMTXATTRS_UNSPECIFIED, buf, buf_size);
            ace_log("HDA Stream %u BDL[%u]: in  file → 0x%016" PRIx64
                     " (%u bytes)\n",
                     stream_num, i, buf_addr, buf_size);
        }

        if (sf->out_file) {
            /* DSP buffer → file */
            address_space_read(&address_space_memory, buf_addr,
                               MEMTXATTRS_UNSPECIFIED, buf, buf_size);
            fwrite(buf, 1, buf_size, sf->out_file);
            fflush(sf->out_file);
            ace_log("HDA Stream %u BDL[%u]: out 0x%016" PRIx64
                     " → file (%u bytes)\n",
                     stream_num, i, buf_addr, buf_size);
        }

        g_free(buf);
        total += buf_size;
    }

    /* Advance LPIB and signal buffer completion to firmware. */
    ace_region(HDA_STREAM_LPIB_OFFSET(stream_num)) = total ? total : cbl;
    ace_region(HDA_STREAM_STS_OFFSET(stream_num))  |= HDA_STS_BCIS;
}

/*
 * Periodic DMA timer callback.
 * Transfers one BDL entry per tick (~100ms), updates LPIB, sets BCIS on wrap.
 * Simulates 48 frames/1ms at 100x slower rate.
 */
static void hda_stream_period_cb(void *opaque)
{
    HDAStreamPeriodic *sp = opaque;
    struct adsp_io_info *info = sp->info;
    unsigned stream_num = sp->stream_num;
    HDAStreamFile *sf = &hda_stream_files[stream_num];
    HDABdlEntry entry;
    uint64_t bdl_addr, buf_addr;
    uint32_t lvi, cbl, lpib;
    uint8_t *buf;

    if (!sp->running) {
        return;
    }

    bdl_addr = ((uint64_t)ace_region(HDA_STREAM_BDPU_OFFSET(stream_num)) << 32)
               | ace_region(HDA_STREAM_BDPL_OFFSET(stream_num));
    lvi  = ace_region(HDA_STREAM_LVI_OFFSET(stream_num)) & 0xFF;
    cbl  = ace_region(HDA_STREAM_CBL_OFFSET(stream_num));

    if (!bdl_addr || !cbl) {
        /* No buffer configured — fake completion for firmware. */
        ace_region(HDA_STREAM_STS_OFFSET(stream_num)) |= HDA_STS_BCIS;
        timer_mod(sp->timer, qemu_clock_get_ms(QEMU_CLOCK_REALTIME)
                  + DMA_SIM_PERIOD_MS);
        return;
    }

    /* Read the current BDL entry. */
    address_space_read(&address_space_memory,
                       bdl_addr + sp->current_bdl_idx * sizeof(entry),
                       MEMTXATTRS_UNSPECIFIED, &entry, sizeof(entry));

    buf_addr = ((uint64_t)le32_to_cpu(entry.addr_hi) << 32)
               | le32_to_cpu(entry.addr_lo);
    uint32_t buf_size = le32_to_cpu(entry.size);

    if (buf_addr && buf_size) {
        buf = g_malloc(buf_size);

        if (sf->in_file) {
            size_t n = fread(buf, 1, buf_size, sf->in_file);
            if (n < buf_size) {
                memset(buf + n, 0, buf_size - n);
                if (feof(sf->in_file)) {
                    rewind(sf->in_file);
                }
            }
            address_space_write(&address_space_memory, buf_addr,
                                MEMTXATTRS_UNSPECIFIED, buf, buf_size);
        }

        if (sf->out_file) {
            address_space_read(&address_space_memory, buf_addr,
                               MEMTXATTRS_UNSPECIFIED, buf, buf_size);
            fwrite(buf, 1, buf_size, sf->out_file);
            fflush(sf->out_file);
        }

        g_free(buf);
    }

    /* Update LPIB — advance within the cyclic buffer. */
    lpib = ace_region(HDA_STREAM_LPIB_OFFSET(stream_num));
    lpib += buf_size;
    if (lpib >= cbl) {
        lpib = 0;
    }
    ace_region(HDA_STREAM_LPIB_OFFSET(stream_num)) = lpib;

    /* Advance BDL index; set BCIS when we complete the last valid entry. */
    if (sp->current_bdl_idx >= lvi) {
        sp->current_bdl_idx = 0;
        ace_region(HDA_STREAM_STS_OFFSET(stream_num)) |= HDA_STS_BCIS;
    } else {
        sp->current_bdl_idx++;
    }

    /* Reschedule for next period. */
    timer_mod(sp->timer, qemu_clock_get_ms(QEMU_CLOCK_REALTIME)
              + DMA_SIM_PERIOD_MS);
}

/* Start periodic DMA on a stream (called when RUN transitions 0→1). */
static void hda_stream_start_periodic(struct adsp_io_info *info,
                                      unsigned stream_num)
{
    HDAStreamPeriodic *sp = &hda_stream_periodic[stream_num];
    HDAStreamFile *sf = &hda_stream_files[stream_num];

    /* If no file backing, fall back to one-shot transfer. */
    if (!sf->in_file && !sf->out_file) {
        hda_stream_xfer(info, stream_num);
        return;
    }

    sp->info = info;
    sp->stream_num = stream_num;
    sp->current_bdl_idx = 0;
    sp->running = true;

    if (!sp->timer) {
        sp->timer = timer_new_ms(QEMU_CLOCK_REALTIME,
                                 hda_stream_period_cb, sp);
    }

    ace_log("HDA Stream %u: starting periodic DMA (%d ms/period)\n",
            stream_num, DMA_SIM_PERIOD_MS);

    timer_mod(sp->timer, qemu_clock_get_ms(QEMU_CLOCK_REALTIME)
              + DMA_SIM_PERIOD_MS);
}

/* Stop periodic DMA on a stream (called when RUN transitions 1→0). */
static void hda_stream_stop_periodic(unsigned stream_num)
{
    HDAStreamPeriodic *sp = &hda_stream_periodic[stream_num];

    sp->running = false;
    if (sp->timer) {
        timer_del(sp->timer);
    }
    ace_log("HDA Stream %u: stopped periodic DMA\n", stream_num);
}

/* DMA channel state */
typedef struct {
    bool enabled;
    uint64_t src_addr;
    uint64_t dst_addr;
    uint32_t transfer_size;
    uint32_t position;
    uint32_t control;
    uint32_t config;
} GPDMAChannel;

/* HD Audio Stream state */
typedef struct {
    bool running;
    bool reset;
    uint32_t ctl;
    uint32_t sts;
    uint32_t lpib;          /* Link Position In Buffer */
    uint32_t cbl;           /* Cyclic Buffer Length */
    uint32_t lvi;           /* Last Valid Index */
    uint64_t bdl_addr;      /* Buffer Descriptor List address */
    uint32_t stream_tag;
} HDAStream;

/* HD Audio Stream DMA read handler */
uint64_t ace_hda_stream_read(void *opaque, hwaddr addr, unsigned size)
{
    struct adsp_io_info *info = opaque;
    uint32_t stream_num = (addr - HDA_STREAM_CTL_OFFSET(0)) / 0x20;
    uint32_t reg_offset = addr % 0x20;
    uint32_t value = ace_region(addr);
    char status_buf[256];

    /* Limit stream number to reasonable range */
    if (stream_num >= 16) {
        stream_num = 0;
    }

    switch (reg_offset) {
    case 0x00: /* SDxCTL - Stream Control */
        snprintf(status_buf, sizeof(status_buf), "RUN=%d RST=%d IOC=%d TAG=%d",
                 !!(value & HDA_CTL_STRM_RUN),
                 !!(value & HDA_CTL_STRM_RESET),
                 !!(value & HDA_CTL_IOC_ENABLE),
                 (value & HDA_CTL_STREAM_TAG_MASK) >> HDA_CTL_STREAM_TAG_SHIFT);
        /* TODO: trace_adsp_dsp_hda_stream_ctl_read(stream_num, value, status_buf); */
        /* Trace format mapping needs explicit synchronization against %d bounds */
        ace_log("ace: read :HDA Stream %d CTL@0x%lx: %u: 0x%08x (%s)\n", stream_num, (unsigned long)addr, size, value, status_buf);
        break;
    case 0x03: /* SDxSTS - Stream Status */
        snprintf(status_buf, sizeof(status_buf), "FIFO_RDY=%d DESC_ERR=%d FIFO_ERR=%d BCIS=%d",
                 !!(value & HDA_STS_FIFO_READY),
                 !!(value & HDA_STS_DESC_ERROR),
                 !!(value & HDA_STS_FIFO_ERROR),
                 !!(value & HDA_STS_BCIS));
        /* TODO: trace_adsp_dsp_hda_stream_sts_read(stream_num, value, status_buf); */
        ace_log("ace: read :HDA Stream %d STS@0x%lx: %u: 0x%08x (%s)\n", stream_num, (unsigned long)addr, size, value, status_buf);
        break;
    case 0x04: /* SDxLPIB - Link Position In Buffer */
        /* TODO: trace_adsp_dsp_hda_stream_pos_read(stream_num, value, ace_region(addr + 4)); */
        ace_log( "HDA Stream %d: LPIB=0x%08x CBL=0x%08x\n",
                stream_num, value, ace_region((addr + 4)));
        break;
    default:
        break;
    }

    return value;
}

/* HD Audio Stream DMA write handler */
void ace_hda_stream_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    struct adsp_io_info *info = opaque;
    uint32_t stream_num = (addr - HDA_STREAM_CTL_OFFSET(0)) / 0x20;
    uint32_t reg_offset = addr % 0x20;
    uint32_t old_value = ace_region(addr);
    char action_buf[256];

    /* Limit stream number to reasonable range */
    if (stream_num >= 16) {
        stream_num = 0;
    }

    switch (reg_offset) {
    case 0x00: /* SDxCTL - Stream Control */
        if ((val & HDA_CTL_STRM_RESET) && !(old_value & HDA_CTL_STRM_RESET)) {
            snprintf(action_buf, sizeof(action_buf), "RESET stream");
            /* Reset stream state */
            /* Reset reports FIFO ready again so FW sees the channel idle and able to accept setup. */
            ace_region((addr + 0x03)) = HDA_STS_FIFO_READY; /* Set FIFO ready */
            /* Reset also rewinds the link position counter to the start of the buffer. */
            ace_region((addr + 0x04)) = 0; /* Clear LPIB */
        } else if ((val & HDA_CTL_STRM_RUN) && !(old_value & HDA_CTL_STRM_RUN)) {
            snprintf(action_buf, sizeof(action_buf), "START stream tag=%d",
                     (uint32_t)((val & HDA_CTL_STREAM_TAG_MASK) >> HDA_CTL_STREAM_TAG_SHIFT));
            /* Preserve CTL before the file transfer reads BDL/CBL/LVI registers. */
            ace_region(addr) = val;
            hda_stream_start_periodic(info, stream_num);
            break;
        } else if (!(val & HDA_CTL_STRM_RUN) && (old_value & HDA_CTL_STRM_RUN)) {
            snprintf(action_buf, sizeof(action_buf), "STOP stream");
            hda_stream_stop_periodic(stream_num);
        } else {
            snprintf(action_buf, sizeof(action_buf), "CONFIG update");
        }
        /* TODO: trace_adsp_dsp_hda_stream_ctl_write(stream_num, (uint32_t)val, action_buf); */
        ace_log("ace: write :HDA Stream %d CTL@0x%lx: %u: 0x%08x (%s)\n", stream_num, (unsigned long)addr, size, (uint32_t)val, action_buf);
        /* Preserve the full stream-control word so later state changes can inspect RUN/RESET/tag.
         * (RUN=1 start path does this early and breaks, so only non-RUN paths reach here.) */
        ace_region(addr) = val;
        break;
    
    case 0x03: /* SDxSTS - Stream Status (write 1 to clear) */
        /* Status bits are W1C in HDA stream state, so clear only the bits FW acknowledges. */
        ace_region(addr) &= ~val; /* W1C - Write 1 to Clear */
        break;
    
    case 0x08: /* SDxCBL - Cyclic Buffer Length */
        /* CBL defines the total cyclic-buffer span that LPIB advances through. */
        ace_region(addr) = val;
        ace_log( "HDA Stream %d: CBL=0x%08x (buffer size)\n",
                stream_num, (uint32_t)val);
        break;
    
    case 0x0C: /* SDxLVI - Last Valid Index */
        /* LVI is only 8 bits architecturally; higher bits are discarded. */
        ace_region(addr) = val & 0xFF;
        ace_log( "HDA Stream %d: LVI=%d (BDL entries)\n",
                stream_num, (uint32_t)(val & 0xFF));
        break;
    
    case 0x18: /* SDxBDPL - Buffer Descriptor List Pointer Lower */
    case 0x1C: /* SDxBDPU - Buffer Descriptor List Pointer Upper */
        /* Preserve both halves of the BDL pointer so FW can program the 64-bit descriptor base. */
        ace_region(addr) = val;
        if (reg_offset == 0x1C) {
            uint64_t bdl_addr = ((uint64_t)val << 32) | ace_region((addr - 4));
            /* TODO: trace_adsp_dsp_hda_bdl_write(stream_num, bdl_addr, ace_region(addr - 0x10) & 0xFF); */
            ace_log( "HDA Stream %d: BDL addr=0x%016llx entries=%d\n",
                    stream_num, (unsigned long long)bdl_addr,
                    (int)(ace_region((addr - 0x10)) & 0xFF));
        }
        break;
    
    default:
        /* Unmodelled HDA stream registers still retain their programmed values for FW readback. */
        ace_region(addr) = val;
        break;
    }
}

/* GPDMA channel read handler */
uint64_t ace_gpdma_read(void *opaque, hwaddr addr, unsigned size)
{
    struct adsp_io_info *info = opaque;
    uint32_t value = ace_region(addr);
    
    /* Log GPDMA accesses for debugging */
    if ((addr & 0xF) == 0) { /* Log only on major register boundaries */
        ace_log( "GPDMA: read 0x%04llx = 0x%08x\n", 
                (unsigned long long)addr, value);
    }
    
    return value;
}

/* GPDMA channel write handler */
void ace_gpdma_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    struct adsp_io_info *info = opaque;
    uint32_t old_value = ace_region(addr);
    
    /* Decode controller and channel from address */
    uint32_t ctrl_offset = (addr - GPDMA_CTL_BASE(0));
    uint32_t controller = ctrl_offset / 0x100;
    uint32_t ch_offset = ctrl_offset % 0x100;
    uint32_t channel = 0;
    char config_buf[512];
    
    /* Determine channel number from offset */
    if (ch_offset < 0x100) {
        channel = (ch_offset & 0xFF) / 0x20;
        if (channel >= 8) channel = 0;
    }
    
    /* Default GPDMA behavior is to retain the programmer-visible register contents. */
    ace_region(addr) = val;
    
    /* Decode register type and log appropriately */
    if ((ch_offset & 0x1F) == 0x10) { /* CTL_LO register */
        bool enabling = (val & GPDMA_CTL_CH_EN) && !(old_value & GPDMA_CTL_CH_EN);
        bool disabling = !(val & GPDMA_CTL_CH_EN) && (old_value & GPDMA_CTL_CH_EN);
        
        if (enabling) {
            uint64_t src = ace_region((addr - 0x10)) | 
                          ((uint64_t)ace_region((addr - 0x0C)) << 32);
            uint64_t dst = ace_region((addr - 0x08)) |
                          ((uint64_t)ace_region((addr - 0x04)) << 32);
            uint32_t len = val >> 20; /* Transfer size in upper bits */
            /* TODO: trace_adsp_dsp_gpdma_ch_enable(controller, channel, src, dst, len); */
            ace_log( "GPDMA%d CH%d: ENABLE src=0x%llx dst=0x%llx len=%d\n",
                    controller, channel, 
                    (unsigned long long)src, (unsigned long long)dst, len);
        } else if (disabling) {
            /* TODO: trace_adsp_dsp_gpdma_ch_disable(controller, channel); */
            ace_log( "GPDMA%d CH%d: DISABLE\n", controller, channel);
        }
    } else if ((ch_offset & 0x1F) == 0x00 || (ch_offset & 0x1F) == 0x08) {
        /* Source/Dest address - just store, log on CTL write */
    } else if (ch_offset >= 0x40 && ch_offset < 0x80) { /* CFG registers */
        uint32_t cfg_ch = (ch_offset - 0x40) / 0x08;
        snprintf(config_buf, sizeof(config_buf),
                 "priority=%d suspend=%d hs_dst=%d hs_src=%d",
                 (uint32_t)((val >> GPDMA_CFG_CH_PRIOR_SHIFT) & 0x7),
                 !!((uint32_t)val & GPDMA_CFG_CH_SUSP),
                 !!((uint32_t)val & GPDMA_CFG_HS_SEL_DST),
                 !!((uint32_t)val & GPDMA_CFG_HS_SEL_SRC));
        /* TODO: trace_adsp_dsp_gpdma_config(controller, cfg_ch, config_buf); */
        ace_log( "GPDMA%d CH%d: CONFIG %s\n",
                controller, cfg_ch, config_buf);
    }
}

/* Memory region operations for HDA Stream DMA */
const MemoryRegionOps ace_hda_stream_ops = {
    .read = ace_hda_stream_read,
    .write = ace_hda_stream_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

/* Memory region operations for GPDMA */
const MemoryRegionOps ace_gpdma_ops = {
    .read = ace_gpdma_read,
    .write = ace_gpdma_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

/* =========================================================================
 * DSP-side HDA Gateway Register Emulation (DGCS/DGBS/DGBWP/DGBRP)
 *
 * The firmware accesses gateway registers at:
 *   hda_host_out: 0x72800, 0x40 per stream (playback: host → DSP)
 *   hda_host_in:  0x72c00, 0x40 per stream (capture: DSP → host)
 *   hda_link_out: 0x79400, 0x40 per stream
 *   hda_link_in:  0x79800, 0x40 per stream
 *
 * Register layout per stream (0x40 bytes):
 *   +0x00 DGCS   - Gateway Channel Status (GEN, FIFORDY, BNE, etc.)
 *   +0x04 DGBBA  - Gateway Buffer Base Address
 *   +0x08 DGBS   - Gateway Buffer Size
 *   +0x0C DGBFPI - Buffer Fragment Position Increment
 *   +0x10 DGBRP  - Buffer Read Position
 *   +0x14 DGBWP  - Buffer Write Position
 *   +0x18 DGBSP  - Buffer Segment Position
 *   +0x1C DGMBS  - Minimum Buffer Size
 *   +0x20 DGLLPI - Linear Link Position (low)
 *   +0x24 DGLLPU - Linear Link Position (high)
 * ========================================================================= */

#define GTW_REG_SIZE    0x40    /* bytes per stream */
#define GTW_DGCS        0x00
#define GTW_DGBBA       0x04
#define GTW_DGBS        0x08
#define GTW_DGBFPI      0x0C
#define GTW_DGBRP       0x10
#define GTW_DGBWP       0x14
#define GTW_DGBSP       0x18
#define GTW_DGMBS       0x1C
#define GTW_DGLLPI      0x20
#define GTW_DGLLPU      0x24

#define GTW_DGCS_GEN     (1 << 26)
#define GTW_DGCS_FIFORDY (1 << 5)
#define GTW_DGCS_BNE     (1 << 8)
#define GTW_DGCS_BSC     (1 << 11)
#define GTW_DGCS_FWCB    (1 << 23)

#define GTW_MAX_STREAMS  16

/*
 * Gateway DMA direction — determines how the periodic timer simulates data.
 * HOST_OUT (playback): QEMU advances DGBWP (host writes data for DSP to read)
 * HOST_IN (capture):   QEMU advances DGBRP (host reads data that DSP wrote)
 */
typedef enum {
    GTW_DIR_HOST_OUT = 0,   /* playback: file → DSP buffer (advance WP) */
    GTW_DIR_HOST_IN,        /* capture:  DSP buffer → file (advance RP) */
    GTW_DIR_LINK_OUT,       /* link playback */
    GTW_DIR_LINK_IN,        /* link capture */
} GtwDirection;

typedef struct {
    QEMUTimer *timer;
    struct adsp_io_info *info;
    unsigned stream_num;
    GtwDirection direction;
    bool running;
} GtwStreamPeriodic;

static GtwStreamPeriodic gtw_periodic[4][GTW_MAX_STREAMS]; /* [dir][stream] */

static void gtw_period_cb(void *opaque)
{
    GtwStreamPeriodic *gp = opaque;
    struct adsp_io_info *info = gp->info;
    unsigned sn = gp->stream_num;
    uint32_t base_off = sn * GTW_REG_SIZE;

    if (!gp->running)
        return;

    uint32_t dgbs = ace_region(base_off + GTW_DGBS);

    ace_log("GTW period_cb: dir=%d sn=%u dgbs=%u\n",
            gp->direction, sn, dgbs);
    uint32_t dgbwp = ace_region(base_off + GTW_DGBWP);
    uint32_t dgbrp = ace_region(base_off + GTW_DGBRP);
    uint32_t dgbba = ace_region(base_off + GTW_DGBBA);
    uint32_t dgbfpi = ace_region(base_off + GTW_DGBFPI);

    if (!dgbs) {
        /* No buffer configured yet — retry next period. */
        timer_mod(gp->timer, qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + DMA_SIM_PERIOD_MS);
        return;
    }

    /* Determine transfer chunk: use DGBFPI if set, else 192 bytes (48 frames × 2ch × 16bit). */
    uint32_t chunk = dgbfpi ? dgbfpi : 192;
    if (chunk > dgbs)
        chunk = dgbs;

    if (gp->direction == GTW_DIR_HOST_OUT) {
        /* Playback: host writes data into the gateway buffer, advance WP. */
        /* Read from file if available, write into DSP memory at DGBBA+WP. */
        HDAStreamFile *sf = &hda_stream_files[sn];
        if (sf->in_file && dgbba) {
            uint8_t *buf = g_malloc(chunk);
            size_t n = fread(buf, 1, chunk, sf->in_file);
            if (n < chunk) {
                memset(buf + n, 0, chunk - n);
                if (feof(sf->in_file))
                    rewind(sf->in_file);
            }
            /* Write into circular buffer at current WP offset. */
            uint32_t wr_off = dgbwp % dgbs;
            uint32_t first = (wr_off + chunk <= dgbs) ? chunk : (dgbs - wr_off);
            address_space_write(&address_space_memory, dgbba + wr_off,
                                MEMTXATTRS_UNSPECIFIED, buf, first);
            if (first < chunk) {
                address_space_write(&address_space_memory, dgbba,
                                    MEMTXATTRS_UNSPECIFIED, buf + first, chunk - first);
            }
            g_free(buf);
        }
        dgbwp = (dgbwp + chunk) % dgbs;
        ace_region(base_off + GTW_DGBWP) = dgbwp;
        /* Set BNE (buffer not empty) since we wrote data. */
        ace_region(base_off + GTW_DGCS) |= GTW_DGCS_BNE;
    } else if (gp->direction == GTW_DIR_HOST_IN) {
        /* Capture: DSP writes data, host reads it. Advance RP. */
        /* Check how much DSP has written (distance WP - RP). */
        uint32_t avail;
        if (dgbwp >= dgbrp)
            avail = dgbwp - dgbrp;
        else
            avail = dgbs - dgbrp + dgbwp;

        if (avail == 0) {
            /* DSP hasn't written anything yet — just reschedule. */
            timer_mod(gp->timer, qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + DMA_SIM_PERIOD_MS);
            return;
        }
        uint32_t to_read = (avail < chunk) ? avail : chunk;

        HDAStreamFile *sf = &hda_stream_files[sn];
        if (sf->out_file && dgbba) {
            uint8_t *buf = g_malloc(to_read);
            uint32_t rd_off = dgbrp % dgbs;
            uint32_t first = (rd_off + to_read <= dgbs) ? to_read : (dgbs - rd_off);
            address_space_read(&address_space_memory, dgbba + rd_off,
                               MEMTXATTRS_UNSPECIFIED, buf, first);
            if (first < to_read) {
                address_space_read(&address_space_memory, dgbba,
                                   MEMTXATTRS_UNSPECIFIED, buf + first, to_read - first);
            }
            fwrite(buf, 1, to_read, sf->out_file);
            fflush(sf->out_file);
            g_free(buf);
        }
        dgbrp = (dgbrp + to_read) % dgbs;
        ace_region(base_off + GTW_DGBRP) = dgbrp;
    } else {
        /* Link directions: just advance pointers without file I/O.
         * Link-out: DSP writes, "codec" reads → advance RP.
         * Link-in:  "codec" writes, DSP reads → advance WP. */
        if (gp->direction == GTW_DIR_LINK_OUT) {
            /* Advance RP to consume what DSP wrote. */
            uint32_t avail = (dgbwp >= dgbrp) ? (dgbwp - dgbrp) : (dgbs - dgbrp + dgbwp);
            if (avail > 0) {
                uint32_t to_consume = (avail < chunk) ? avail : chunk;
                dgbrp = (dgbrp + to_consume) % dgbs;
                ace_region(base_off + GTW_DGBRP) = dgbrp;
            }
        } else { /* GTW_DIR_LINK_IN */
            /* Advance WP to feed DSP with silence/zeros. */
            dgbwp = (dgbwp + chunk) % dgbs;
            ace_region(base_off + GTW_DGBWP) = dgbwp;
            ace_region(base_off + GTW_DGCS) |= GTW_DGCS_BNE;
        }
    }

    /* Set BSC (buffer segment completion) for firmware notification. */
    ace_region(base_off + GTW_DGCS) |= GTW_DGCS_BSC;

    timer_mod(gp->timer, qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + DMA_SIM_PERIOD_MS);
}

static void gtw_start_periodic(struct adsp_io_info *info, unsigned stream_num,
                               GtwDirection dir)
{
    GtwStreamPeriodic *gp = &gtw_periodic[dir][stream_num];

    gp->info = info;
    gp->stream_num = stream_num;
    gp->direction = dir;
    gp->running = true;

    if (!gp->timer) {
        gp->timer = timer_new_ms(QEMU_CLOCK_REALTIME, gtw_period_cb, gp);
    }

    ace_log("GTW stream %u dir=%d: starting periodic DMA (%d ms)\n",
            stream_num, dir, DMA_SIM_PERIOD_MS);
    timer_mod(gp->timer, qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + DMA_SIM_PERIOD_MS);
}

static void gtw_stop_periodic(unsigned stream_num, GtwDirection dir)
{
    GtwStreamPeriodic *gp = &gtw_periodic[dir][stream_num];
    gp->running = false;
    if (gp->timer)
        timer_del(gp->timer);
    ace_log("GTW stream %u dir=%d: stopped periodic DMA\n", stream_num, dir);
}

/* Gateway read handler — firmware reads DGCS, DGBS, DGBWP, DGBRP etc. */
static uint64_t ace_hda_gtw_read(void *opaque, hwaddr addr, unsigned size)
{
    struct adsp_io_info *info = opaque;
    GtwDirection dir = (GtwDirection)(uintptr_t)info->private;
    if (dir == GTW_DIR_HOST_IN) {
        uint32_t sn = addr / GTW_REG_SIZE;
        uint32_t ro = addr % GTW_REG_SIZE;
        ace_log("GTW-HIN read: stream=%u reg=0x%02x val=0x%08x\n",
                sn, ro, ace_region(addr));
    }
    return ace_region(addr);
}

/* Gateway write handler — firmware writes DGCS (GEN/FIFORDY), DGBS, DGBFPI, etc. */
static void ace_hda_gtw_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    struct adsp_io_info *info = opaque;
    uint32_t stream_num = addr / GTW_REG_SIZE;
    uint32_t reg_offset = addr % GTW_REG_SIZE;
    GtwDirection dir_wr = (GtwDirection)(uintptr_t)info->private;
    if (dir_wr == GTW_DIR_HOST_IN) {
        ace_log("GTW-HIN write: stream=%u reg=0x%02x val=0x%08x\n",
                stream_num, reg_offset, (uint32_t)val);
    }
    uint32_t old_val = ace_region(addr);

    ace_region(addr) = (uint32_t)val;

    if (reg_offset == GTW_DGCS) {
        bool gen_now = (val & GTW_DGCS_GEN) != 0;
        bool gen_was = (old_val & GTW_DGCS_GEN) != 0;

        /* Determine direction from the region's init data (stored during device init). */
        GtwDirection dir = (GtwDirection)(uintptr_t)info->private;

        if (gen_now && !gen_was) {
            ace_log("GTW stream %u: GEN enabled (dir=%d FIFORDY=%d)\n",
                    stream_num, dir, !!(val & GTW_DGCS_FIFORDY));
            gtw_start_periodic(info, stream_num, dir);
        } else if (!gen_now && gen_was) {
            ace_log("GTW stream %u: GEN disabled\n", stream_num);
            gtw_stop_periodic(stream_num, dir);
        }
    } else if (reg_offset == GTW_DGBS) {
        ace_log("GTW stream %u: DGBS = %u\n", stream_num, (uint32_t)val);
    } else if (reg_offset == GTW_DGBBA) {
        ace_log("GTW stream %u: DGBBA = 0x%08x\n", stream_num, (uint32_t)val);
    } else if (reg_offset == GTW_DGBFPI) {
        /* DGBFPI write: firmware signals it consumed (HOST_OUT) or produced (HOST_IN)
         * data. Hardware advances the appropriate pointer. */
        GtwDirection dir = (GtwDirection)(uintptr_t)info->private;
        uint32_t base_off = stream_num * GTW_REG_SIZE;
        uint32_t dgbs = ace_region(base_off + GTW_DGBS);

        if (dgbs && val) {
            if (dir == GTW_DIR_HOST_OUT || dir == GTW_DIR_LINK_IN) {
                /* DSP consumed data → advance RP */
                uint32_t rp = ace_region(base_off + GTW_DGBRP);
                rp = (rp + (uint32_t)val) % dgbs;
                ace_region(base_off + GTW_DGBRP) = rp;
            } else {
                /* DSP produced data → advance WP */
                uint32_t wp = ace_region(base_off + GTW_DGBWP);
                wp = (wp + (uint32_t)val) % dgbs;
                ace_region(base_off + GTW_DGBWP) = wp;
            }
        }
    } else if (reg_offset == GTW_DGBWP || reg_offset == GTW_DGBRP) {
        /* Firmware shouldn't normally write these directly; just store. */
    }
}

/* Memory region operations for HDA Gateway registers */
const MemoryRegionOps ace_hda_gtw_ops = {
    .read = ace_hda_gtw_read,
    .write = ace_hda_gtw_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

/* Gateway init functions — store direction in info->private. */
void hda_gtw_hout_init(struct adsp_dev *adsp, MemoryRegion *parent,
                       struct adsp_io_info *info)
{
    static bool files_inited;
    if (!files_inited) {
        hda_stream_files_init();
        files_inited = true;
    }
    info->private = (void *)(uintptr_t)GTW_DIR_HOST_OUT;
}

void hda_gtw_hin_init(struct adsp_dev *adsp, MemoryRegion *parent,
                      struct adsp_io_info *info)
{
    info->private = (void *)(uintptr_t)GTW_DIR_HOST_IN;
}

void hda_gtw_lout_init(struct adsp_dev *adsp, MemoryRegion *parent,
                       struct adsp_io_info *info)
{
    info->private = (void *)(uintptr_t)GTW_DIR_LINK_OUT;
}

void hda_gtw_lin_init(struct adsp_dev *adsp, MemoryRegion *parent,
                      struct adsp_io_info *info)
{
    info->private = (void *)(uintptr_t)GTW_DIR_LINK_IN;
}

/* Initialize DMA subsystem */
static struct adsp_io_info *global_hda_dma_info = NULL;

void adsp_ace_dma_init(struct adsp_dev *adsp, MemoryRegion *parent,
                        struct adsp_io_info *info)
{
    ace_log( "CAVS DMA: Initializing HD Audio Stream DMA and GPDMA controllers\n");

    global_hda_dma_info = info;

    /* Open any file-backed streams configured via QEMU_HDA_IN_<n>/QEMU_HDA_OUT_<n>. */
    hda_stream_files_init();
    
    /* Initialize HDA stream registers to safe defaults */
    for (int i = 0; i < 16; i++) {
        uint32_t stream_base = HDA_STREAM_CTL_OFFSET(i);
        /* Advertise all streams as FIFO-ready after reset so FW can start configuration immediately. */
        ace_region((stream_base + 0x03)) = HDA_STS_FIFO_READY; /* FIFO ready */
    }
    
    ace_log( "CAVS DMA: Initialized %d HDA streams\n", 16);
    ace_log( "CAVS DMA: GPDMA controllers ready for configuration\n");
}

/* Emulate cavstool.py host behavior for standalone testing */
void ace_hda_stream_emulate_cavstool(uint32_t cmd, uint32_t channel, uint32_t ext_data)
{
    struct adsp_io_info *info = global_hda_dma_info;
    if (!info) return;
    if (channel >= HDA_MAX_STREAMS) return;

    switch (cmd) {
    case 6: /* IPCCMD_HDA_RESET */
        ace_hda_stream_write(info, HDA_STREAM_CTL_OFFSET(channel), HDA_CTL_STRM_RESET, 4);
        ace_hda_stream_write(info, HDA_STREAM_CTL_OFFSET(channel), 0, 4);
        break;
    case 7: /* IPCCMD_HDA_CONFIG */
        ace_region(HDA_STREAM_CBL_OFFSET(channel)) = (ext_data >> 8);
        ace_region(HDA_STREAM_BDPL_OFFSET(channel)) = 0x1000; /* dummy BDL */
        break;
    case 8: /* IPCCMD_HDA_START */
        ace_hda_stream_write(info, HDA_STREAM_CTL_OFFSET(channel), HDA_CTL_STRM_RUN, 4);
        break;
    case 9: /* IPCCMD_HDA_STOP */
        ace_hda_stream_write(info, HDA_STREAM_CTL_OFFSET(channel), 0, 4);
        break;
    }
}

#include "monitor/monitor.h"
#include "monitor/hmp-target.h"
#include "qobject/qdict.h"

void hmp_info_ace_dma(Monitor *mon, const QDict *qdict)
{
    struct adsp_io_info *info = global_hda_dma_info;
    unsigned i, stream_num;

    if (!info) {
        monitor_printf(mon, "ACE DMA info not initialized.\n");
        return;
    }

    monitor_printf(mon, "Intel ADSP ACE HDA DMA Streams:\n");

    for (stream_num = 0; stream_num < HDA_MAX_STREAMS; stream_num++) {
        uint64_t bdl_addr;
        uint32_t ctl, sts, lpib, cbl, lvi;

        ctl  = ace_region(HDA_STREAM_CTL_OFFSET(stream_num));
        sts  = ace_region(HDA_STREAM_STS_OFFSET(stream_num));
        lpib = ace_region(HDA_STREAM_LPIB_OFFSET(stream_num));
        cbl  = ace_region(HDA_STREAM_CBL_OFFSET(stream_num));
        lvi  = ace_region(HDA_STREAM_LVI_OFFSET(stream_num)) & 0xFF;
        bdl_addr = ((uint64_t)ace_region(HDA_STREAM_BDPU_OFFSET(stream_num)) << 32)
                   | ace_region(HDA_STREAM_BDPL_OFFSET(stream_num));

        if (!bdl_addr && !ctl && !cbl) {
            continue; /* Skip completely inactive streams */
        }

        monitor_printf(mon, "  Stream %2u: CTL=0x%08x STS=0x%08x LPIB=0x%08x CBL=0x%08x\n",
                       stream_num, ctl, sts, lpib, cbl);
        monitor_printf(mon, "             BDL_ADDR=0x%016" PRIx64 " LVI=%u\n", bdl_addr, lvi);

        if (bdl_addr) {
            for (i = 0; i <= lvi && i < HDA_MAX_BDL_ENTRIES; i++) {
                HDABdlEntry entry;
                address_space_read(&address_space_memory,
                                   bdl_addr + i * sizeof(entry),
                                   MEMTXATTRS_UNSPECIFIED,
                                   &entry, sizeof(entry));

                uint64_t buf_addr = ((uint64_t)le32_to_cpu(entry.addr_hi) << 32)
                                   | le32_to_cpu(entry.addr_lo);
                uint32_t buf_size = le32_to_cpu(entry.size);
                uint32_t ioc = le32_to_cpu(entry.ioc) & 1;

                monitor_printf(mon, "    BDL[%2u]: phys 0x%016" PRIx64 ", size %6u, ioc=%u\n",
                               i, buf_addr, buf_size, ioc);
            }
        }
    }
}

void adsp_monitor_ace_dma_in(Monitor *mon, const QDict *qdict)
{
    int stream = qdict_get_int(qdict, "stream");
    const char *filepath = qdict_get_str(qdict, "file");

    if (stream < 0 || stream >= HDA_MAX_STREAMS) {
        monitor_printf(mon, "Error: Invalid HDA stream ID %d. Must be 0-%d\n", 
                       stream, HDA_MAX_STREAMS - 1);
        return;
    }

    if (hda_stream_files[stream].in_file) {
        fclose(hda_stream_files[stream].in_file);
        hda_stream_files[stream].in_file = NULL;
    }
    
    if (hda_stream_files[stream].in_path) {
        g_free(hda_stream_files[stream].in_path);
        hda_stream_files[stream].in_path = NULL;
    }

    hda_stream_files[stream].in_path = g_strdup(filepath);
    hda_stream_files[stream].in_file = fopen(filepath, "rb");

    if (!hda_stream_files[stream].in_file) {
        monitor_printf(mon, "Error: cannot open input file '%s': %s\n", 
                       filepath, strerror(errno));
    } else {
        monitor_printf(mon, "HDA Stream %u: DMA input now bound to %s\n", 
                       stream, filepath);
    }
}
