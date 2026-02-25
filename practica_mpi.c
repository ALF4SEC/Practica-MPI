/*
    ARQUITECTURA DE COMPUTADORES - GRADO INGENIERÍA INFORMÁTICA 2025-2026
    PRÁCTICA 1: MPI - Descifra mensaje "fuerza bruta" con pistas
    Autores:
        Alfonso Crego Calvo
        Hugo Chalard Collado
        David Lavado González 
        Rubén Hernández Molina
*/

/*
 * ARQUITECTURA DE COMPUTADORES - GRADO INGENIERÍA INFORMÁTICA 2025-2026
 * PRÁCTICA 1: MPI - Descifra mensaje "fuerza bruta" con pistas
 *
 * Compilación:
 *   mpicc practica_mpi.c -o practica_mpi
 *
 * Ejecución (ej: 4 procesos = 1 E/S + 3 buscadores):
 *   mpirun -np 4 practica_mpi
 *   mpirun --oversubscribe -np 16 practica_mpi
 *
 * Se necesitan mínimo 2 procesos (1 E/S + 1 buscador).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mpi.h"
#include <sys/time.h>

/* =========================================================
 *  CONFIGURACIÓN DEL PROBLEMA
 * ========================================================= */

/* Mensaje original (solo lo conoce el proceso 0) */
#define MENSAJE_ORIGINAL \
    "HABIENDO LLEGADO AL FINAL DE MI VIDA DE POBRE PECADOR, CON EL PELO YA CANOSO, "\
    "ME DISPONGO A DEJAR CONSTANCIA SOBRE ESTE PERGAMINO DE LOS HECHOS ASOMBROSOS Y "\
    "TERRIBLES QUE ME FUE DADO PRESENCIAR EN MI JUVENTUD, HACIA FINALES DEL ANHO DEL "\
    "SENHOR DE 1327. QUE DIOS ME CONCEDA SABIDURIA Y GRACIA PARA SER FIEL NARRADOR DE "\
    "LOS SUCESOS QUE TUVIERON LUGAR EN UNA REMOTA ABADIA EN EL RECONDITO NORTE DE "\
    "ITALIA. UNA ABADIA CUYO NOMBRE PARECE AHORA MAS PIADOSO Y PRUDENTE OMITIR."

/* Clave de cifrado (solo la conoce el proceso 0) */
#define CLAVE_CIFRADO   "GTYHUY"

/* Pista para los buscadores - cambiar para distintas pruebas:
 * "EN", "ABADIA", "PERGAMINO" */
#define PISTA_BUSQUEDA  "PERGAMINO"

/* Longitud de la clave en caracteres */
#define LEN_CLAVE       6

/* Tamaño máximo de buffers */
#define MAX_MSG         2048
#define MAX_PISTA       64
#define MAX_NOMBRE      MPI_MAX_PROCESSOR_NAME

/* Intervalo de comprobación de TAG_PARAR (en segundos) */
#define CHECK_INTERVAL_SEG  0.05

/* =========================================================
 *  ETIQUETAS DE MENSAJES MPI
 * ========================================================= */
#define TAG_CONSULTA_CLAVE  20
#define TAG_RESPUESTA_CLAVE 21
#define TAG_PARAR           22
#define TAG_ESTADISTICAS    30

/* =========================================================
 *  RESPUESTAS DEL PROCESO 0
 * ========================================================= */
#define RESP_CLAVE_OK    1
#define RESP_CLAVE_MAL   0

/* =========================================================
 *  ESTRUCTURA DE ESTADÍSTICAS (se envía como tipo derivado)
 * ========================================================= */
typedef struct {
    long long intentos_totales;
    long long aciertos_pista;
    long long n_send;
    long long n_recv;
    long long n_isend;
    long long n_irecv;
    long long n_probe;
    long long n_iprobe;
    long long n_bcast;
    double    tiempo;
} Estadisticas;

/* =========================================================
 *  FUNCIONES AUXILIARES
 * ========================================================= */

double mygettime(void)
{
    struct timeval tv;
    if (gettimeofday(&tv, 0) < 0) {
        perror("gettimeofday");
    }
    return (double)tv.tv_sec + (0.000001 * (double)tv.tv_usec);
}

/*
 * Generador de números aleatorios propio (para portabilidad).
 * FIX: next_rand se pasa como parámetro (puntero) en lugar de ser
 * una variable global estática, así cada proceso tiene su propio
 * estado y no hay interferencias entre ellos.
 */
int myrand(unsigned int *state)
{
    *state = (*state) * 1103515245 + 12345;
    return (int)((*state) % 2147483647);
}

void mysrand(unsigned int *state, unsigned int seed)
{
    *state = seed;
}

/* Cifrado / descifrado XOR */
void xor_cipher(const char *entrada, char *salida, size_t tam_mensaje,
                const char *clave, size_t len_clave)
{
    for (size_t i = 0; i < tam_mensaje; i++) {
        salida[i] = entrada[i] ^ clave[i % len_clave];
    }
}

/* Genera una clave aleatoria de caracteres 'A'-'Z' */
void clave_aleatoria(char *clave, int len, unsigned int *state)
{
    for (int i = 0; i < len; i++) {
        clave[i] = 'A' + (myrand(state) % 26);
    }
}

/* =========================================================
 *  CONSTRUCCIÓN DEL TIPO DERIVADO PARA ESTADÍSTICAS
 * ========================================================= */
void crear_tipo_estadisticas(MPI_Datatype *tipo)
{
    Estadisticas dummy;
    int          bloques[10] = {1,1,1,1,1,1,1,1,1,1};
    MPI_Datatype tipos[10]   = {
        MPI_LONG_LONG, MPI_LONG_LONG, MPI_LONG_LONG,
        MPI_LONG_LONG, MPI_LONG_LONG, MPI_LONG_LONG,
        MPI_LONG_LONG, MPI_LONG_LONG, MPI_LONG_LONG,
        MPI_DOUBLE
    };
    MPI_Aint base_addr, desplaz[10];

    MPI_Get_address(&dummy,                  &base_addr);
    MPI_Get_address(&dummy.intentos_totales, &desplaz[0]);
    MPI_Get_address(&dummy.aciertos_pista,   &desplaz[1]);
    MPI_Get_address(&dummy.n_send,           &desplaz[2]);
    MPI_Get_address(&dummy.n_recv,           &desplaz[3]);
    MPI_Get_address(&dummy.n_isend,          &desplaz[4]);
    MPI_Get_address(&dummy.n_irecv,          &desplaz[5]);
    MPI_Get_address(&dummy.n_probe,          &desplaz[6]);
    MPI_Get_address(&dummy.n_iprobe,         &desplaz[7]);
    MPI_Get_address(&dummy.n_bcast,          &desplaz[8]);
    MPI_Get_address(&dummy.tiempo,           &desplaz[9]);

    for (int i = 0; i < 10; i++)
        desplaz[i] -= base_addr;

    MPI_Type_create_struct(10, bloques, desplaz, tipos, tipo);
    MPI_Type_commit(tipo);
}

/* =========================================================
 *  PROCESO 0 - E/S
 * ========================================================= */
void proceso_ES(int num_procs)
{
    int  num_buscadores = num_procs - 1;
    char nombre_proc[MAX_NOMBRE];
    int  lon_nombre;
    MPI_Get_processor_name(nombre_proc, &lon_nombre);

    /* ---------- datos del problema ---------- */
    const char *mensaje_orig = MENSAJE_ORIGINAL;
    const char *clave_real   = CLAVE_CIFRADO;
    const char *pista        = PISTA_BUSQUEDA;

    int tam_msg   = (int)strlen(mensaje_orig);
    int tam_pista = (int)strlen(pista);
    int tam_clave = LEN_CLAVE;

    char mensaje_cifrado[MAX_MSG];
    xor_cipher(mensaje_orig, mensaje_cifrado, tam_msg, clave_real, tam_clave);

    /* ---------- estadísticas propias ---------- */
    long long n_send = 0, n_recv = 0, n_bcast = 0;

    printf("\n=== PROCESO 0 (E/S) en %s ===\n", nombre_proc);
    printf("Mensaje original  : %s\n", mensaje_orig);
    printf("Clave de cifrado  : %s\n", clave_real);
    printf("Pista de búsqueda : %s\n", pista);
    printf("Tamaño mensaje    : %d bytes\n", tam_msg);
    printf("Buscadores activos: %d\n\n", num_buscadores);
    fflush(stdout);

    double t_ini = mygettime();

    /* ---------- ENVÍO COLECTIVO (Bcast) de los datos comunes ---------- */
    int datos_ini[3] = {tam_msg, tam_pista, tam_clave};
    MPI_Bcast(datos_ini,       3,         MPI_INT,  0, MPI_COMM_WORLD); n_bcast++;
    MPI_Bcast(mensaje_cifrado, tam_msg,   MPI_CHAR, 0, MPI_COMM_WORLD); n_bcast++;
    MPI_Bcast((char*)pista,    tam_pista, MPI_CHAR, 0, MPI_COMM_WORLD); n_bcast++;

    /* ---------- BUCLE de consultas ----------
     *
     * FIX: El proceso 0 sigue en este bucle hasta que TODOS los
     * buscadores hayan terminado (enviado sus estadísticas).
     * Antes salía con buscadores_activos=0 nada más encontrar la
     * clave, dejando mensajes TAG_CONSULTA_CLAVE en tránsito y
     * causando deadlock: buscadores esperando respuesta que nunca
     * llegaba.
     *
     * Solución: usamos MPI_Iprobe para atender cualquier mensaje
     * pendiente (TAG_CONSULTA_CLAVE o TAG_ESTADISTICAS) en orden
     * de llegada, hasta recibir estadísticas de todos.
     * ----------------------------------------------------------- */
    int  buscadores_pendientes_stats = num_buscadores;
    int  clave_encontrada = 0;
    char clave_recibida[MAX_PISTA + 1];
    MPI_Status status;

    MPI_Datatype tipo_stats;
    crear_tipo_estadisticas(&tipo_stats);

    long long tot_intentos = 0, tot_pista = 0;
    long long tot_send_b = 0, tot_recv_b = 0;

    while (buscadores_pendientes_stats > 0) {

        /* Esperar cualquier mensaje de cualquier buscador */
        MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
        n_recv++; /* contamos el Probe como operación de recepción */

        int origen = status.MPI_SOURCE;
        int tag    = status.MPI_TAG;

        if (tag == TAG_CONSULTA_CLAVE) {
            /* --- Consulta de clave --- */
            MPI_Recv(clave_recibida, tam_clave, MPI_CHAR,
                     origen, TAG_CONSULTA_CLAVE, MPI_COMM_WORLD, &status);
            /* (el recv lo contamos junto con el probe anterior) */
            clave_recibida[tam_clave] = '\0';

            int respuesta;
            if (!clave_encontrada &&
                strncmp(clave_recibida, clave_real, tam_clave) == 0) {

                /* *** CLAVE CORRECTA *** */
                clave_encontrada = 1;
                respuesta = RESP_CLAVE_OK;
                MPI_Send(&respuesta, 1, MPI_INT, origen,
                         TAG_RESPUESTA_CLAVE, MPI_COMM_WORLD);
                n_send++;

                printf(">>> Clave encontrada por proceso %d: %s <<<\n",
                       origen, clave_recibida);
                fflush(stdout);

                /* Avisar a los demás buscadores para que paren */
                int parar = RESP_CLAVE_OK; /* cualquier valor sirve */
                for (int p = 1; p < num_procs; p++) {
                    if (p != origen) {
                        MPI_Send(&parar, 1, MPI_INT, p,
                                 TAG_PARAR, MPI_COMM_WORLD);
                        n_send++;
                    }
                }

            } else {
                /* Clave incorrecta (o ya encontrada por otro) */
                respuesta = RESP_CLAVE_MAL;
                MPI_Send(&respuesta, 1, MPI_INT, origen,
                         TAG_RESPUESTA_CLAVE, MPI_COMM_WORLD);
                n_send++;
            }

        } else if (tag == TAG_ESTADISTICAS) {
            /* --- Estadísticas finales de un buscador --- */
            Estadisticas est;
            MPI_Recv(&est, 1, tipo_stats, origen,
                     TAG_ESTADISTICAS, MPI_COMM_WORLD, &status);
            /* (el recv lo contamos junto con el probe anterior) */

            printf("--- Buscador %d ---\n", origen);
            printf("  Intentos totales  : %lld\n",  est.intentos_totales);
            printf("  Aciertos de pista : %lld\n",  est.aciertos_pista);
            printf("  MPI_Send          : %lld\n",  est.n_send);
            printf("  MPI_Recv          : %lld\n",  est.n_recv);
            printf("  MPI_Isend         : %lld\n",  est.n_isend);
            printf("  MPI_Irecv         : %lld\n",  est.n_irecv);
            printf("  MPI_Probe         : %lld\n",  est.n_probe);
            printf("  MPI_Iprobe        : %lld\n",  est.n_iprobe);
            printf("  MPI_Bcast (recib.): %lld\n",  est.n_bcast);
            printf("  Tiempo buscador   : %.6f s\n", est.tiempo);

            tot_intentos += est.intentos_totales;
            tot_pista    += est.aciertos_pista;
            tot_send_b   += est.n_send;
            tot_recv_b   += est.n_recv;

            buscadores_pendientes_stats--;
        }
        /* Cualquier otro tag inesperado se ignora (no debería ocurrir) */
    }

    double t_fin = mygettime();

    printf("\n--- TOTALES ---\n");
    printf("  Intentos totales (todos buscadores) : %lld\n", tot_intentos);
    printf("  Aciertos de pista (todos)           : %lld\n", tot_pista);
    printf("  MPI_Send buscadores                 : %lld\n", tot_send_b);
    printf("  MPI_Recv buscadores                 : %lld\n", tot_recv_b);
    printf("  MPI_Send proceso 0                  : %lld\n", n_send);
    printf("  MPI_Recv/Probe proceso 0            : %lld\n", n_recv);
    printf("  MPI_Bcast proceso 0                 : %lld\n", n_bcast);
    printf("  Tiempo total (proceso 0)            : %.6f s\n", t_fin - t_ini);
    printf("===========================================\n\n");
    fflush(stdout);

    MPI_Type_free(&tipo_stats);
}

/* =========================================================
 *  PROCESOS BUSCADORES
 * ========================================================= */
void proceso_buscador(int id, int num_procs)
{
    char nombre_proc[MAX_NOMBRE];
    int  lon_nombre;
    MPI_Get_processor_name(nombre_proc, &lon_nombre);

    /* ---------- estadísticas ---------- */
    Estadisticas est;
    memset(&est, 0, sizeof(est));

    double t_ini = mygettime();

    /* ---------- Recibir datos comunes por Bcast ---------- */
    int datos_ini[3];
    MPI_Bcast(datos_ini, 3, MPI_INT, 0, MPI_COMM_WORLD);
    est.n_bcast++;

    int tam_msg   = datos_ini[0];
    int tam_pista = datos_ini[1];
    int tam_clave = datos_ini[2];

    char mensaje_cifrado[MAX_MSG];
    MPI_Bcast(mensaje_cifrado, tam_msg, MPI_CHAR, 0, MPI_COMM_WORLD);
    est.n_bcast++;

    char pista[MAX_PISTA];
    MPI_Bcast(pista, tam_pista, MPI_CHAR, 0, MPI_COMM_WORLD);
    est.n_bcast++;
    pista[tam_pista] = '\0';

    char mensaje_descifrado[MAX_MSG + 1];

    /*
     * FIX: next_rand ahora es una variable LOCAL de cada proceso
     * (pasada por puntero a myrand/mysrand), eliminando la variable
     * global estática que compartían todos los procesos del mismo
     * ejecutable y podía causar resultados inesperados.
     *
     * Semilla distinta para cada proceso: id * primo + offset.
     */
    unsigned int rand_state;
    mysrand(&rand_state, (unsigned int)(id * 31337 + 12345));

    char clave[MAX_PISTA];
    clave[tam_clave] = '\0';

    int       terminado = 0;
    int       clave_ok  = 0;
    MPI_Status status;

    /*
     * FIX: comprobación de TAG_PARAR basada en tiempo (cada
     * CHECK_INTERVAL_SEG segundos) en lugar de cada N intentos.
     * Así el tiempo de respuesta al mensaje de parada es
     * predecible independientemente de la velocidad del hardware.
     */
    double ultimo_check = mygettime();

    while (!terminado) {
        /* Generar clave aleatoria */
        clave_aleatoria(clave, tam_clave, &rand_state);
        est.intentos_totales++;

        /* Descifrar */
        xor_cipher(mensaje_cifrado, mensaje_descifrado,
                   tam_msg, clave, tam_clave);
        mensaje_descifrado[tam_msg] = '\0';

        /* Buscar pista */
        if (strstr(mensaje_descifrado, pista) != NULL) {
            est.aciertos_pista++;

            /* Consultar al proceso 0 */
            MPI_Send(clave, tam_clave, MPI_CHAR, 0,
                     TAG_CONSULTA_CLAVE, MPI_COMM_WORLD);
            est.n_send++;

            /*
             * Esperar respuesta (TAG_RESPUESTA_CLAVE) o parada
             * (TAG_PARAR). Usamos Iprobe en bucle para no
             * bloquearnos en un solo tag.
             *
             * FIX: el proceso 0 ahora SIEMPRE responde a
             * TAG_CONSULTA_CLAVE (con RESP_CLAVE_MAL si ya se
             * encontró la clave) antes de enviar TAG_PARAR, así
             * que aquí siempre llegará TAG_RESPUESTA_CLAVE primero.
             * Aun así comprobamos TAG_PARAR por robustez.
             */
            int recibido = 0;
            while (!recibido) {
                int flag;

                MPI_Iprobe(0, TAG_RESPUESTA_CLAVE,
                           MPI_COMM_WORLD, &flag, &status);
                est.n_iprobe++;
                if (flag) {
                    int respuesta;
                    MPI_Recv(&respuesta, 1, MPI_INT, 0,
                             TAG_RESPUESTA_CLAVE,
                             MPI_COMM_WORLD, &status);
                    est.n_recv++;
                    recibido = 1;
                    if (respuesta == RESP_CLAVE_OK) {
                        terminado = 1;
                        clave_ok  = 1;
                    }
                    break;
                }

                MPI_Iprobe(0, TAG_PARAR,
                           MPI_COMM_WORLD, &flag, &status);
                est.n_iprobe++;
                if (flag) {
                    int dummy;
                    MPI_Recv(&dummy, 1, MPI_INT, 0, TAG_PARAR,
                             MPI_COMM_WORLD, &status);
                    est.n_recv++;
                    terminado = 1;
                    recibido  = 1;
                    break;
                }
            }

        } else {
            /*
             * Sin pista: comprobar cada CHECK_INTERVAL_SEG segundos
             * si llegó orden de parar.
             * FIX: basado en tiempo real, no en número de intentos.
             */
            double ahora = mygettime();
            if (ahora - ultimo_check >= CHECK_INTERVAL_SEG) {
                ultimo_check = ahora;
                int flag;
                MPI_Iprobe(0, TAG_PARAR, MPI_COMM_WORLD,
                           &flag, &status);
                est.n_iprobe++;
                if (flag) {
                    int dummy;
                    MPI_Recv(&dummy, 1, MPI_INT, 0, TAG_PARAR,
                             MPI_COMM_WORLD, &status);
                    est.n_recv++;
                    terminado = 1;
                }
            }
        }
    }

    double t_fin = mygettime();
    est.tiempo = t_fin - t_ini;

    if (clave_ok) {
        printf("[Buscador %d en %s] Clave ENCONTRADA: %s "
               "(%.6f s, %lld intentos)\n",
               id, nombre_proc, clave, est.tiempo,
               est.intentos_totales);
    } else {
        printf("[Buscador %d en %s] Parado "
               "(%.6f s, %lld intentos, %lld pistas)\n",
               id, nombre_proc, est.tiempo,
               est.intentos_totales, est.aciertos_pista);
    }
    fflush(stdout);

    /* Enviar estadísticas al proceso 0 */
    MPI_Datatype tipo_stats;
    crear_tipo_estadisticas(&tipo_stats);

    /*
     * FIX: contamos este MPI_Send en n_send porque la práctica
     * exige contabilizar TODAS las llamadas MPI realizadas.
     */
    est.n_send++;
    MPI_Send(&est, 1, tipo_stats, 0, TAG_ESTADISTICAS, MPI_COMM_WORLD);

    MPI_Type_free(&tipo_stats);
}

/* =========================================================
 *  MAIN
 * ========================================================= */
int main(int argc, char **argv)
{
    int id, num_procs;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &id);
    MPI_Comm_size(MPI_COMM_WORLD, &num_procs);

    if (num_procs < 2) {
        if (id == 0) {
            fprintf(stderr,
                "ERROR: Se necesitan al menos 2 procesos "
                "(1 E/S + 1 buscador).\n");
        }
        MPI_Finalize();
        return 1;
    }

    if (id == 0) {
        proceso_ES(num_procs);
    } else {
        proceso_buscador(id, num_procs);
    }

    MPI_Finalize();
    return 0;
}