/* DW DMA (DesignWare DMA) - ACE3.x GPDMA support */
#ifndef HW_DMA_DW_DMA_H
#define HW_DMA_DW_DMA_H

#include "system/memory.h"

struct adsp_io_info;
struct adsp_dev;

/* GPDMA operations exported from ace-dma.c */
extern const MemoryRegionOps cavs_gpdma_ops;

static inline void dw_dma_msg(void *msg)
{
    /* Stub for compatibility */
}

#endif /* HW_DMA_DW_DMA_H */
