#!/usr/bin/env bash
# Prepare a checkout: submodules + Python venv with imgtool, smpmgr, python-can, the umcub tool.
set -euo pipefail
cd "$(dirname "$0")/.."
git submodule update --init --depth 1 third_party/mcuboot third_party/tinyusb \
    third_party/cmsis_core third_party/st/cmsis_device_h7 third_party/st/stm32h7xx_hal_driver \
    third_party/st/cmsis_device_f1 third_party/st/stm32f1xx_hal_driver \
    third_party/st/cmsis_device_f4 third_party/st/stm32f4xx_hal_driver \
    third_party/st/stm32_open_pin_data            # ST pin data: build-time pin check
python3 -m venv .venv
.venv/bin/pip install -q --upgrade pip
.venv/bin/pip install -q -r tools/requirements.txt
.venv/bin/pip install -q -e third_party/mcuboot/scripts
.venv/bin/pip install -q -e tools/host                # umcub host library and tool
echo "ok: .venv/bin/imgtool $(.venv/bin/imgtool version)"
