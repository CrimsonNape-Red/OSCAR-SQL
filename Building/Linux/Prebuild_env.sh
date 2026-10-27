#!/bin/bash
# sudo ./Prebuild_env.sh
apt update && apt install -y build-essential libgl1-mesa-dev qtcreator qttools5-dev libqt5serialport5 libqt5serialport5-dev libz-dev
# Qt 6 builds: also install qt6-connectivity-dev (Qt Bluetooth, for Data > Viatom BLE Import)