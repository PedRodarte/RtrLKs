#!/usr/bin/env python3
"""Teste ponta a ponta da tag pelo Bluetooth do PC (Windows), fazendo o papel do celular.

Requisitos:
  - Windows com Bluetooth LE; pip install bleak pyserial
  - Firmware com CONFIG_FINDLK_TEST_LOG_PASSKEY=y (o script lê o passkey no log serial)
  - Tag sem nenhum celular pareado (o PC precisa ser o "primeiro"), ou janela aberta pelo BOOT
  - Lista de Wi-Fi de casa vazia (em casa a tag não arma)

Uso:  python tools/test_ble_pc.py --port COM3 [--unpair]

Ao final o PC é despareado (--unpair) para não ocupar o lugar do celular.
"""

import argparse
import asyncio
import re
import sys
import threading
import time

import serial
from bleak import BleakClient, BleakScanner
from winrt.windows.devices.enumeration import (
    DeviceInformation,
    DevicePairingKinds,
    DevicePairingProtectionLevel,
    DevicePairingResultStatus,
)

BASE = "-1b8e-4f2a-9d6e-3c5b7e9f0a12"
SVC = "a7c30001" + BASE
HEARTBEAT = "a7c30002" + BASE
COMING = "a7c30003" + BASE
COMMAND = "a7c30004" + BASE
STATE = "a7c30005" + BASE
CONFIG = "a7c30006" + BASE
STATE_NAMES = {0: "DESARMADO", 1: "ARMADO", 2: "PERDIDO", 3: "CHEGANDO"}


class SerialLog(threading.Thread):
    """Lê o log serial em segundo plano e guarda as linhas."""

    def __init__(self, port):
        super().__init__(daemon=True)
        self.port = port
        self.lines = []
        self.passkey = None
        self.passkey_event = threading.Event()
        self.stop = False

    def run(self):
        s = serial.Serial(self.port, 115200, timeout=0.2)
        buf = b""
        while not self.stop:
            buf += s.read(4096)
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                line = raw.decode("utf-8", "replace").strip()
                self.lines.append((time.time(), line))
                m = re.search(r"TESTE passkey=(\d{6})", line)
                if m:
                    self.passkey = m.group(1)
                    self.passkey_event.set()
        s.close()

    def find(self, pattern, since=0.0):
        return [l for t, l in self.lines if t >= since and re.search(pattern, l)]


results = []


def check(name, ok, detail=""):
    results.append((name, ok, detail))
    print(f"[{'OK ' if ok else 'FALHOU'}] {name}" + (f" — {detail}" if detail else ""))


async def pair_with_passkey(client, log):
    info = await DeviceInformation.create_from_id_async(client._backend._requester.device_information.id)
    if info.pairing.is_paired:
        return "ALREADY_PAIRED"
    custom = info.pairing.custom

    def on_request(sender, args):
        # O passkey é gerado pela tag e aparece no log serial pouco antes
        if log.passkey_event.wait(15):
            args.accept_with_pin(log.passkey)

    token = custom.add_pairing_requested(on_request)
    try:
        result = await custom.pair_with_protection_level_async(
            DevicePairingKinds.PROVIDE_PIN, DevicePairingProtectionLevel.ENCRYPTION_AND_AUTHENTICATION
        )
    finally:
        custom.remove_pairing_requested(token)
    return result.status.name


async def wait_state(states, expected, timeout):
    """Espera uma notificação do Estado com o valor esperado; devolve o tempo gasto ou None."""
    start = time.time()
    while time.time() - start < timeout:
        if states and states[-1][1] == expected:
            return states[-1][0] - start
        await asyncio.sleep(0.2)
    return None


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM3")
    ap.add_argument("--unpair", action="store_true", help="desparear o PC ao final")
    args = ap.parse_args()

    log = SerialLog(args.port)
    log.start()
    print("Aguardando a tag iniciar...")
    await asyncio.sleep(4)

    device = await BleakScanner.find_device_by_filter(
        lambda d, adv: SVC in [u.lower() for u in adv.service_uuids], timeout=15
    )
    check("Tag encontrada no scan", device is not None, device and f"{device.name} {device.address}")
    if not device:
        return

    async with BleakClient(device) as client:
        # 1. Sem pareamento, escrita é recusada
        try:
            await client.write_gatt_char(HEARTBEAT, b"sem-par", response=True)
            check("Escrita sem pareamento recusada", False, "foi aceita")
        except Exception as e:
            check("Escrita sem pareamento recusada", "Authentication" in str(e) or "Encryption" in str(e), str(e)[:80])

        # 2. Pareamento com o passkey da tela (lido do log)
        status = await pair_with_passkey(client, log)
        check("Pareamento com passkey", status in ("PAIRED", "ALREADY_PAIRED"), f"{status}, passkey {log.passkey}")
        if status not in ("PAIRED", "ALREADY_PAIRED"):
            return
        await asyncio.sleep(2)

        # 3. Estado: leitura e notificações
        states = []
        await client.start_notify(STATE, lambda _, data: states.append((time.time(), data[0])))
        initial = (await client.read_gatt_char(STATE))[0]
        check("Leitura do Estado pareado", initial in STATE_NAMES, STATE_NAMES.get(initial, initial))
        if initial != 0:
            await client.write_gatt_char(COMMAND, b"DESARMAR", response=True)
            await asyncio.sleep(1)

        # 4. Armar com heartbeat
        await client.write_gatt_char(HEARTBEAT, "Teste PC".encode(), response=True)
        check("Heartbeat arma (00 -> 01)", await wait_state(states, 1, 5) is not None)

        # 5. Heartbeats a cada 5 s mantêm armado
        for _ in range(4):
            await asyncio.sleep(5)
            await client.write_gatt_char(HEARTBEAT, "Teste PC".encode(), response=True)
        check("Heartbeats mantêm ARMADO por 20 s", states[-1][1] == 1)

        # 6. Sem heartbeat -> PERDIDO em ~20 s
        t0 = time.time()
        elapsed = await wait_state(states, 2, 35)
        lost_after = elapsed is not None and states[-1][0] - t0
        check("Sem heartbeat vai a PERDIDO (02)", elapsed is not None,
              f"após {lost_after:.0f} s" if elapsed is not None else "não chegou em 35 s")

        # 7. Chegando -> 03, com proximidade
        mark = time.time()
        await client.write_gatt_char(COMING, "Pai".encode(), response=True)
        check("Chegando leva a CHEGANDO (03)", await wait_state(states, 3, 5) is not None)
        for _ in range(2):
            await asyncio.sleep(4)
            await client.write_gatt_char(HEARTBEAT, "Pai".encode(), response=True)
        prox = log.find(r"proximidade: nível", since=0)
        check("Proximidade medida por RSSI", bool(prox), prox[-1].split("tag_ble: ")[-1] if prox else "sem leitura")
        check("Continua em CHEGANDO com heartbeats", states[-1][1] == 3)

        # 8. Comando desconhecido e texto longo demais
        await client.write_gatt_char(COMMAND, b"XYZ", response=True)
        await asyncio.sleep(1)
        check("Comando desconhecido ignorado", states[-1][1] == 3 and bool(log.find(r"comando desconhecido", mark)))
        try:
            await client.write_gatt_char(HEARTBEAT, b"x" * 40, response=True)
            check("Texto com mais de 32 bytes recusado", False, "foi aceito")
        except Exception as e:
            check("Texto com mais de 32 bytes recusado", True, str(e)[:60])

        # 9. Configuração do QR pelo app: nome, telefone, validação, leitura da URL montada
        await client.write_gatt_char(CONFIG, "n=Ana Júlia".encode(), response=True)
        await client.write_gatt_char(CONFIG, b"t=5531999998888", response=True)
        url = (await client.read_gatt_char(CONFIG)).decode()
        check("Configuração: URL do QR com nome e telefone",
              "?n=Ana%20J%C3%BAlia&t=5531999998888&d=" in url, url)
        for bad, why in ((b"t=12ab", "telefone inválido"), (b"u=http://inseguro", "URL sem HTTPS"), (b"x=1", "chave desconhecida")):
            try:
                await client.write_gatt_char(CONFIG, bad, response=True)
                check(f"Configuração recusa {why}", False, "foi aceito")
            except Exception as e:
                check(f"Configuração recusa {why}", True, str(e)[:50])
        await client.write_gatt_char(CONFIG, b"n=", response=True)
        await client.write_gatt_char(CONFIG, b"t=", response=True)
        url = (await client.read_gatt_char(CONFIG)).decode()
        check("Configuração: limpar nome e telefone", "n=" not in url and "t=" not in url, url)

        # 10. Encontrado -> 01; Desarmar -> 00
        await client.write_gatt_char(COMMAND, b"ENCONTRADO", response=True)
        check("ENCONTRADO volta a ARMADO (01)", await wait_state(states, 1, 5) is not None)
        await client.write_gatt_char(COMMAND, b"DESARMAR", response=True)
        check("DESARMAR leva a DESARMADO (00)", await wait_state(states, 0, 5) is not None)

        transitions = log.find(r"app: \w+ -> \w+")
        print("\nTransições no log da tag:")
        for t in transitions:
            print("   ", t.split("app: ")[-1])

        if args.unpair:
            await client.unpair()
            print("\nPC despareado no Windows.")

    log.stop = True
    ok = sum(1 for _, o, _ in results if o)
    print(f"\nResultado: {ok}/{len(results)} verificações OK")
    sys.exit(0 if ok == len(results) else 1)


if __name__ == "__main__":
    asyncio.run(main())
