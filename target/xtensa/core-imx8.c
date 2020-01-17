#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/gdbstub.h"
#include "qemu-common.h"
#include "qemu/host-utils.h"

#include "core-imx8/core-isa.h"
#include "overlay_tool.h"

#define xtensa_modules xtensa_modules_imx8
#include "core-imx8/xtensa-modules.inc.c"

static XtensaConfig imx8 __attribute__((unused)) = {
    .name = "imx8",
    .gdb_regmap = {
        .reg = {
#include "core-imx8/gdb-config.inc.c"
        }
    },
    .isa_internal = &xtensa_modules,
    .clock_freq_khz = 40000,
    DEFAULT_SECTIONS
};

REGISTER_CORE(imx8)
