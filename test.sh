sudo dmsetup create zero1 --table "0 20000 zero"
sudo dmsetup create dmp1 --table "0 20000 dmp /dev/mapper/zero1"
ls -al /dev/mapper/*
sudo dd if=/dev/random of=/dev/mapper/dmp1 bs=4k count=1
sudo dd of=/dev/null if=/dev/mapper/dmp1 bs=4k count=1
sudo cat /sys/module/dmp/stat/volumes
sudo dmsetup remove dmp1
sudo dmsetup remove zero1
