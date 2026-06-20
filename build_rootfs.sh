#!/bin/bash

# Directorio de destino para la jaula del contenedor
ROOTFS_DIR="/tmp/aether_rootfs"

# Binario fuente de AetherCore
BIN_PATH="build/aethercore"

if [ ! -f "$BIN_PATH" ]; then
    echo "[!] Error: No se encontro el binario '$BIN_PATH'. Ejecuta 'make' en build/ primero."
    exit 1
fi

echo "[+] Limpiando y creando jerarquia RootFS en $ROOTFS_DIR..."
rm -rf "$ROOTFS_DIR"
mkdir -p "$ROOTFS_DIR/bin"

# Copiar el ejecutable principal y renombrarlo
cp "$BIN_PATH" "$ROOTFS_DIR/bin/aether_server"

echo "[+] Resolviendo dependencias compartidas (ldd)..."

# Copiar todas las librerías dinámicas listadas por ldd
ldd "$BIN_PATH" | grep "=> /" | awk '{print $3}' | while read -r lib; do
    cp --parents "$lib" "$ROOTFS_DIR"
done

# Copiar explícitamente el cargador dinámico (ej. /lib64/ld-linux-x86-64.so.2)
ldd "$BIN_PATH" | grep "/ld-linux" | awk '{print $1}' | while read -r ld_loader; do
    cp --parents "$ld_loader" "$ROOTFS_DIR"
done

echo "[+] RootFS preparado con exito. Listo para pivot_root."