# Práctica de MPI: descifrado por fuerza bruta con pistas

Primera práctica de Arquitectura de Computadores (Grado en Ingeniería Informática, curso 2025-2026). Es un programa en C con MPI que descifra por fuerza bruta un mensaje cifrado con XOR, repartiendo la búsqueda de la clave entre varios procesos. Además incluye un script para medir cómo escala con distinto número de procesos y los resultados obtenidos en cuatro máquinas.

Autores: Alfonso Crego Calvo, Hugo Chalard Collado, David Lavado González y Rubén Hernández Molina.

## Problema

El mensaje original es el comienzo de *El nombre de la rosa* y se cifra con XOR usando una clave de 6 letras mayúsculas (`GTYHUY`). Los procesos buscadores no conocen la clave: prueban claves al azar, descifran el mensaje con cada una y comprueban si en el resultado aparece una palabra que se da como pista. Cuanto más larga y menos frecuente es la pista, menos falsos positivos hay.

Pistas admitidas: `EN`, `ABADIA` y `PERGAMINO`.

## Reparto del trabajo

**Proceso 0 (entrada/salida)**

- Cifra el mensaje y envía a todos, con `MPI_Bcast`, el tamaño de los datos, el mensaje cifrado y la pista.
- Espera consultas de cualquier buscador con `MPI_Probe`. Cuando un buscador le propone una clave, responde si es la correcta. Cuando alguien acierta, envía una orden de parada al resto.
- Al final recoge las estadísticas de cada buscador y las imprime.

**Procesos buscadores (del 1 al N)**

- Generan claves aleatorias con una semilla distinta en cada proceso, para que no repitan las mismas claves.
- Si la pista aparece en el texto descifrado, mandan la clave al proceso 0 y esperan su respuesta.
- Si no aparece, cada 10.000 intentos miran con `MPI_Iprobe` si ha llegado la orden de parada. Así no hacen una llamada MPI en cada intento.
- Antes de terminar leen cualquier orden de parada pendiente, para no dejar mensajes sin recibir, y envían sus estadísticas al proceso 0.

Si una consulta llega cuando la clave ya la ha encontrado otro buscador, el proceso 0 responde con un código de parada por el mismo canal de la respuesta. De esa forma toda consulta tiene respuesta y ningún proceso se queda bloqueado.

### Estadísticas

Cada buscador cuenta los intentos totales, las veces que ha aparecido la pista, las llamadas a `MPI_Send`, `MPI_Recv`, `MPI_Isend`, `MPI_Irecv`, `MPI_Probe`, `MPI_Iprobe` y `MPI_Bcast`, y su tiempo de ejecución. Las envía al proceso 0 con un tipo de datos derivado de MPI (`MPI_Type_create_struct`), y el proceso 0 imprime los datos de cada buscador y los totales.

## Compilación y ejecución

Hace falta una implementación de MPI, como Open MPI o MPICH.

```bash
make
mpirun -np <procesos> ./practica_mpi <PISTA>
```

El número de procesos incluye el proceso 0: con `-np 5` hay 4 buscadores. Por ejemplo:

```bash
mpirun -np 5 ./practica_mpi ABADIA
```

## Pruebas de rendimiento

`pruebas.sh` compila el programa y lo ejecuta con las tres pistas, duplicando el número de buscadores en cada paso (1, 2, 4, 8...) hasta llegar a 64. A partir del número de CPU físicas de la máquina usa `--oversubscribe`.

```bash
./pruebas.sh <num_cpus_fisicas>
```

El número de CPU se puede consultar con `lscpu` o `nproc --all`. La salida se guarda en `Resultados/<nombre_maquina>/<Pista>/<N>ProcBusc.txt`, junto con la información de la máquina que da `lscpu`.

## Resultados

La carpeta `Resultados/` tiene las ejecuciones en las máquinas de los cuatro miembros del grupo: `MacBook-Pro-de-Hugo-2.local`, `Pruebas-Ordenador-Alfonso`, `VM_RUBEN` y `resultadosDavidPC`. Las hojas de cálculo `medicionesMPI .xlsx` y `medicionesMPI _Ruben.xlsx` recogen las mediciones.
