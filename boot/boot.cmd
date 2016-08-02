fatload mmc 0 0x81000000 ti_logo.bmp
bmp display 0x81000000
setenv bootargs 'console=ttyO0,115200n8 rootwait root=/dev/mmcblk0p2 rw mem=364M@0x80000000 mem=320M@0x9FC00000 vmalloc=500M notifyk.vpssm3_sva=0xBF900000 ip=off noinitrd'
fatload mmc 0 0x80009000 uImage
bootm 0x80009000
