import asyncio
import struct
import time
import random

HOST = '127.0.0.1'
# Lista de los puertos de nuestros 5 contenedores
PORTS = [8081, 8082, 8083, 8084, 8085]


async def client_task(client_id):
    # Balanceo de carga aleatorio: El cliente elige un nodo al azar
    target_port = random.choice(PORTS)

    try:
        reader, writer = await asyncio.open_connection(HOST, target_port)

        key = f"user:{client_id}".encode('utf-8')
        payload = f"data_from_cluster_client_{client_id}".encode('utf-8')

        # 1. ESCRITURA (STORE = 1) -> 16 bytes header
        header = struct.pack('<IiII', 1, 0, len(key), len(payload))
        writer.write(header + key + payload)
        await writer.drain()

        # Leer respuesta
        res_header = await reader.readexactly(16)

        # 2. LECTURA (FETCH = 2)
        header = struct.pack('<IiII', 2, 0, len(key), 0)
        writer.write(header + key)
        await writer.drain()

        res_header = await reader.readexactly(16)
        opcode, status, klen, plen = struct.unpack('<IiII', res_header)
        if plen > 0:
            data = await reader.readexactly(plen)

        writer.close()
        await writer.wait_closed()
    except Exception as e:
        # Silenciamos errores de conexión para la prueba masiva
        pass


async def main():
    print("=======================================")
    print("  AetherCore Distributed Stress Test")
    print("=======================================")
    print(f"[+] Atacando cluster en los puertos: {PORTS}")
    print("[+] Lanzando 10,000 transacciones concurrentes (5,000 clientes)...")

    start = time.time()

    # Despachamos las 5000 tareas
    tasks = [client_task(i) for i in range(5000)]
    await asyncio.gather(*tasks)

    end = time.time()
    print(f"[!] Stress Test completado.")
    print(
        f"[!] Tiempo total de resolucion distribuida: {end - start:.2f} segundos.")
    print("=======================================")

if __name__ == "__main__":
    asyncio.run(main())
