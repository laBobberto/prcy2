#!/bin/bash

# Убиваем старые процессы если они есть
pkill -f "python3 node.py"

echo "Запуск Мэш-сети (3 узла)..."

# Узел 2 (Дальний)
NODE_ID=NODE2 PORT=5002 NEIGHBORS=5001 python3 node/node.py > node2.log 2>&1 &
echo "Узел 2 запущен на порту 5002"

# Узел 1 (Посредник)
NODE_ID=NODE1 PORT=5001 NEIGHBORS=5000,5002 python3 node/node.py > node1.log 2>&1 &
echo "Узел 1 запущен на порту 5001"

# Шлюз (Ближний к пользователю)
echo "Запуск Шлюза... Вы сможете вводить сообщения."
echo "Формат: <ID_ПОЛУЧАТЕЛЯ> <СООБЩЕНИЕ>"
echo "Например: NODE2 Hello_Mesh!"
echo "------------------------------------------------"
NODE_ID=GATEWAY PORT=5000 NEIGHBORS=5001 python3 node/node.py
