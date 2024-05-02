#!/bin/sh
set -v

PLATFORM=rg35xxplus
HOST_WORKSPACE=$(pwd)

if [ ! "$(docker images -q rg35xxplus-toolchain:latest 2> /dev/null)" ]; then
  echo 'toolchain image does not exist, building a new image'
  mkdir -p toolchains
  git clone https://github.com/haoict/union-$PLATFORM-toolchain/ toolchains/$PLATFORM-toolchain
  cd toolchains/$PLATFORM-toolchain && make .build
  cd $HOST_WORKSPACE
fi

docker run -it --rm -v $HOST_WORKSPACE:/root/workspace $PLATFORM-toolchain /bin/bash -c '. ~/.bashrc && cd /root/workspace && git config --global --add safe.directory /root/workspace && make && chmod 777 -R ./build'