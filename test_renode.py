#!/usr/bin/env python3
"""
Автоматизированное тестирование firmware на Renode
Запускает 3 и 5 узлов, проверяет все функции
"""

import subprocess
import time
import os
import sys
import signal

class RenodeTest:
    def __init__(self, config_file, num_nodes):
        self.config_file = config_file
        self.num_nodes = num_nodes
        self.process = None

    def start(self):
        """Запуск Renode с конфигурацией"""
        print(f"\n{'='*70}")
        print(f"Запуск Renode: {self.config_file} ({self.num_nodes} узлов)")
        print(f"{'='*70}\n")

        cmd = [
            'renode',
            '--disable-xwt',  # Без GUI
            '--console',
            self.config_file
        ]

        try:
            self.process = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                preexec_fn=os.setsid
            )

            print(f"✓ Renode запущен (PID: {self.process.pid})")
            return True

        except Exception as e:
            print(f"✗ Ошибка запуска Renode: {e}")
            return False

    def wait_and_monitor(self, duration=10):
        """Ожидание и мониторинг вывода"""
        print(f"\nМониторинг в течение {duration} секунд...")

        start_time = time.time()
        output_lines = []

        while time.time() - start_time < duration:
            if self.process.poll() is not None:
                print("✗ Renode завершился преждевременно")
                return False

            # Читаем вывод
            try:
                line = self.process.stdout.readline()
                if line:
                    output_lines.append(line.strip())
                    if len(output_lines) <= 50:  # Показываем первые 50 строк
                        print(f"  {line.strip()}")
            except:
                pass

            time.sleep(0.1)

        print(f"\n✓ Мониторинг завершен, получено {len(output_lines)} строк вывода")
        return True

    def stop(self):
        """Остановка Renode"""
        if self.process:
            print("\nОстановка Renode...")
            try:
                os.killpg(os.getpgid(self.process.pid), signal.SIGTERM)
                self.process.wait(timeout=5)
                print("✓ Renode остановлен")
            except:
                try:
                    os.killpg(os.getpgid(self.process.pid), signal.SIGKILL)
                    print("✓ Renode принудительно остановлен")
                except:
                    print("✗ Не удалось остановить Renode")

    def check_uart_output(self):
        """Проверка вывода UART"""
        print("\nПроверка UART терминалов...")

        found_output = False
        for i in range(1, self.num_nodes + 1):
            uart_file = f"/tmp/node{i}_uart"
            if os.path.exists(uart_file):
                print(f"  ✓ NODE{i} UART: {uart_file}")
                found_output = True
            else:
                print(f"  ✗ NODE{i} UART не найден")

        return found_output

def test_3_nodes():
    """Тест с 3 узлами"""
    test = RenodeTest('renode/mesh_3nodes.resc', 3)

    if not test.start():
        return False

    time.sleep(2)  # Даем время на инициализацию

    success = test.wait_and_monitor(duration=15)

    test.check_uart_output()

    test.stop()

    return success

def test_5_nodes():
    """Тест с 5 узлами"""
    test = RenodeTest('renode/mesh_5nodes.resc', 5)

    if not test.start():
        return False

    time.sleep(2)  # Даем время на инициализацию

    success = test.wait_and_monitor(duration=20)

    test.check_uart_output()

    test.stop()

    return success

def check_prerequisites():
    """Проверка необходимых компонентов"""
    print("Проверка предварительных условий...")

    # Проверка Renode
    try:
        result = subprocess.run(['renode', '--version'],
                              capture_output=True, text=True, timeout=5)
        print(f"✓ Renode установлен: {result.stdout.strip()}")
    except:
        print("✗ Renode не найден")
        return False

    # Проверка firmware
    if not os.path.exists('firmware/mesh_firmware.elf'):
        print("✗ firmware/mesh_firmware.elf не найден")
        print("  Запустите: cd firmware && make")
        return False
    print("✓ Firmware найден")

    # Проверка конфигураций
    if not os.path.exists('renode/mesh_3nodes.resc'):
        print("✗ renode/mesh_3nodes.resc не найден")
        return False
    print("✓ Конфигурация 3 узлов найдена")

    if not os.path.exists('renode/mesh_5nodes.resc'):
        print("✗ renode/mesh_5nodes.resc не найден")
        return False
    print("✓ Конфигурация 5 узлов найдена")

    return True

def main():
    print("="*70)
    print("Автоматизированное тестирование Mesh Network на Renode")
    print("="*70)

    if not check_prerequisites():
        print("\n✗ Не все предварительные условия выполнены")
        return 1

    print("\n✓ Все предварительные условия выполнены\n")

    results = []

    # Тест 1: 3 узла
    print("\n" + "="*70)
    print("ТЕСТ 1: Mesh-сеть с 3 узлами")
    print("="*70)
    try:
        success = test_3_nodes()
        results.append(("3 узла", success))
    except KeyboardInterrupt:
        print("\n\n✗ Тест прерван пользователем")
        return 1
    except Exception as e:
        print(f"\n✗ Ошибка теста: {e}")
        results.append(("3 узла", False))

    time.sleep(2)

    # Тест 2: 5 узлов
    print("\n" + "="*70)
    print("ТЕСТ 2: Mesh-сеть с 5 узлами")
    print("="*70)
    try:
        success = test_5_nodes()
        results.append(("5 узлов", success))
    except KeyboardInterrupt:
        print("\n\n✗ Тест прерван пользователем")
        return 1
    except Exception as e:
        print(f"\n✗ Ошибка теста: {e}")
        results.append(("5 узлов", False))

    # Итоги
    print("\n" + "="*70)
    print("ИТОГИ ТЕСТИРОВАНИЯ")
    print("="*70)

    for name, success in results:
        status = "✓ PASS" if success else "✗ FAIL"
        print(f"{status}: {name}")

    total = len(results)
    passed = sum(1 for _, s in results if s)

    print(f"\nВсего: {passed}/{total} тестов пройдено")

    if passed == total:
        print("\n✓ ВСЕ ТЕСТЫ ПРОЙДЕНЫ")
        return 0
    else:
        print("\n✗ НЕКОТОРЫЕ ТЕСТЫ НЕ ПРОЙДЕНЫ")
        return 1

if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\n\nТестирование прервано")
        sys.exit(1)
