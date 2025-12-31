#!/usr/bin/env bash

# Install/register custom partition tables into the Arduino ESP32 core.
#
# This is intentionally a standalone script so:
# - Local dev can run: ./tools/install-custom-partitions.sh
# - setup.sh can call it automatically
# - CI workflows can call it before compiling boards that use PartitionScheme=...

set -euo pipefail

SCRIPT_DIR="$(CDPATH="" cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(CDPATH="" cd "$SCRIPT_DIR/.." && pwd)"

PARTITION_FILE_SRC="$REPO_ROOT/partitions/partitions_ota_1_9mb.csv"
PARTITION_SCHEME_ID="ota_1_9mb"
PARTITION_FILE_BASENAME="partitions_ota_1_9mb.csv"
PARTITION_NAME_NO_EXT="partitions_ota_1_9mb"

BOARD_ID="nologo_esp32c3_super_mini"
UPLOAD_MAX_SIZE="1966080"  # 0x1E0000

ESP32_HW_BASE="$HOME/.arduino15/packages/esp32/hardware/esp32"

if [[ ! -f "$PARTITION_FILE_SRC" ]]; then
  echo "Error: partition file not found: $PARTITION_FILE_SRC" >&2
  echo "Did you delete or rename the file under partitions/?" >&2
  exit 1
fi

if [[ ! -d "$ESP32_HW_BASE" ]]; then
  echo "Error: ESP32 Arduino core not found at $ESP32_HW_BASE" >&2
  echo "Run ./setup.sh first to install esp32:esp32." >&2
  exit 1
fi

# Pick the latest installed ESP32 core directory.
# Example: ~/.arduino15/packages/esp32/hardware/esp32/3.0.7
ESP32_DIR="$(ls -1d "$ESP32_HW_BASE"/*/ 2>/dev/null | sort -V | tail -n 1 || true)"
ESP32_DIR="${ESP32_DIR%/}"

if [[ -z "$ESP32_DIR" || ! -d "$ESP32_DIR" ]]; then
  echo "Error: could not locate an installed ESP32 core version under $ESP32_HW_BASE" >&2
  exit 1
fi

PARTITION_DIR="$ESP32_DIR/tools/partitions"
BOARDS_TXT="$ESP32_DIR/boards.txt"

if [[ ! -d "$PARTITION_DIR" ]]; then
  echo "Error: partition directory not found: $PARTITION_DIR" >&2
  exit 1
fi

if [[ ! -f "$BOARDS_TXT" ]]; then
  echo "Error: boards.txt not found: $BOARDS_TXT" >&2
  exit 1
fi

echo "Installing custom partition table into Arduino ESP32 core..."
echo "- Core:       $ESP32_DIR"
echo "- Partitions: $PARTITION_DIR"

# 1) Copy partition CSV
cp "$PARTITION_FILE_SRC" "$PARTITION_DIR/$PARTITION_FILE_BASENAME"
echo "✓ Installed $PARTITION_FILE_BASENAME"

# 2) Register PartitionScheme in boards.txt (idempotent)
# We key off the specific menu entry to avoid false positives.
if grep -q "^${BOARD_ID}\.menu\.PartitionScheme\.${PARTITION_SCHEME_ID}=" "$BOARDS_TXT"; then
  echo "✓ PartitionScheme '$PARTITION_SCHEME_ID' already registered in boards.txt"
else
  {
    echo ""
    echo "# Custom OTA partition scheme (installed by $REPO_ROOT/tools/install-custom-partitions.sh)"
    echo "${BOARD_ID}.menu.PartitionScheme.${PARTITION_SCHEME_ID}=Custom OTA (1.9MB APP×2)"
    echo "${BOARD_ID}.menu.PartitionScheme.${PARTITION_SCHEME_ID}.build.partitions=${PARTITION_NAME_NO_EXT}"
    echo "${BOARD_ID}.menu.PartitionScheme.${PARTITION_SCHEME_ID}.upload.maximum_size=${UPLOAD_MAX_SIZE}"
  } >> "$BOARDS_TXT"

  echo "✓ Registered PartitionScheme '$PARTITION_SCHEME_ID' for board '$BOARD_ID'"
fi

echo "Done."