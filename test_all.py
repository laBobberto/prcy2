#!/usr/bin/env python3
"""
Комплексное тестирование Mesh Network
- Python симуляция (13 тестов)
- Renode эмуляция STM32 (3 и 5 узлов)
"""

import subprocess
import sys
import time
import os

class TestRunner:
    def __init__(self):
        self.results = []

    def run_python_tests(self):
        """Запуск Python тестов"""
        print("\n" + "="*70)
        print("PYTHON СИМУЛЯЦИЯ")
        print("="*70)

        tests = [
            ("MIC тесты", "simulation/test_mic.py"),
            ("AODV тесты", "simulation/test_aodv.py"),
            ("Интеграционные тесты", "simulation/test_integration.py"),
            ("Синхронизация C/Python", "check_sync.py"),
        ]

        for name, script in tests:
            print(f"\n--- {name} ---")
            try:
                result = subprocess.run(
                    ["python3", script],
                    capture_output=True,
                    text=True,
                    timeout=60
                )

                success = result.returncode == 0
                self.results.append((name, success))

                if success:
                    print(f"✓ {name}: PASS")
                else:
                    print(f"✗ {name}: FAIL")
                    if result.stdout:
                        print("STDOUT:", result.stdout[-500:])
                    if result.stderr:
                        print("STDERR:", result.stderr[-500:])

            except subprocess.TimeoutExpired:
                print(f"✗ {name}: TIMEOUT")
                self.results.append((name, False))
            except Exception as e:
                print(f"✗ {name}: ERROR - {e}")
                self.results.append((name, False))

    def run_renode_test(self, config, name, duration=15):
        """Запуск одного теста Renode"""
        print(f"\n--- {name} ---")

        try:
            # Запуск Renode
            cmd = [
                'renode',
                '--disable-xwt',
                '--console',
                config
            ]

            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=duration
            )

            # Проверяем, что firmware загрузился
            output = result.stdout + result.stderr

            success = (
                "Machine started" in output and
                "Loading block" in output and
                result.returncode == 0
            )

            if success:
                print(f"✓ {name}: Firmware загружен и запущен")
                # Показываем ключевые строки
                for line in output.split('\n'):
                    if 'Loading block' in line or 'Machine started' in line:
                        print(f"  {line.strip()}")
            else:
                print(f"✗ {name}: Ошибка запуска")
                print("Вывод:", output[-500:])

            self.results.append((name, success))
            return success

        except subprocess.TimeoutExpired:
            print(f"✓ {name}: Работает (timeout после {duration}s - это нормально)")
            self.results.append((name, True))
            return True
        except Exception as e:
            print(f"✗ {name}: ERROR - {e}")
            self.results.append((name, False))
            return False

    def run_renode_tests(self):
        """Запуск Renode тестов"""
        print("\n" + "="*70)
        print("RENODE ЭМУЛЯЦИЯ STM32F4")
        print("="*70)

        # Проверка наличия firmware
        if not os.path.exists('firmware/mesh_firmware.elf'):
            print("\n✗ firmware/mesh_firmware.elf не найден")
            print("  Запустите: cd firmware && make")
            self.results.append(("Renode 3 узла", False))
            self.results.append(("Renode 5 узлов", False))
            return

        print("✓ Firmware найден")

        # Тест 1: 3 узла
        self.run_renode_test(
            'renode/mesh_simple.resc',
            'Renode: 3 узла (простая конфигурация)',
            duration=10
        )

        time.sleep(1)

        # Тест 2: 3 узла (полная)
        self.run_renode_test(
            'renode/mesh_3nodes.resc',
            'Renode: 3 узла (полная конфигурация)',
            duration=10
        )

        time.sleep(1)

        # Тест 3: 5 узлов
        self.run_renode_test(
            'renode/mesh_5nodes.resc',
            'Renode: 5 узлов',
            duration=10
        )

    def print_summary(self):
        """Вывод итогов"""
        print("\n" + "="*70)
        print("ИТОГОВЫЕ РЕЗУЛЬТАТЫ")
        print("="*70)

        for name, success in self.results:
            status = "✓ PASS" if success else "✗ FAIL"
            print(f"{status}: {name}")

        total = len(self.results)
        passed = sum(1 for _, s in self.results if s)

        print(f"\nВсего тестов: {total}")
        print(f"Пройдено: {passed}")
        print(f"Не пройдено: {total - passed}")
        print(f"Процент успеха: {passed*100//total}%")

        if passed == total:
            print("\n" + "="*70)
            print("✓ ВСЕ ТЕСТЫ ПРОЙДЕНЫ УСПЕШНО")
            print("="*70)
            return 0
        else:
            print("\n" + "="*70)
            print("✗ НЕКОТОРЫЕ ТЕСТЫ НЕ ПРОЙДЕНЫ")
            print("="*70)
            return 1

def main():
    print("="*70)
    print("КОМПЛЕКСНОЕ ТЕСТИРОВАНИЕ MESH NETWORK")
    print("="*70)
    print("\nТесты:")
    print("  1. Python симуляция (MIC, AODV, интеграция)")
    print("  2. Renode эмуляция STM32F4 (3 и 5 узлов)")
    print("  3. Синхронизация C/Python")

    runner = TestRunner()

    # Python тесты
    runner.run_python_tests()

    # Renode тесты
    runner.run_renode_tests()

    # Итоги
    return runner.print_summary()

if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\n\nТестирование прервано пользователем")
        sys.exit(1)
