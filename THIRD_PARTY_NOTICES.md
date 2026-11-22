# Third-party notices

The hub's firmware is built with software by others, each under its own licence. Those licences cover their parts, and
[LICENSE](LICENSE) covers the rest. The full licence texts are in [licenses/](licenses), and every release carries this
file and that folder alongside the firmware.

## In the firmware

| Software | Version | Licence | Copyright |
| --- | --- | --- | --- |
| [Arduino core for the ESP32](https://github.com/espressif/arduino-esp32), with its WiFi, DNSServer, ESPmDNS, LittleFS, Preferences and Update libraries | 3.3.11 | LGPL-2.1-or-later ([text](licenses/LGPL-2.1.txt)) | Espressif Systems and contributors |
| libb64, in the Arduino core | | Public domain | Chris Venter |
| [ESP-IDF](https://github.com/espressif/esp-idf), including the bootloader, Wi-Fi, ESP-NOW and PHY libraries, NVS, the network interface and core dumps | 5.5.5 | Apache-2.0 ([text](licenses/Apache-2.0.txt)) | Espressif Systems (Shanghai) Co., Ltd. |
| [FreeRTOS kernel](https://github.com/espressif/esp-idf/tree/master/components/freertos), in ESP-IDF | | MIT ([text](licenses/FreeRTOS-MIT.txt)) | Amazon.com, Inc. or its affiliates |
| [lwIP](https://savannah.nongnu.org/projects/lwip/), in ESP-IDF | | BSD-3-Clause ([text](licenses/lwIP-BSD-3-Clause.txt)) | Swedish Institute of Computer Science |
| [Mbed TLS](https://github.com/Mbed-TLS/mbedtls), in ESP-IDF | | Apache-2.0 ([text](licenses/Apache-2.0.txt)) | The Mbed TLS Contributors |
| [wpa_supplicant](https://w1.fi/wpa_supplicant/), in ESP-IDF | | BSD-3-Clause ([text](licenses/wpa_supplicant-BSD-3-Clause.txt)) | Jouni Malinen and contributors |
| [TLSF](https://github.com/mattconte/tlsf), the memory allocator in ESP-IDF | | BSD-3-Clause ([text](licenses/TLSF-BSD-3-Clause.txt)) | Matthew Conte |
| [mDNS](https://github.com/espressif/esp-protocols/tree/master/components/mdns) | 1.11.3 | Apache-2.0 ([text](licenses/Apache-2.0.txt)) | Espressif Systems (Shanghai) Co., Ltd. |
| [esp_littlefs](https://github.com/joltwallet/esp_littlefs) | 1.22.2 | MIT ([text](licenses/esp_littlefs-MIT.txt)) | Brian Pugh |
| [littlefs](https://github.com/littlefs-project/littlefs), in esp_littlefs | | BSD-3-Clause ([text](licenses/littlefs-BSD-3-Clause.txt)) | The littlefs authors, Arm Limited |
| [Monocypher](https://monocypher.org), in `lib/monocypher`: Ed25519 and SHA-512, which check a firmware update's signature | 4.0.2 | BSD-2-Clause or CC0-1.0 ([text](licenses/Monocypher-BSD-2-Clause-or-CC0.txt)) | Loup Vaillant and the Monocypher contributors |
| [newlib](https://sourceware.org/newlib/), the C library | | BSD-style licences, each listed in [the text](licenses/newlib.txt) | Red Hat and others |
| The GCC runtime libraries (libgcc, libstdc++) | | GPL-3.0 with the GCC Runtime Library Exception | Free Software Foundation |

The X25519 key exchange in `lib/s2o_proto/s2o_x25519.cpp` follows the field arithmetic of
[TweetNaCl](https://tweetnacl.cr.yp.to/), which is in the public domain.

## Used by the page, not part of the firmware

The Sensors page makes pictures with [ffmpeg.wasm](https://github.com/ffmpegwasm/ffmpeg.wasm) (`@ffmpeg/core`
0.12.6). The browser downloads it from jsDelivr or unpkg when the page needs it, and checks it against the hashes in
`web/pictures.js`. The hub neither contains nor sends it. It is under the GPL-2.0-or-later and LGPL-2.1-or-later
licences of FFmpeg and of the libraries it is built with.

## The LGPL and the Arduino core

The Arduino core is linked into the firmware, and the GNU Lesser General Public License lets anyone who has the firmware
replace that library with a modified version of their own. The complete source of each release, under its tag in this
repository, builds that firmware with `pio run`, so a modified core can be linked in its place. [LICENSE](LICENSE) gives
the permissions the LGPL asks for: modifying the firmware for one's own use, and reverse engineering it to debug such
changes. The source of the Arduino core itself is at https://github.com/espressif/arduino-esp32, under the tag of the
version above.
