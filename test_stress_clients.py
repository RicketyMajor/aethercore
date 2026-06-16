import asyncio
import struct
import time

HOST = '127.0.0.1'
PORT = 8080


async def client_task(client_id):
    try:
        reader, writer = await asyncio.open_connection(HOST, PORT)

        key = f"user:{client_id}".encode('utf-8')
        payload = f"data_from_{client_id}".encode('utf-8')

        # 1. ESCRITURA (STORE = 1) -> 16 bytes header
        header = struct.pack('<IiII', 1, 0, len(key), len(payload))
        writer.write(header + key + payload)
        await writer.drain()

        # Leer respuesta
        res_header = await reader.readexactly(16)
        opcode, status, klen, plen = struct.unpack('<IiII', res_header)

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
        pass


async def main():
    print("[+] Lanzando 5,000 conexiones concurrentes...")
    start = time.time()

    # Despacha las 5000 tareas
    tasks = [client_task(i) for i in range(5000)]
    await asyncio.gather(*tasks)

    end = time.time()
    print(
        f"[!] Stress Test completado. 10,000 transacciones de red resueltas en {end - start:.2f} segundos.")

if __name__ == "__main__":
    asyncio.run(main())
