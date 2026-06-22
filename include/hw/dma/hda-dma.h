/* HDA DMA - ACE3.x HD Audio Stream DMA support */
#ifndef HW_DMA_HDA_DMA_H
#define HW_DMA_HDA_DMA_H

#include "system/memory.h"

struct adsp_io_info;
struct adsp_dev;

/* HD Audio Stream DMA operations exported from ace-dma.c */
extern const MemoryRegionOps cavs_hda_stream_ops;
extern const MemoryRegionOps hda_dmac_ops;

/* Initialize HDA DMA */
static inline void hda_dma_init_dev(struct adsp_dev *adsp, MemoryRegion *parent,
        struct adsp_io_info *info)
{
    /* Initialization handled by adsp_ace_dma_init */
}

/* Gateway init functions — set direction in private_data */
void hda_gtw_hout_init(struct adsp_dev *adsp, MemoryRegion *parent,
                       struct adsp_io_info *info);
void hda_gtw_hin_init(struct adsp_dev *adsp, MemoryRegion *parent,
                      struct adsp_io_info *info);
void hda_gtw_lout_init(struct adsp_dev *adsp, MemoryRegion *parent,
                       struct adsp_io_info *info);
void hda_gtw_lin_init(struct adsp_dev *adsp, MemoryRegion *parent,
                      struct adsp_io_info *info);

#endif /* HW_DMA_HDA_DMA_H */
