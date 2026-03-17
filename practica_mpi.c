/*
    ARQUITECTURA DE COMPUTADORES - GRADO INGENIERÍA INFORMÁTICA 2025-2026
    PRÁCTICA 1: MPI - Descifra mensaje "fuerza bruta" con pistas
    Autores:
        Alfonso Crego Calvo
        Hugo Chalard Collado
        David Lavado González 
        Rubén Hernández Molina
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mpi.h"
#include <sys/time.h>

/* =========================================================
 *  CONFIGURACION DEL PROBLEMA
 * ========================================================= */

#define MENSAJE_ORIGINAL \
    "HABIENDO LLEGADO AL FINAL DE MI VIDA DE POBRE PECADOR, CON EL PELO YA CANOSO, "\
    "ME DISPONGO A DEJAR CONSTANCIA SOBRE ESTE PERGAMINO DE LOS HECHOS ASOMBROSOS Y "\
    "TERRIBLES QUE ME FUE DADO PRESENCIAR EN MI JUVENTUD, HACIA FINALES DEL ANHO DEL "\
    "SENHOR DE 1327. QUE DIOS ME CONCEDA SABIDURIA Y GRACIA PARA SER FIEL NARRADOR DE "\
    "LOS SUCESOS QUE TUVIERON LUGAR EN UNA REMOTA ABADIA EN EL RECONDITO NORTE DE "\
    "ITALIA. UNA ABADIA CUYO NOMBRE PARECE AHORA MAS PIADOSO Y PRUDENTE OMITIR."

#define CLAVE_CIFRADO    "GTYHUY"

#define LEN_CLAVE        6

#define MAX_MSG          2048
#define MAX_PISTA        64
#define MAX_NOMBRE       MPI_MAX_PROCESSOR_NAME

/* Intervalo de comprobacion de TAG_PARAR (solo rama sin pista) en segundos */
#define INTERVALO_CHECK  0.05

/* =========================================================
 *  ETIQUETAS MPI
 * ========================================================= */
#define TAG_CONSULTA_CLAVE  20  
#define TAG_RESPUESTA_CLAVE 21
#define TAG_PARAR           22
#define TAG_ESTADISTICAS    30

/* =========================================================
 *  RESPUESTAS DEL PROCESO 0
 *  RESP_PARAR se usa cuando la clave ya fue encontrada por
 *  otro buscador y llega una consulta nueva: en vez de enviar
 *  TAG_PARAR + RESP_CLAVE_MAL (que dejaría un mensaje sin leer),
 *  se responde con RESP_PARAR por TAG_RESPUESTA_CLAVE para que
 *  el buscador lo consuma en su inner-loop y termine limpiamente.
 * ========================================================= */
#define RESP_CLAVE_OK   1
#define RESP_CLAVE_MAL  0
#define RESP_PARAR      2

/* =========================================================
 *  ESTRUCTURA DE ESTADISTICAS
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
 *  (exactamente las proporcionadas por el enunciado)
 * ========================================================= */

double mygettime(void)
{
    struct timeval tv;
    if (gettimeofday(&tv, 0) < 0) {
        perror("oops");
    }
    return (double)tv.tv_sec + (0.000001 * (double)tv.tv_usec);
}

static unsigned int next = 1;
#define RAND_MAX 2147483647

int myrand(void)
{
    next = next * 1103515245 + 12345;
    return((unsigned)(next % RAND_MAX));
}

void mysrand(unsigned int seed)
{
    next = seed;
}

void xor(char *entrada, char *salida, size_t tam_mensaje,
         char *clave, size_t len_clave)
{
    for (size_t i = 0; i < tam_mensaje; i++) {
        salida[i] = entrada[i] ^ clave[i % len_clave];
    }
}

/* =========================================================
 *  FUNCION PROPIA: genera clave aleatoria 'A'-'Z'
 * ========================================================= */
void clave_aleatoria(char *clave, int len)
{
    for (int i = 0; i < len; i++) {
        clave[i] = 'A' + (myrand() % 26);
    }
}

/* =========================================================
 *  TIPO DERIVADO MPI PARA ESTADISTICAS
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
void proceso_ES(int num_procs, const char *pista)
{
    int  num_buscadores = num_procs - 1;
    char nombre_proc[MAX_NOMBRE];
    int  lon_nombre;
    MPI_Get_processor_name(nombre_proc, &lon_nombre);

    const char *mensaje_orig = MENSAJE_ORIGINAL;
    const char *clave_real   = CLAVE_CIFRADO;

    int tam_msg   = (int)strlen(mensaje_orig);
    int tam_pista = (int)strlen(pista);
    int tam_clave = LEN_CLAVE;

    char mensaje_cifrado[MAX_MSG];
    xor((char*)mensaje_orig, mensaje_cifrado, tam_msg,
        (char*)clave_real, tam_clave);

    long long n_send = 0, n_recv = 0, n_bcast = 0, n_probe = 0;

    printf("\n=== PROCESO 0 (E/S) en %s ===\n", nombre_proc);
    printf("Mensaje original  : %s\n", mensaje_orig);
    printf("Clave de cifrado  : %s\n", clave_real);
    printf("Pista de busqueda : %s\n", pista);
    printf("Tamano mensaje    : %d bytes\n", tam_msg);
    printf("Buscadores activos: %d\n\n", num_buscadores);
    fflush(stdout);

    double t_ini = mygettime();

    /* Broadcast de datos iniciales */
    int datos_ini[3] = {tam_msg, tam_pista, tam_clave};
    MPI_Bcast(datos_ini,       3,         MPI_INT,  0, MPI_COMM_WORLD); n_bcast++;
    MPI_Bcast(mensaje_cifrado, tam_msg,   MPI_CHAR, 0, MPI_COMM_WORLD); n_bcast++;
    MPI_Bcast((char*)pista,    tam_pista, MPI_CHAR, 0, MPI_COMM_WORLD); n_bcast++;

    /* Tipo derivado para estadisticas */
    MPI_Datatype tipo_stats;
    crear_tipo_estadisticas(&tipo_stats);

    Estadisticas stats_buscadores[num_procs];
    memset(stats_buscadores, 0, sizeof(stats_buscadores));

    int  buscadores_pendientes = num_buscadores;
    int  clave_encontrada      = 0;
    char clave_recibida[MAX_PISTA + 1];
    MPI_Status status;

    /*
     * Bucle principal del proceso 0.
     * Espera con MPI_Probe a cualquier mensaje de cualquier buscador.
     * Puede recibir:
     *   - TAG_CONSULTA_CLAVE : un buscador cree haber encontrado la clave
     *   - TAG_ESTADISTICAS   : un buscador ha terminado y manda sus stats
     *
     * Cuando ya se encontro la clave y llega una TAG_CONSULTA_CLAVE nueva,
     * se responde con RESP_PARAR (por TAG_RESPUESTA_CLAVE) en vez de enviar
     * un TAG_PARAR separado. Asi el buscador lo consume en su inner-loop
     * y no queda ningun mensaje sin leer.
     */
    while (buscadores_pendientes > 0) {

        MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
        n_probe++;

        int origen = status.MPI_SOURCE;
        int tag    = status.MPI_TAG;

        if (tag == TAG_CONSULTA_CLAVE) {

            MPI_Recv(clave_recibida, tam_clave, MPI_CHAR,
                     origen, TAG_CONSULTA_CLAVE, MPI_COMM_WORLD, &status);
            n_recv++;
            clave_recibida[tam_clave] = '\0';

            int respuesta;

            if (!clave_encontrada &&
                strncmp(clave_recibida, clave_real, tam_clave) == 0) {

                /* --- CLAVE CORRECTA --- */
                clave_encontrada = 1;
                respuesta = RESP_CLAVE_OK;
                MPI_Send(&respuesta, 1, MPI_INT, origen,
                         TAG_RESPUESTA_CLAVE, MPI_COMM_WORLD);
                n_send++;

                printf(">>> Clave encontrada por proceso %d: %s <<<\n",
                       origen, clave_recibida);
                fflush(stdout);

                /*
                 * Avisar con TAG_PARAR solo a los buscadores que NO tienen
                 * ninguna consulta pendiente en este momento (los que estan
                 * en la rama "sin pista" del bucle principal).
                 * Los que tengan una consulta pendiente recibiran RESP_PARAR
                 * cuando la envien y proceso 0 la atienda.
                 */
                int parar = 1;
                for (int p = 1; p < num_procs; p++) {
                    if (p != origen) {
                        MPI_Send(&parar, 1, MPI_INT, p,
                                 TAG_PARAR, MPI_COMM_WORLD);
                        n_send++;
                    }
                }

            } else if (clave_encontrada) {

                /*
                 * La clave ya fue encontrada antes por otro buscador,
                 * pero este ha enviado una consulta que hay que responder
                 * obligatoriamente para no dejar mensajes sin leer.
                 * Se responde RESP_PARAR por TAG_RESPUESTA_CLAVE para que
                 * el buscador lo consuma en su inner-loop y termine limpio.
                 * Como ya se envio TAG_PARAR a este proceso antes, tambien
                 * podria haber llegado ese TAG_PARAR; pero no importa:
                 * el buscador comprueba primero TAG_RESPUESTA_CLAVE en su
                 * inner-loop, asi que lo consumira correctamente.
                 * El TAG_PARAR ya enviado puede quedar pendiente, pero como
                 * el buscador siempre hace MPI_Iprobe de TAG_PARAR despues
                 * de salir del inner-loop lo consumira antes de terminar
                 * (ver rama "flush" al final de proceso_buscador).
                 */
                respuesta = RESP_PARAR;
                MPI_Send(&respuesta, 1, MPI_INT, origen,
                         TAG_RESPUESTA_CLAVE, MPI_COMM_WORLD);
                n_send++;

            } else {

                /* --- CLAVE INCORRECTA --- */
                respuesta = RESP_CLAVE_MAL;
                MPI_Send(&respuesta, 1, MPI_INT, origen,
                         TAG_RESPUESTA_CLAVE, MPI_COMM_WORLD);
                n_send++;
            }

        } else if (tag == TAG_ESTADISTICAS) {

            MPI_Recv(&stats_buscadores[origen], 1, tipo_stats,
                     origen, TAG_ESTADISTICAS, MPI_COMM_WORLD, &status);
            n_recv++;
            buscadores_pendientes--;
        }
    }

    double t_fin = mygettime();

    printf("\n========== ESTADISTICAS GLOBALES ==========\n");
    printf("Tiempo total (proceso 0): %.6f s\n\n", t_fin - t_ini);

    long long tot_intentos = 0, tot_pista = 0;
    long long tot_send_b = 0, tot_recv_b = 0;

    for (int p = 1; p < num_procs; p++) {
        Estadisticas *e = &stats_buscadores[p];
        printf("--- Buscador %d ---\n", p);
        printf("  Intentos totales  : %lld\n",  e->intentos_totales);
        printf("  Aciertos de pista : %lld\n",  e->aciertos_pista);
        printf("  MPI_Send          : %lld\n",  e->n_send);
        printf("  MPI_Recv          : %lld\n",  e->n_recv);
        printf("  MPI_Isend         : %lld\n",  e->n_isend);
        printf("  MPI_Irecv         : %lld\n",  e->n_irecv);
        printf("  MPI_Probe         : %lld\n",  e->n_probe);
        printf("  MPI_Iprobe        : %lld\n",  e->n_iprobe);
        printf("  MPI_Bcast (recib.): %lld\n",  e->n_bcast);
        printf("  Tiempo buscador   : %.6f s\n", e->tiempo);

        tot_intentos += e->intentos_totales;
        tot_pista    += e->aciertos_pista;
        tot_send_b   += e->n_send;
        tot_recv_b   += e->n_recv;
    }

    printf("\n--- TOTALES ---\n");
    printf("  Intentos totales (todos buscadores) : %lld\n", tot_intentos);
    printf("  Aciertos de pista (todos)           : %lld\n", tot_pista);
    printf("  MPI_Send buscadores                 : %lld\n", tot_send_b);
    printf("  MPI_Recv buscadores                 : %lld\n", tot_recv_b);
    printf("  MPI_Send proceso 0                  : %lld\n", n_send);
    printf("  MPI_Recv proceso 0                  : %lld\n", n_recv);
    printf("  MPI_Probe proceso 0                 : %lld\n", n_probe);
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
    Estadisticas est;
    memset(&est, 0, sizeof(est));

    double t_ini = mygettime();

    /* Recibir datos iniciales por Bcast */
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

    /* Semilla distinta por proceso para que busquen zonas distintas */
    mysrand((unsigned int)(id * 31337 + 12345));

    char clave[MAX_PISTA];
    clave[tam_clave] = '\0';

    int        terminado    = 0;
    int        clave_ok     = 0;
    MPI_Status status;
    double     ultimo_check = mygettime();

    while (!terminado) {

        clave_aleatoria(clave, tam_clave);
        est.intentos_totales++;

        xor(mensaje_cifrado, mensaje_descifrado, tam_msg, clave, tam_clave);
        mensaje_descifrado[tam_msg] = '\0';

        if (strstr(mensaje_descifrado, pista) != NULL) {

            /* La pista aparece: consultar al proceso 0 */
            est.aciertos_pista++;

            MPI_Send(clave, tam_clave, MPI_CHAR, 0,
                     TAG_CONSULTA_CLAVE, MPI_COMM_WORLD);
            est.n_send++;

            /*
             * Inner-loop: esperar UNICAMENTE TAG_RESPUESTA_CLAVE.
             * Proceso 0 siempre responde a cada consulta, bien con
             * RESP_CLAVE_OK, RESP_CLAVE_MAL o RESP_PARAR.
             * Esto garantiza que no quede ninguna consulta sin respuesta.
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
                    } else if (respuesta == RESP_PARAR) {
                        terminado = 1;
                    }
                    /* RESP_CLAVE_MAL: sigue buscando */
                }
            }

        } else {

            /*
             * La pista NO aparece: comprobar TAG_PARAR periodicamente
             * para no bloquear el bucle de busqueda indefinidamente.
             */
            double ahora = mygettime();
            if (ahora - ultimo_check >= INTERVALO_CHECK) {
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

    /*
     * Flush: al salir del bucle pueden quedar pendientes:
     *   - Un TAG_PARAR si este buscador termino por RESP_PARAR en el
     *     inner-loop (proceso 0 ya habia enviado TAG_PARAR antes de que
     *     llegara la consulta de este buscador).
     * Se consume con Iprobe para no dejar mensajes sin leer.
     */
    {
        int flag;
        MPI_Iprobe(0, TAG_PARAR, MPI_COMM_WORLD, &flag, &status);
        est.n_iprobe++;
        if (flag) {
            int dummy;
            MPI_Recv(&dummy, 1, MPI_INT, 0, TAG_PARAR,
                    MPI_COMM_WORLD, &status);
            est.n_recv++;
        }
    }

    double t_fin = mygettime();
    est.tiempo = t_fin - t_ini;

    /* Enviar estadisticas al proceso 0
     * (Solo el proceso 0 imprime segun el enunciado) */
    MPI_Datatype tipo_stats;
    crear_tipo_estadisticas(&tipo_stats);

    est.n_send++;   /* contamos este Send en las estadisticas */
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

    if (argc != 2) {
        if (id == 0) {
            fprintf(stderr, "Uso: mpirun -np <num_procs> %s "
                "<pista_busqueda>\n", argv[0]);
        }
        MPI_Finalize();
        return 1;
    } else {
        if (strcmp(argv[1], "EN") != 0
            && strcmp(argv[1], "ABADIA") != 0
            && strcmp(argv[1], "PERGAMINO") != 0) {

            if (id == 0) {
                fprintf(stderr, "ERROR: La pista de busqueda debe ser "
                    "'EN', 'ABADIA' o 'PERGAMINO'.\n");
            }
            MPI_Finalize();
            return 1;
        }
    }

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
        proceso_ES(num_procs, argv[1]);
    } else {
        proceso_buscador(id, num_procs);
    }

    MPI_Finalize();
    return 0;
}
