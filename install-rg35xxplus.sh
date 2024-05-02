#!/bin/sh

cp ./build/SimpleTerminal ./rg35xxplus/APPS/
rsync -rv ./rg35xxplus/APPS/* root@192.168.1.140:/mnt/mmc/Roms/APPS
