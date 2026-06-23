xtensa_ss = ss.source_set()
xtensa_ss.add(files(
  'mx_pic.c',
  'pic_cpu.c',
  'xtensa_memory.c',
))
xtensa_ss.add(when: 'CONFIG_XTENSA_SIM', if_true: files('sim.c'))
xtensa_ss.add(when: 'CONFIG_XTENSA_VIRT', if_true: files('virt.c'))
xtensa_ss.add(when: 'CONFIG_XTENSA_XTFPGA', if_true: files('xtfpga.c'))
xtensa_ss.add(when: 'CONFIG_XTENSA_CAVS', if_true: files(
  'intel_adsp/adsp-common.c',
  'intel_adsp/ace15.c',
  'intel_adsp/ace20.c',
  'intel_adsp/ace30.c',
  'intel_adsp/ace40.c',
  'intel_adsp/cavs25.c',
  'intel_adsp/ace-dint.c',
  'intel_adsp/ace-shim.c',
  'intel_adsp/ace-hostwin.c',
  'intel_adsp/ace-dmw.c',
  'intel_adsp/ace-pmccu.c',
  'intel_adsp/ace-hfipc.c',
  'intel_adsp/ace-hfimr.c',
  'intel_adsp/ace-irq.c',
  'intel_adsp/ace-ipc.c',
  'intel_adsp/ace-l2.c',
  'intel_adsp/ace-low.c',
  'intel_adsp/ace-manifest.c',
  'intel_adsp/ace-memory.c',
  'intel_adsp/ace-dma.c',
  'intel_adsp/ace-dspcs.c',
  'intel_adsp/ace-idc.c',
  'intel_adsp/ace-timer.c',
  'intel_adsp/ace-tlb.c'
))

hw_arch += {'xtensa': xtensa_ss}
