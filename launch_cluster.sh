echo "======================================="
echo "  AetherCore Cluster Orchestrator"
echo "======================================="

# Crear carpeta para los logs si no existe
mkdir -p logs

echo "[+] Levantando malla de 5 nodos virtuales..."

for i in {1..5}; do
    PORT=$((8080 + i))
    echo "  -> Instanciando Nodo $i (Puerto TCP: $PORT, RAM Limit: 256MB)"
    
    # Ejecutamos el contenedor en segundo plano (&) y pasamos el puerto por variable de entorno
    sudo env AETHER_PORT=$PORT ./build/aether_run --memory 256M -- /bin/aether_server > /dev/null 2>&1 &
done

echo ""
echo "[OK] Cluster distribuido en ejecucion."
echo "[!] Usa 'htop' en otra terminal y busca 'aether_server' para ver los procesos asilados."
echo "[!] Para apagar todo el cluster, ejecuta: sudo pkill aether_server"
echo "======================================="