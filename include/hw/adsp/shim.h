/* Stub header for adsp shim - minimal definitions for v5.2 compatibility */
#ifndef HW_ADSP_SHIM_H
#define HW_ADSP_SHIM_H

struct adsp_io_info;

/* SHIM register offsets */
#define SHIM_DSPWC       0x20
#define SHIM_DSPWCTT0C   0x30
#define SHIM_DSPWCTT1C   0x38
#define SHIM_DSPWCTTCS   0x28
#define SHIM_CLKCTL      0x78
#define SHIM_CLKSTS      0x7C

/* SHIM register bit fields */
#define SHIM_DSPWCTTCS_T0A  0x00000010
#define SHIM_DSPWCTTCS_T0T  0x00040000
#define SHIM_DSPWCTTCS_T1A  0x00000100
#define SHIM_DSPWCTTCS_T1T  0x00400000

/* Forward declarations */
void ace_irq_set(struct adsp_dev *adsp, int irq, uint32_t mask);
void ace_irq_clear(struct adsp_dev *adsp, int irq, uint32_t mask);

/* Trace function stubs */
static inline void trace_adsp_dsp_shim_read(uint64_t addr, uint32_t val) {}
static inline void trace_adsp_dsp_shim_write(uint64_t addr, uint32_t val) {}
static inline void trace_adsp_dsp_shim_event(const char *msg, uint32_t val) {}
static inline void trace_adsp_dsp_host_msg(const char *type, uint32_t msg) {}
static inline void trace_adsp_dsp_host_event(const char *msg) {}

#endif /* HW_ADSP_SHIM_H */
