#!/bin/bash

if [ $# -eq 0 ]
then
  echo "usage: $0 device kernel fs_image.ext4"
  echo "supported devices: byt, cht, bsw, hsw, bdw, bxt, apl, skl, kbl, cfl, cnl, cml, icl, tgl, ehl"
  exit
fi

# Default Kernel and FW if none supplied
KERNEL=${2:-$HOME/work/linux/arch/x86_64/boot/bzImage}
FS=${3:-$HOME/work/poky/build/tmp/deploy/images/qemux86-64/core-image-sato-qemux86-64.ext4}

# create ADSP devices
case $1 in
*byt)
 ADSP="-device driver=adsp-byt"
 HDA=""
  ;;
*cht)
 ADSP="-device driver=adsp-cht"
 HDA=""
  ;;
*bsw)
 ADSP="-device driver=adsp-cht"
 HDA=""
  ;;
*hsw)
 ADSP="-device driver=adsp-hsw"
 HDA=""
  ;;
*bdw)
 ADSP="-device driver=adsp-bdw"
 HDA=""
  ;;
*bxt)
 HDA=" -device bxt-intel-hda,id=sound0,bus=pci.0,addr=0xe"
  ;;
*apl)
 HDA=" -device apl-intel-hda,id=sound0,bus=pci.0,addr=0xe,debug=1"
  ;;
*skl)
 HDA=" -device skl-intel-hda,id=sound0,bus=pci.0,addr=0xe"
  ;;
*kbl)
 HDA=" -device kbl-intel-hda,id=sound0,bus=pci.0,addr=0xe"
  ;;
*cml_lp)
 HDA=" -device cml-lp-intel-hda,id=sound0,bus=pci.0,addr=0xe"
  ;;
*cml)
 HDA=" -device cml-hp-intel-hda,id=sound0,bus=pci.0,addr=0xe"
  ;;
*cfl)
 HDA=" -device cfl-intel-hda,id=sound0,bus=pci.0,addr=0xe"
  ;;
*cnl)
 HDA=" -device cnl-intel-hda,id=sound0,bus=pci.0,addr=0xe"
  ;;
*icl)
 HDA=" -device icl-intel-hda,id=sound0,bus=pci.0,addr=0xe"
  ;;
*tgl)
 HDA=" -device tgl-intel-hda,id=sound0,bus=pci.0,addr=0xe"
  ;;
*ehl)
 HDA=" -device ehl-intel-hda,id=sound0,bus=pci.0,addr=0xe"
  ;;
*)
  echo "usage: $0 device"
  echo "supported devices: byt, cht, bsw, hsw, bdw, bxt, apl, skl, kbl, cfl, cml, icl, tgl, ehl"
  ./xtensa-softmmu/qemu-system-xtensa -machine help
  exit
  ;;
esac

# Launch Qemu
./x86_64-softmmu/qemu-system-x86_64 \
	-enable-kvm -machine pc,accel=kvm -smp 2,sockets=2,cores=1,threads=1 \
	-device virtio-net-pci,netdev=net0,mac=52:54:00:12:34:02 \
	-netdev user,id=net0,hostfwd=tcp::5555-:22 \
	-drive file=$FS,if=virtio,format=raw \
	-show-cursor -usb -device usb-tablet \
	-object rng-random,filename=/dev/urandom,id=rng0 \
	-device virtio-rng-pci,rng=rng0 \
	-cpu core2duo -m 256 \
	$HDA $ADSP \
	-serial mon:vc -serial null \
	-kernel $KERNEL -append 'root=/dev/vda rw highres=off  mem=256M ip=192.168.7.2::192.168.7.1:255.255.255.0 uvesafb.mode_option=640x480-32 oprofile.timer=1 uvesafb.task_timeout=-1 '

