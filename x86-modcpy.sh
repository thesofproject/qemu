#!/bin/bash

# script to copy kernel modules to Poky FS for Qemu
# Needs to run under sudo for mount and copy to FS

if [ $# -eq 0 ]
then
  echo "usage: $0 username"
  echo "Needs to be run under sudo."
  exit
fi

WORK=/home/$1/work
MOUNT=$WORK/mnt-ext4
POKY=$WORK/poky/build/tmp/deploy/images
LINUX=$WORK/linux
SOF=$WORK/sof/sof

echo "check: work dir is $WORK"
echo "check: mount point is $MOUNT"
echo "check: poky dir is $POKY"
echo "check: linux dir is $LINUX"
echo "check: sof is $SOF"
echo
read -p "***  Press enter to continue or CTRL-C to abort ***" var

# Mount ext4 FS as locally
echo Mounting FS
mount -o loop $POKY/qemux86-64/core-image-sato-qemux86-64.ext4 $MOUNT

echo Delete old modules....
rm -fr $MOUNT/lib/modules
rm -fr $MOUNT/lib/firmware

# Install modules
echo install modules
cd $LINUX
make modules_install INSTALL_MOD_PATH=$MOUNT

# Install Firmware
echo install firmware
cd $SOF
mkdir $MOUNT/lib/firmware/
mkdir $MOUNT/lib/firmware/intel/
mkdir $MOUNT/lib/firmware/intel/sof
mkdir $MOUNT/lib/firmware/intel/sof-tplg
cp -f `ls build*/*.ri` $MOUNT/lib/firmware/intel/sof
cp `find . -name *.tplg` $MOUNT/lib/firmware/intel/sof-tplg

# unount
umount $MOUNT
echo Done !
