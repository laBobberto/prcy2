#!/bin/bash
# Flash script for STM32F103 + SX1278 Mesh Network
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

usage() {
    echo -e "${CYAN}Mesh Network Flasher${NC}"
    echo ""
    echo "Usage: $0 [command]"
    echo ""
    echo "Commands:"
    echo "  1, node1     Flash Node 1 (sender)"
    echo "  2, node2     Flash Node 2 (loopback/echo)"
    echo "  all          Flash both nodes sequentially"
    echo "  build        Build only, don't flash"
    echo "  monitor      Open serial monitor"
    echo "  clean        Clean build artifacts"
    echo ""
    echo "Examples:"
    echo "  $0 1         # Flash Node 1"
    echo "  $0 2         # Flash Node 2"
    echo "  $0 all       # Flash both"
    echo "  $0 build     # Just build"
    echo "  $0 monitor   # Serial monitor"
}

check_pio() {
    if ! command -v pio &> /dev/null; then
        echo -e "${RED}Error: PlatformIO not found.${NC}"
        echo "Install: pip install platformio"
        exit 1
    fi
}

build_all() {
    echo -e "${CYAN}[BUILD] Compiling firmware...${NC}"
    pio run
    echo -e "${GREEN}[BUILD] Done.${NC}"
}

flash_node() {
    local env=$1
    local name=$2
    echo -e "${CYAN}[FLASH] Flashing ${name} (env: ${env})...${NC}"
    pio run -e "$env" -t upload
    echo -e "${GREEN}[FLASH] ${name} done.${NC}"
}

monitor() {
    echo -e "${CYAN}[MONITOR] Opening serial monitor (115200 baud)...${NC}"
    echo -e "${YELLOW}Press Ctrl+C to exit${NC}"
    pio device monitor -b 115200
}

clean() {
    echo -e "${CYAN}[CLEAN] Removing build artifacts...${NC}"
    pio run -t clean
    rm -rf .pio/build
    echo -e "${GREEN}[CLEAN] Done.${NC}"
}

check_pio

case "${1:-}" in
    1|node1)
        flash_node "node1" "Node 1 (Sender)"
        ;;
    2|node2)
        flash_node "node2" "Node 2 (Loopback)"
        ;;
    all)
        build_all
        echo ""
        flash_node "node1" "Node 1 (Sender)"
        echo ""
        echo -e "${YELLOW}Connect Node 2 and press Enter...${NC}"
        read -r
        flash_node "node2" "Node 2 (Loopback)"
        ;;
    build)
        build_all
        ;;
    monitor)
        monitor
        ;;
    clean)
        clean
        ;;
    *)
        usage
        ;;
esac
