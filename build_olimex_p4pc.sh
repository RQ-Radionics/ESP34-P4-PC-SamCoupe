#!/usr/bin/env bash
# build_olimex_p4pc.sh — Build SimCoupe for Olimex ESP32-P4-PC
#
# Usage:
#   ./build_olimex_p4pc.sh              — incremental build
#   ./build_olimex_p4pc.sh clean        — clean configure + build
#   ./build_olimex_p4pc.sh flash        — incremental build + flash
#   ./build_olimex_p4pc.sh monitor      — incremental build + flash + monitor
#   ./build_olimex_p4pc.sh clean flash  — clean + build + flash
#
# Run with 'clean' after changing sdkconfig.defaults or adding components.
set -e

cd "$(dirname "$0")"

echo "=== SimCoupe ESP32-P4-PC build ==="

if [[ "${*}" == *clean* ]]; then
    echo "--- Clean configure ---"
    rm -f sdkconfig
    rm -rf build
    idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults" set-target esp32p4
fi

idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults" build

echo ""
echo "=== Build complete ==="
echo ""

case "${1:-}" in
    flash|clean)
        if [[ "${*}" == *flash* ]]; then
            echo "Flashing..."
            idf.py -p "${PORT:-/dev/cu.usbmodem*}" flash
        fi
        ;;
    monitor)
        echo "Flashing and monitoring..."
        idf.py -p "${PORT:-/dev/cu.usbmodem*}" flash monitor
        ;;
    *)
        echo "Done. Flash with:"
        echo "  idf.py -p /dev/cu.usbmodem* flash"
        echo "  idf.py -p /dev/cu.usbmodem* flash monitor"
        ;;
esac
