#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/gdbstub.h"
#include "qemu-common.h"
#include "qemu/host-utils.h"

#include "core-imx8m/core-isa.h"
#include "overlay_tool.h"

#define xtensa_modules xtensa_modules_imx8m
#include "core-imx8m/xtensa-modules.inc.c"

static XtensaConfig imx8m __attribute__((unused)) = {
    .name = "imx8m",
    .gdb_regmap = {
        .reg = {
#include "core-imx8m/gdb-config.inc.c"
        }
    },
    .isa_internal = &xtensa_modules,
    .clock_freq_khz = 40000,
    DEFAULT_SECTIONS
};

REGISTER_CORE(imx8m)
