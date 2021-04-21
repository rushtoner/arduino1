#!/bin/bash

CARD=DAR13
if [ -d /Volumes/${CARD} ]; then
  echo Getting logs from /Volumes/${CARD}
  rsync --progress -uv /Volumes/${CARD}/LORA-*.* logs/${CARD}/.
  rsync --progress -uv /Volumes/${CARD}/HMR-*.* logs/${CARD}/.
  rsync --progress -uv /Volumes/${CARD}/APRS*.* logs/${CARD}/.
fi

CARD=DAR14
if [ -d /Volumes/${CARD} ]; then
  echo Getting logs from /Volumes/${CARD}
  rsync --progress -uv /Volumes/${CARD}/LORA-*.* logs/${CARD}/.
  rsync --progress -uv /Volumes/${CARD}/HMR-*.* logs/${CARD}/.
  rsync --progress -uv /Volumes/${CARD}/APRS*.* logs/${CARD}/.
fi

CARD=FLIGHT2
if [ -d /Volumes/${CARD} ]; then
  echo Getting logs from /Volumes/${CARD}
  rsync --progress -uvr /Volumes/${CARD}/XK07DATA logs/${CARD}/.
fi

