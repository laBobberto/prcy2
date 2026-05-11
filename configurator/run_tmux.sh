#!/bin/bash
# Запуск конфигуратора mesh-сети с tmux логами

echo "╔══════════════════════════════════════════════════════════════════╗"
echo "║           Mesh Network Configurator (TMUX)                       ║"
echo "╚══════════════════════════════════════════════════════════════════╝"
echo ""

# Проверка tmux
if ! command -v tmux &> /dev/null; then
    echo "✗ tmux не установлен"
    echo ""
    echo "Установите tmux:"
    echo "  Fedora/RHEL: sudo dnf install tmux"
    echo "  Ubuntu/Debian: sudo apt install tmux"
    echo "  Arch: sudo pacman -S tmux"
    exit 1
fi

echo "✓ tmux установлен"
echo ""
echo "Запуск конфигуратора с поддержкой отдельных окон для логов..."
echo ""

cd "$(dirname "$0")"
python3 mesh_configurator_tmux.py
