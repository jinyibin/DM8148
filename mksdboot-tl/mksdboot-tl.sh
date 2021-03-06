#! /bin/sh
# Script to create DVSDK SD card for TI816x SOC_FAMILY plaforms.
#
# Author: Brijesh Singh, Texas Instruments Inc.
#       : Adapted for TI816x by Siddharth Heroor, Texas Instruments Inc.
#
# Licensed under terms of GPLv2
#

VERSION="0.4"

execute ()
{
    $* >/dev/null
    if [ $? -ne 0 ]; then
        echo
        echo "ERROR: executing $*"
        echo
        exit 1
    fi
}

version ()
{
  echo
  echo "`basename $1` version $VERSION"
  echo "Script to create bootable SD card for DM814x EVM"
  echo

  exit 0
}

usage ()
{
  echo "
Usage: `basename $1` <options> [ files for install partition ]

Mandatory options:
  --device              SD block device node (e.g /dev/sdd)

Optional options:
  --version             Print version.
  --help                Print this help message.
"
  exit 1
}

# Process command line...
while [ $# -gt 0 ]; do
  case $1 in
    --help | -h)
      usage $0
      ;;
    --device) shift; device=$1; shift; ;;
    --sdk) shift; sdkdir=$1; shift; ;;
    --version) version $0;;
    *) copy="$copy $1"; shift; ;;
  esac
done

test -z $device && usage $0

if [ ! -f filesystem/*.tar.gz ]; then
  echo "ERROR: failed to find rootfs tar in filesystem "
  exit 1;
fi
 
if [ ! -b $device ]; then
   echo "ERROR: $device is not a block device file"
   exit 1;
fi

echo "************************************************************"
echo "*         THIS WILL DELETE ALL THE DATA ON $device        *"
echo "*                                                          *"
echo "*         WARNING! Make sure your computer does not go     *"
echo "*                  in to idle mode while this script is    *"
echo "*                  running. The script will complete,      *"
echo "*                  but your SD card may be corrupted.      *"
echo "*                                                          *"
echo "*         Press <ENTER> to confirm....                     *"
echo "************************************************************"
read junk

for i in `ls -1 $device?`; do
 echo "unmounting device '$i'"
 umount $i 2>/dev/null
done

execute "dd if=/dev/zero of=$device bs=1024 count=1024"

# get the partition information.
total_size=`fdisk -l $device | grep Disk | awk '{print $5}'`
total_cyln=`echo $total_size/255/63/512 | bc`

# start from cylinder 20, this should give enough space for flashing utility
# to write u-boot binary.
pc1_start=20
pc1_end=10

# end of boot partition
# recalculate number of 1/8 total_cyln
pc1_end=$(((($total_cyln - $pc1_end) / 8) * 1))

# start of rootfs partition
pc2_start=$(($pc1_start + $pc1_end))

{
echo ,$pc1_end,0x0C,*
echo ,,,-
} | sfdisk -D -H 255 -S 63 -C $total_cyln $device

if [ $? -ne 0 ]; then
    echo ERROR
    exit 1;
fi

echo "Formating ${device}1 ..."
execute "mkfs.vfat -F 32 -n "boot" ${device}1"
echo "Formating ${device}2 ..."
execute "mkfs.ext3 -j -L "rootfs" ${device}2"
if [ "$pc2" != "" ]; then
 echo "Formating ${device}3 ..."
 execute "mkfs.ext3 -j -L "START_HERE" ${device}3"
fi

# creating boot.scr
execute "mkdir -p /tmp/sdk"
cat <<EOF >/tmp/sdk/boot.cmd
fatload mmc 0 0x81000000 ti_logo.bmp
bmp display 0x81000000
setenv bootargs 'console=ttyO0,115200n8 rootwait root=/dev/mmcblk0p2 rw mem=364M@0x80000000 mem=320M@0x9FC00000 vmalloc=500M  notifyk.vpssm3_sva=0xBF900000 ip=off noinitrd'
fatload mmc 0 0x80009000 uImage
bootm 0x80009000
EOF

mkimage -A arm -O linux -T script -C none -a 0 -e 0 -n 'Execute uImage.bin' -d /tmp/sdk/boot.cmd /tmp/sdk/boot.scr

if [ $? -ne 0 ]; then
  echo "Failed to execute mkimage to create boot.scr"
  echo "Execute 'sudo apt-get install uboot-mkimage' to install the package"
  exit 1
fi

echo "Copying u-boot/mlo/uImage on ${device}1"
execute "mkdir -p /tmp/sdk/$$"
execute "mount ${device}1 /tmp/sdk/$$"
execute "cp /tmp/sdk/boot.scr /tmp/sdk/$$/"
execute "cp /tmp/sdk/boot.cmd /tmp/sdk/$$/"
execute "cp boot/* /tmp/sdk/$$/"

sync
echo "unmounting ${device}1"
execute "umount /tmp/sdk/$$"

execute "mount ${device}2 /tmp/sdk/$$"
echo "Extracting filesystem on ${device}2 ..."
rootfs=`ls -1 filesystem/*.tar.gz`
execute "tar zxf $rootfs -C /tmp/sdk/$$"
sync

#echo "Extracting modules on ${device}2 ..."
#modules=`ls -1 modules/*.tar.gz`
#execute "tar zxf $modules -C /tmp/sdk/$$/lib/modules/"
#sync

# check if we need to create symbolic link for matrix to auto start
echo -n "Creating matrix-gui symbolic link..."
if [ -f /tmp/sdk/$$/etc/init.d/matrix-gui ]; then
  if [ -h /tmp/sdk/$$/etc/rc3.d/*matrix* ]; then
    echo " (skipped) "
  else
    ln -s  ../init.d/matrix-gui /tmp/sdk/$$/etc/rc3.d/S99matrix-gui
    ln -s  ../init.d/matrix-gui /tmp/sdk/$$/etc/rc5.d/S99matrix-gui
    echo "done"
  fi
fi

# add config file
echo -n "Add config file ..."
execute "cp boot/pointercal /tmp/sdk/$$/etc/pointercal"
execute "cp boot/opkg.conf /tmp/sdk/$$/etc/opkg/opkg.conf"

sync
echo "unmounting ${device}2"
execute "umount /tmp/sdk/$$"

if [ "$pc2" != "" ]; then
  echo "Copying $copy on ${device}3 ..."
  execute "mount ${device}3 /tmp/sdk/$$"
  execute "cp -ar $copy /tmp/sdk/$$"
  #execute "cp $sdkdir/bin/setup.htm /tmp/sdk/$$"
  #execute "cp $sdkdir/bin/top_ti814x_evm.png /tmp/sdk/$$/"
  #execute "cp $sdkdir/docs/*_Quick_start_guide.pdf /tmp/sdk/$$/quickstartguide.pdf"
  sync
  echo "unmounting ${device}3"
  execute "umount /tmp/sdk/$$"
fi

execute "rm -rf /tmp/sdk/$$"
echo "completed!"

