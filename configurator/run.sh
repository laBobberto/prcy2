#!/bin/bash
# Быстрый запуск конфигуратора mesh-сети

echo "╔══════════════════════════════════════════════════════════════════╗"
echo "║           Mesh Network Configurator                              ║"
echo "╚══════════════════════════════════════════════════════════════════╝"
echo ""

# Проверка tkinter
if ! python3 -c "import tkinter" 2>/dev/null; then
    echo "✗ tkinter не установлен"
    echo ""
    echo "Установите tkinter:"
    echo "  Fedora/RHEL: sudo dnf install python3-tkinter"
    echo "  Ubuntu/Debian: sudo apt install python3-tk"
    echo "  Arch: sudo pacman -S tk"
    exit 1
fi

echo "✓ tkinter установлен"
echo ""
echo "Запуск конфигуратора..."
echo ""

cd "$(dirname "$0")"
python3 mesh_configurator.py
