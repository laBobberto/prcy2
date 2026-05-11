# Установка PyQt5

## Для запуска графической версии конфигуратора необходимо установить PyQt5

### Установка через pip (рекомендуется):

```bash
pip install PyQt5
```

Или для пользователя:

```bash
pip install --user PyQt5
```

### Установка через системный менеджер пакетов:

**Fedora/RHEL:**
```bash
sudo dnf install python3-pyqt5
```

**Ubuntu/Debian:**
```bash
sudo apt install python3-pyqt5
```

**Arch:**
```bash
sudo pacman -S python-pyqt5
```

### Проверка установки:

```bash
python3 -c "import PyQt5; print('PyQt5 установлен')"
```

### Запуск конфигуратора:

```bash
cd configurator
./run_qt.sh
```

## Альтернативы (если PyQt5 не устанавливается):

1. **TUI версия** (без зависимостей):
   ```bash
   python3 mesh_configurator_tui.py
   ```

2. **TMUX версия** (требует tmux):
   ```bash
   ./run_tmux.sh
   ```

3. **Tkinter версия** (требует tkinter):
   ```bash
   ./run.sh
   ```
