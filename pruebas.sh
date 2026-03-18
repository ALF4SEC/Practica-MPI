# Verificamos que el usuario haya proporcionado el número de cpus que tiene la máquina como argumento
if [ $# -ne 1 ]; then
    echo "Uso: $0 <num_cpus_fisicos_maquina>"
    echo "Nota: El número de cpus físicos de la máquina se puede obtener con el comando 'lscpu' o 'nproc --all'."
    exit 1
fi
NUM_CPUS=$1

# Recuperamos el nombre de la máquina en la que se están ejecutando las pruebas
MAQUINA=$(uname -a | awk '{print $2}')

# En caso de que no se pueda recuperar el nombre de la máquina, se asigna un valor por defecto
if [ -z "$MAQUINA" ]; then
    MAQUINA="Desconocida"
fi

if command -v lscpu >/dev/null 2>&1; then
    lscpu >> Resultados/${MAQUINA}/info_maquina.txt
else
    echo "No se pudo obtener información detallada de la máquina. Asegúrate de tener el comando 'lscpu' disponible." >> Resultados/${MAQUINA}/info_maquina.txt
fi

# En caso de no existir el directorio de resultados para la máquina, se crea
if [ ! -d "Resultados/${MAQUINA}" ]; then
    mkdir -p Resultados/${MAQUINA}/Pergamino
    mkdir -p Resultados/${MAQUINA}/Abadia
    mkdir -p Resultados/${MAQUINA}/En
fi

# Compilamos el código
make

# Ejecutamos el programa con diferentes casos de prueba
echo "###################"
echo "Pruebas PERGAMINO"
echo "###################"
echo ""
i=1
while [ $i -le $NUM_CPUS ]; do
    echo "Ejecutando PERGAMINO con $i buscadores"
    mpirun -np $(($i+1)) practica_mpi PERGAMINO >> Resultados/${MAQUINA}/Pergamino/$((i))ProcBusc.txt
    i=$((i*2))
done
if [ $i -ne $NUM_CPUS ]; then
    echo "Ejecutando PERGAMINO con $NUM_CPUS buscadores"
    mpirun -np $NUM_CPUS practica_mpi PERGAMINO >> Resultados/${MAQUINA}/Pergamino/$((NUM_CPUS))ProcBusc.txt
fi
while [ $i -le 64 ]; do
    echo "Ejecutando PERGAMINO con $i buscadores"
    mpirun --oversubscribe -np $((i+1)) practica_mpi PERGAMINO >> Resultados/${MAQUINA}/Pergamino/$((i))ProcBusc.txt
    i=$((i*2))
done

echo "###################"
echo "Pruebas ABADIA"
echo "###################"
echo ""
i=1
while [ $i -le $NUM_CPUS ]; do
    echo "Ejecutando ABADIA con $i buscadores"
    mpirun -np $((i+1)) practica_mpi ABADIA >> Resultados/${MAQUINA}/Abadia/$((i))ProcBusc.txt
    i=$((i*2))
done
if [ $i -ne $NUM_CPUS ]; then
    echo "Ejecutando ABADIA con $NUM_CPUS buscadores"
    mpirun -np $NUM_CPUS practica_mpi ABADIA >> Resultados/${MAQUINA}/Abadia/$((NUM_CPUS))ProcBusc.txt
fi
while [ $i -le 64 ]; do
    echo "Ejecutando ABADIA con $i buscadores"
    mpirun --oversubscribe -np $((i+1)) practica_mpi ABADIA >> Resultados/${MAQUINA}/Abadia/$((i))ProcBusc.txt
    i=$((i*2))
done

echo "###################"
echo "Pruebas EN"
echo "###################"
echo ""
i=1
while [ $i -le $NUM_CPUS ]; do
    echo "Ejecutando EN con $i buscadores"
    mpirun -np $((i+1)) practica_mpi EN >> Resultados/${MAQUINA}/En/$((i))ProcBusc.txt
    i=$((i*2))
done
if [ $i -ne $NUM_CPUS ]; then
    echo "Ejecutando EN con $NUM_CPUS buscadores"
    mpirun -np $NUM_CPUS practica_mpi EN >> Resultados/${MAQUINA}/En/$((NUM_CPUS))ProcBusc.txt
fi
while [ $i -le 64 ]; do
    echo "Ejecutando EN con $i buscadores"
    mpirun --oversubscribe -np $((i+1)) practica_mpi EN >> Resultados/${MAQUINA}/En/$((i))ProcBusc.txt
    i=$((i*2))
done

unset MAQUINA
unset NUM_CPUS
unset i
