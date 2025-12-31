#!/bin/bash

# ESP32 Flash Erase Script
# Completely erases the ESP32 flash memory
# Usage: ./upload-erase.sh [board-name] [port]
#   - board-name: Required when multiple boards configured
#   - port: Optional, auto-detected if not provided

set -e

# Load common configuration and functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/config.sh"

# Get arduino-cli path
ARDUINO_CLI=$(find_arduino_cli)

# Parse board and port arguments
parse_board_and_port_args "$@"

# Extract chip type from FQBN (2nd segment is platform, use that for chip type)
CHIP_TYPE=$(echo "$FQBN" | cut -d':' -f3 | grep -oE 'esp32[a-z0-9]*' | head -n1)
if [[ -z "$CHIP_TYPE" ]]; then
    CHIP_TYPE="esp32"  # Default fallback
fi

# Auto-detect port if not specified
if [[ -z "$PORT" ]]; then
    if PORT=$(find_serial_port); then
        echo -e "${GREEN}Auto-detected port: $PORT${NC}"
    else
        echo -e "${RED}Error: No serial device found at /dev/ttyUSB0 or /dev/ttyACM0${NC}"
        if [[ "${#FQBN_TARGETS[@]}" -gt 1 ]]; then
            echo "Please specify port manually: ${0##*/} <board-name> [port]"
        else
            echo "Please specify port manually: ${0##*/} [port]"
        fi
        exit 1
    fi
fi

echo -e "${CYAN}=== ESP32 Flash Erase Utility ===${NC}"
echo ""
echo -e "${RED}WARNING: This will completely erase all data from the ESP32 flash memory!${NC}"
echo "Board: $BOARD"
echo "Chip:  $CHIP_TYPE"
echo "FQBN:  $FQBN"
echo "Port:  $PORT"
echo ""
read -p "Are you sure you want to continue? (y/N) " -n 1 -r
echo ""

if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo -e "${YELLOW}Flash erase cancelled${NC}"
    exit 0
fi

# Check if arduino-cli exists in local bin directory
if [ ! -f "$ARDUINO_CLI" ]; then
    echo -e "${RED}Error: arduino-cli is not installed at $ARDUINO_CLI${NC}"
    echo "Please run setup.sh first"
    exit 1
fi

echo ""
echo -e "${YELLOW}Erasing flash memory...${NC}"
echo ""

# Use esptool.py which is installed with ESP32 platform
# The arduino-cli includes esptool.py with the ESP32 core
ESPTOOL="$HOME/.arduino15/packages/esp32/tools/esptool_py/*/esptool.py"

# Find esptool.py
ESPTOOL_PATH=$(find "$HOME/.arduino15/packages/esp32/tools/esptool_py" -name "esptool.py" -type f 2>/dev/null | head -n 1)

if [ -z "$ESPTOOL_PATH" ]; then
    echo -e "${RED}Error: esptool.py not found${NC}"
    echo "Please ensure ESP32 platform is installed (run setup.sh)"
    exit 1
fi

# Erase flash
python3 "$ESPTOOL_PATH" --chip "$CHIP_TYPE" --port "$PORT" erase_flash

echo ""
echo -e "${GREEN}=== Flash Erase Complete ===${NC}"
echo "The ESP32 flash memory has been completely erased"
echo ""
echo "Next steps:"
if [[ "${#FQBN_TARGETS[@]}" -gt 1 ]]; then
    echo "  1. Upload new firmware: ./upload.sh $BOARD"
    echo "  2. Or build and upload: ./build.sh $BOARD && ./upload.sh $BOARD"
else
    echo "  1. Upload new firmware: ./upload.sh"
    echo "  2. Or build and upload: ./build.sh && ./upload.sh"
fi
