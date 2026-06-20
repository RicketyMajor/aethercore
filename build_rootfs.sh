# Directorio de destino para la jaula del contenedor
ROOTFS_DIR="/tmp/aether_rootfs"
BIN_PATH="build/aethercore"

echo "[+] Limpiando y creando jerarquia RootFS en $ROOTFS_DIR..."
rm -rf "$ROOTFS_DIR"
mkdir -p "$ROOTFS_DIR/bin"

# Función maestra para copiar un binario y resolver sus librerías con ldd
copy_bin_and_deps() {
    local bin=$1
    local dest_name=$2
    
    if [ -f "$bin" ]; then
        if [ -n "$dest_name" ]; then
            cp "$bin" "$ROOTFS_DIR/bin/$dest_name"
            echo "  -> Copiando $dest_name y sus dependencias..."
        else
            cp "$bin" "$ROOTFS_DIR/bin/"
            echo "  -> Copiando $bin y sus dependencias..."
        fi
        
        # Copiar librerías dinámicas
        ldd "$bin" | grep "=> /" | awk '{print $3}' | while read -r lib; do
            cp --parents "$lib" "$ROOTFS_DIR" 2>/dev/null
        done
        
        # Copiar el cargador de Linux
        ldd "$bin" | grep "/ld-linux" | awk '{print $1}' | while read -r ld_loader; do
            cp --parents "$ld_loader" "$ROOTFS_DIR" 2>/dev/null
        done
    fi
}

echo "[+] Empaquetando Motor AetherCore..."
if [ -f "$BIN_PATH" ]; then
    copy_bin_and_deps "$BIN_PATH" "aether_server"
else
    echo "[!] Advertencia: Binario de AetherCore no encontrado. Compila primero."
fi

echo "[+] Empaquetando utilidades de debugging del sistema..."
copy_bin_and_deps "/bin/sh" ""
copy_bin_and_deps "/bin/ls" ""
copy_bin_and_deps "/bin/ps" ""
copy_bin_and_deps "/bin/hostname" ""

echo "[+] RootFS preparado con exito. Listo para aislar."