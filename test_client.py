import socket
import struct
import time

# Constantes del Servidor
HOST = '127.0.0.1'
PORT = 8080

# Opcodes (1 = STORE, 2 = FETCH, 3 = REMOVE)
OPCODE_STORE = 1


def send_binary_message(sock, opcode, key_str, payload_str):
    key_bytes = key_str.encode('utf-8')
    payload_bytes = payload_str.encode('utf-8')

    key_len = len(key_bytes)
    payload_len = len(payload_bytes)

    # Empaqueta el AetherHeader: <III significa Little-Endian, 3 Unsigned Ints (3 * 4 bytes = 12 bytes)
    header = struct.pack('<III', opcode, key_len, payload_len)

    # Construye el paquete completo
    packet = header + key_bytes + payload_bytes

    # Envía la data cruda por el socket
    sock.sendall(packet)
    print(
        f"[*] Enviado paquete binario de {len(packet)} bytes (Opcode: {opcode}, Key: {key_str}, Payload: {payload_str})")


if __name__ == "__main__":
    print("[+] Conectando al AetherCore Broker...")
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((HOST, PORT))

        # Envía un par de paquetes para probar el Pipelining (varios mensajes de golpe)
        send_binary_message(s, OPCODE_STORE, "usuario:99", "Alonso Vera")
        send_binary_message(s, OPCODE_STORE, "config:theme", "Dark_Mode")

        # Mantiene el socket abierto un segundo para ver la salida
        time.sleep(1)
