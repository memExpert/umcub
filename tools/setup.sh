#!/usr/bin/env bash
# Prepare a checkout: submodules + Python venv with imgtool, smpmgr, python-can.
set -euo pipefail
cd "$(dirname "$0")/.."
git submodule update --init --depth 1 third_party/mcuboot third_party/tinyusb \
    third_party/cmsis_core third_party/st/cmsis_device_h7 third_party/st/stm32h7xx_hal_driver
python3 -m venv .venv
.venv/bin/pip install -q --upgrade pip
.venv/bin/pip install -q -r tools/requirements.txt
.venv/bin/pip install -q -e third_party/mcuboot/scripts
echo "ok: .venv/bin/imgtool $(.venv/bin/imgtool version)"
