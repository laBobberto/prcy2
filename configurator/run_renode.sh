#!/bin/bash
# Запуск графического конфигуратора Renode (PyQt5)

echo "╔══════════════════════════════════════════════════════════════════╗"
echo "║      Mesh Network Configurator - Renode Edition (PyQt5)          ║"
echo "╚══════════════════════════════════════════════════════════════════╝"
echo ""

# Ищем Python с PyQt5
PYTHON_CMD=""

for cmd in python3.14 python3.13 python3.12 python3.11 python3.10 python3; do
    if command -v $cmd &> /dev/null; then
        if $cmd -c "import PyQt5" 2>/dev/null; then
            PYTHON_CMD=$cmd
            break
        fi
    fi
done

if [ -z "$PYTHON_CMD" ]; then
    echo "✗ PyQt5 не найден ни в одной версии Python"
    echo ""
    echo "Установите PyQt5:"
    echo "  pip install PyQt5"
    echo "  или"
    echo "  pip3.14 install PyQt5"
    exit 1
fi

echo "✓ PyQt5 найден ($PYTHON_CMD)"
echo ""
echo "Запуск Renode-конфигуратора..."
echo ""

cd "$(dirname "$0")"
$PYTHON_CMD pyqt_renode.py
