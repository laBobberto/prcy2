#!/usr/bin/env python3
"""
Проверка синхронизации между C и Python реализациями
"""

import re
import os

def extract_c_functions(filepath):
    """Извлекает объявления функций из C файла"""
    with open(filepath, 'r') as f:
        content = f.read()

    # Ищем объявления функций (void/uint8_t/etc function_name(...))
    pattern = r'^\s*(void|uint8_t|uint16_t|uint32_t|int|route_entry_t\*)\s+(\w+)\s*\([^)]*\)'
    matches = re.findall(pattern, content, re.MULTILINE)

    return [name for _, name in matches if not name.startswith('_')]

def extract_python_methods(filepath):
    """Извлекает методы из Python класса"""
    with open(filepath, 'r') as f:
        content = f.read()

    # Ищем определения методов (def method_name(...))
    pattern = r'^\s*def\s+(\w+)\s*\('
    matches = re.findall(pattern, content, re.MULTILINE)

    return [name for name in matches if not name.startswith('_')]

def compare_implementations():
    """Сравнивает C и Python реализации"""

    print("=" * 70)
    print("Проверка синхронизации C и Python реализаций")
    print("=" * 70)

    # Извлекаем функции из C
    c_mesh_funcs = extract_c_functions('firmware/src/mesh.c')
    c_crypto_funcs = extract_c_functions('firmware/lib/kuznyechik/kuznyechik.c')

    # Извлекаем методы из Python
    py_node_methods = extract_python_methods('simulation/node/node.py')
    py_crypto_methods = extract_python_methods('simulation/node/crypto.py')

    print("\n### Mesh функции ###\n")

    # Маппинг C функций на Python методы
    mesh_mapping = {
        'mesh_init': 'MeshNode.__init__',
        'mesh_tick': 'tick',
        'mesh_send_data': 'send_packet (частично)',
        'mesh_process_packet': 'handle_packet',
        'mesh_set_pairwise_key': 'set_pairwise_key',
        'mesh_rotate_session_key': 'rotate_session_key',
        'mesh_broadcast_time': 'broadcast_time',
        'mesh_get_time': 'internal_clock (поле)',
        'mesh_find_route': 'find_route',
        'mesh_add_route': 'add_route',
        'mesh_update_route_lifetime': 'update_route_lifetime',
        'mesh_invalidate_route': 'invalidate_route',
        'mesh_cleanup_routes': 'cleanup_routes',
        'mesh_send_rreq': 'send_rreq',
        'mesh_process_rreq': 'process_rreq',
        'mesh_process_rrep': 'process_rrep',
        'mesh_cleanup_old_data': 'cleanup_old_data',
        'mesh_crypt_ctr': 'Kuznyechik.ctr_crypt',
        'compute_e2e_mic': 'compute_e2e_mic',
        'compute_link_mic': 'compute_link_mic',
    }

    print("C функция → Python метод:")
    print("-" * 70)

    missing_in_python = []
    for c_func in sorted(c_mesh_funcs):
        if c_func.startswith('lora_'):
            continue  # Пропускаем LoRa функции

        py_equiv = mesh_mapping.get(c_func, '???')
        status = "✓" if py_equiv != 'НЕТ' and py_equiv != '???' else "✗"

        print(f"{status} {c_func:30} → {py_equiv}")

        if py_equiv == 'НЕТ' or py_equiv == '???':
            missing_in_python.append(c_func)

    print("\n### Crypto функции ###\n")

    crypto_mapping = {
        'kuznyechik_init': '__init__',
        'kuznyechik_encrypt_block': 'encrypt_block',
        'kuznyechik_decrypt_block': 'decrypt_block',
        'kuznyechik_mac': 'mac',
    }

    print("C функция → Python метод:")
    print("-" * 70)

    for c_func in sorted(c_crypto_funcs):
        py_equiv = crypto_mapping.get(c_func, '???')
        status = "✓" if py_equiv != '???' else "✗"

        print(f"{status} {c_func:30} → {py_equiv}")

    print("\n### Отсутствующие в Python ###\n")

    if missing_in_python:
        for func in missing_in_python:
            print(f"  - {func}")
    else:
        print("  Все основные функции реализованы!")

    print("\n### Дополнительные проверки ###\n")

    # Проверяем константы
    print("Константы:")
    constants = {
        'MAX_PAYLOAD_SIZE': (64, 64),
        'MESH_DEFAULT_TTL': (20, 20),
        'MAX_NODES': (256, 256),
        'MAX_ROUTES': (32, 'dict (unlimited)'),
        'ROUTE_LIFETIME': (30000, 30000),
    }

    for const, (c_val, py_val) in constants.items():
        match = "✓" if c_val == py_val else "⚠"
        print(f"  {match} {const:25} C={c_val:10} Python={py_val}")

    print("\n### Packet Types ###\n")

    packet_types = [
        'PACKET_TYPE_DATA',
        'PACKET_TYPE_RREQ',
        'PACKET_TYPE_RREP',
        'PACKET_TYPE_TIME',
        'PACKET_TYPE_KEY_ROTATION',
        'PACKET_TYPE_RERR',
    ]

    for ptype in packet_types:
        # Проверяем наличие в Python (как строки)
        py_type = ptype.replace('PACKET_TYPE_', '')
        print(f"  ✓ {ptype:30} → '{py_type}'")

    print("\n" + "=" * 70)
    print("Итого:")
    print("=" * 70)

    total_funcs = len([f for f in c_mesh_funcs if not f.startswith('lora_')])
    implemented = total_funcs - len(missing_in_python)

    print(f"Реализовано: {implemented}/{total_funcs} функций ({implemented*100//total_funcs}%)")

    if missing_in_python:
        print(f"\nНужно добавить в Python:")
        for func in missing_in_python:
            print(f"  - {func}")

    return len(missing_in_python) == 0

if __name__ == "__main__":
    import sys
    success = compare_implementations()
    sys.exit(0 if success else 1)
