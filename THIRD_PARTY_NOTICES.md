# Third-party components

umcub itself is licensed under the Apache License 2.0 (see `LICENSE`).
The following components are used as git submodules in `third_party/` and keep
their own licenses. When you ship a bootloader **binary**, include these notices
(copyright lines and license texts) with your product documentation.

| Component | Path | License | Used in the bootloader binary |
|---|---|---|---|
| MCUboot | `third_party/mcuboot` | Apache-2.0 | yes (bootutil, boot_serial) |
| zcbor (inside MCUboot) | `third_party/mcuboot/boot/zcbor` | Apache-2.0 | yes |
| mbed TLS ASN.1 parser (inside MCUboot) | `third_party/mcuboot/ext/mbedtls-asn1` | Apache-2.0 | yes (ECDSA key/signature parsing) |
| TinyCrypt (Intel, inside MCUboot) | `third_party/mcuboot/ext/tinycrypt` | BSD-3-Clause | yes (SHA-256, ECDSA P-256) |
| tinyUSB | `third_party/tinyusb` | MIT | yes, when USB is enabled |
| CMSIS Core (Arm) | `third_party/cmsis_core` | Apache-2.0 | headers |
| STM32H7 CMSIS device (ST) | `third_party/st/cmsis_device_h7` | Apache-2.0 | headers, startup vector table |
| STM32H7 HAL/LL driver (ST, LL headers only) | `third_party/st/stm32h7xx_hal_driver` | BSD-3-Clause | LL inline functions |

`tools/keys/dev-ecdsa-p256.pem` is a public **development** signing key: never
use it for products.
