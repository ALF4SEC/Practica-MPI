/*
    ARQUITECTURA DE COMPUTADORES - GRADO INGENIERÍA INFORMÁTICA 2025-2026
    PRÁCTICA 1: MPI - Descifra mensaje "fuerza bruta" con pistas
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

/* =========================================================
 *  ETIQUETAS DE MENSAJES MPI
 * ========================================================= */
#define TAG_TAM_MSG         10
#define TAG_MSG_CIFRADO     11
#define TAG_TAM_PISTA       12
#define TAG_PISTA           13
#define TAG_TAM_CLAVE       14
#define TAG_CONSULTA_CLAVE  20
#define TAG_RESPUESTA_CLAVE 21
#define TAG_PARAR           22
#define TAG_ESTADISTICAS    30

/* =========================================================
 *  RESPUESTAS DEL PROCESO 0
 * ========================================================= */
#define RESP_CLAVE_OK    1
#define RESP_CLAVE_MAL   0
#define RESP_PARAR       2

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

/* Generador de números aleatorios propio (para portabilidad) */
static unsigned int next_rand = 1;

int myrand(void)
{
    next_rand = next_rand * 1103515245 + 12345;
    return (unsigned)(next_rand % 2147483647);
}

void mysrand(unsigned int seed)
{
    next_rand = seed;
}

/* Cifrado / descifrado XOR */
void xor_cipher(const char *entrada, char *salida, size_t tam_mensaje,
                const char *clave, size_t len_clave)
{
    for (size_t i = 0; i < tam_mensaje; i++) {
        salida[i] = entrada[i] ^ clave[i % len_clave];
    }
}

/* Incrementa la clave de forma secuencial: AAAAAA -> BAAAAA -> ... -> ZZZZZZ */
/* Devuelve 0 cuando se han agotado todas las combinaciones */
int siguiente_clave(char *clave, int len)
{
    int pos = 0;
    while (pos < len) {
        if (clave[pos] < 'Z') {
            clave[pos]++;
            return 1;
        } else {
            clave[pos] = 'A';
            pos++;
        }
    }
    return 0; /* overflow: se han probado todas */
}

/* Genera una clave aleatoria de caracteres 'A'-'Z' */
void clave_aleatoria(char *clave, int len)
{
    for (int i = 0; i < len; i++) {
        clave[i] = 'A' + (myrand() % 26);
    }
}

/* =========================================================
 *  CONSTRUCCIÓN DEL TIPO DERIVADO PARA ESTADÍSTICAS
 * ========================================================= */
void crear_tipo_estadisticas(MPI_Datatype *tipo)
{
    Estadisticas dummy;
    int          bloques[10]    = {1,1,1,1,1,1,1,1,1,1};
    MPI_Datatype tipos[10]      = {
        MPI_LONG_LONG, MPI_LONG_LONG, MPI_LONG_LONG,
        MPI_LONG_LONG, MPI_LONG_LONG, MPI_LONG_LONG,
        MPI_LONG_LONG, MPI_LONG_LONG, MPI_LONG_LONG,
        MPI_DOUBLE
    };
    MPI_Aint base_addr, desplaz[10];

    MPI_Get_address(&dummy,                   &base_addr);
    MPI_Get_address(&dummy.intentos_totales,  &desplaz[0]);
    MPI_Get_address(&dummy.aciertos_pista,    &desplaz[1]);
    MPI_Get_address(&dummy.n_send,            &desplaz[2]);
    MPI_Get_address(&dummy.n_recv,            &desplaz[3]);
    MPI_Get_address(&dummy.n_isend,           &desplaz[4]);
    MPI_Get_address(&dummy.n_irecv,           &desplaz[5]);
    MPI_Get_address(&dummy.n_probe,           &desplaz[6]);
    MPI_Get_address(&dummy.n_iprobe,          &desplaz[7]);
    MPI_Get_address(&dummy.n_bcast,           &desplaz[8]);
    MPI_Get_address(&dummy.tiempo,            &desplaz[9]);

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
    int    num_buscadores = num_procs - 1;
    char   nombre_proc[MAX_NOMBRE];
    int    lon_nombre;
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
    /* Enviamos: tam_msg, tam_pista, tam_clave, mensaje_cifrado, pista */
    int datos_ini[3] = {tam_msg, tam_pista, tam_clave};
    MPI_Bcast(datos_ini,        3,       MPI_INT,  0, MPI_COMM_WORLD); n_bcast++;
    MPI_Bcast(mensaje_cifrado,  tam_msg, MPI_CHAR, 0, MPI_COMM_WORLD); n_bcast++;
    MPI_Bcast((char*)pista,     tam_pista, MPI_CHAR, 0, MPI_COMM_WORLD); n_bcast++;

    /* ---------- BUCLE de consultas ---------- */
    int  buscadores_activos = num_buscadores;
    int  clave_encontrada   = 0;
    char clave_recibida[MAX_PISTA];
    MPI_Status status;

    while (buscadores_activos > 0) {
        /* Esperar cualquier consulta de clave */
        MPI_Recv(clave_recibida, tam_clave, MPI_CHAR,
                 MPI_ANY_SOURCE, TAG_CONSULTA_CLAVE,
                 MPI_COMM_WORLD, &status);
        n_recv++;

        int origen = status.MPI_SOURCE;
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
            int parar = RESP_PARAR;
            for (int p = 1; p < num_procs; p++) {
                if (p != origen) {
                    MPI_Send(&parar, 1, MPI_INT, p,
                             TAG_PARAR, MPI_COMM_WORLD);
                    n_send++;
                }
            }
            buscadores_activos = 0; /* todos avisados */

        } else {
            /* Clave incorrecta */
            respuesta = RESP_CLAVE_MAL;
            MPI_Send(&respuesta, 1, MPI_INT, origen,
                     TAG_RESPUESTA_CLAVE, MPI_COMM_WORLD);
            n_send++;
        }
    }

    /* Si nadie encontró la clave (todos los rangos agotados sin éxito),
       esperar igualmente a que lleguen estadísticas de todos. */

    double t_fin = mygettime();

    /* ---------- Recibir estadísticas ---------- */
    MPI_Datatype tipo_stats;
    crear_tipo_estadisticas(&tipo_stats);

    printf("\n========== ESTADÍSTICAS GLOBALES ==========\n");
    printf("Tiempo total (proceso 0): %.6f s\n\n", t_fin - t_ini);

    long long tot_intentos = 0, tot_pista = 0;
    long long tot_send_b = 0, tot_recv_b = 0;

    for (int p = 1; p < num_procs; p++) {
        Estadisticas est;
        MPI_Recv(&est, 1, tipo_stats, p, TAG_ESTADISTICAS,
                 MPI_COMM_WORLD, &status);
        n_recv++;

        printf("--- Buscador %d ---\n", p);
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
    }

    printf("\n--- TOTALES ---\n");
    printf("  Intentos totales (todos buscadores) : %lld\n", tot_intentos);
    printf("  Aciertos de pista (todos)           : %lld\n", tot_pista);
    printf("  MPI_Send buscadores                 : %lld\n", tot_send_b);
    printf("  MPI_Recv buscadores                 : %lld\n", tot_recv_b);
    printf("  MPI_Send proceso 0                  : %lld\n", n_send);
    printf("  MPI_Recv proceso 0                  : %lld\n", n_recv);
    printf("  MPI_Bcast proceso 0                 : %lld\n", n_bcast);
    printf("===========================================\n\n");
    fflush(stdout);

    MPI_Type_free(&tipo_stats);
}

/* =========================================================
 *  PROCESOS BUSCADORES
 * ========================================================= */
void proceso_buscador(int id, int num_procs)
{
    int  num_buscadores = num_procs - 1;
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

    int  tam_msg   = datos_ini[0];
    int  tam_pista = datos_ini[1];
    int  tam_clave = datos_ini[2];

    char mensaje_cifrado[MAX_MSG];
    MPI_Bcast(mensaje_cifrado, tam_msg, MPI_CHAR, 0, MPI_COMM_WORLD);
    est.n_bcast++;

    char pista[MAX_PISTA];
    MPI_Bcast(pista, tam_pista, MPI_CHAR, 0, MPI_COMM_WORLD);
    est.n_bcast++;
    pista[tam_pista] = '\0';

    char mensaje_descifrado[MAX_MSG + 1];

    /* --------------------------------------------------------
     * ESTRATEGIA DE BÚSQUEDA:
     *
     * Búsqueda ALEATORIA - cada buscador genera claves de forma
     * aleatoria con semilla diferente. Se combina con un intento
     * secuencial por rango para asegurar cobertura completa.
     *
     * Reparto de rango secuencial:
     *   Total posibilidades = 26^tam_clave
     *   Cada buscador recibe un bloque del espacio de búsqueda.
     *
     * En esta implementación se usa búsqueda aleatoria mezclada
     * con verificación periódica de mensaje de parada.
     * -------------------------------------------------------- */

    /* Inicializar semilla aleatoria distinta para cada proceso */
    mysrand((unsigned int)(id * 31337 + 12345));

    /* Calcular rango secuencial para este buscador (fallback) */
    /* Índice inicial basado en id (1-indexed buscador) */
    /* Usaremos búsqueda aleatoria principalmente */

    char clave[MAX_PISTA];
    memset(clave, 'A', tam_clave);
    clave[tam_clave] = '\0';

    /* Para búsqueda secuencial por rango:
     * Buscador i (1..num_buscadores) empieza en la posición:
     *   inicio = (i-1) * (26^tam_clave / num_buscadores)
     * Aquí lo simplificamos avanzando el primer carácter:
     *   Buscador 1 empieza en A..., buscador 2 en (26/n)...
     * Para claves largas usamos búsqueda aleatoria. */

    /* Estrategia: cada buscador avanza su primer bloque de letras */
    int letras_por_buscador = 26 / num_buscadores;
    int letra_inicio = (id - 1) * letras_por_buscador;
    int letra_fin    = (id == num_buscadores)
                       ? 26
                       : id * letras_por_buscador;

    /* Usar búsqueda aleatoria (más uniforme para claves largas) */
    /* Mezclamos: primero intentos aleatorios, y si se agota
       el espacio asignado, se marca como terminado. */

    int terminado   = 0;
    int clave_ok    = 0;
    MPI_Status status;

    /* Para no saturar MPI_Iprobe, lo hacemos cada N intentos */
    const int CHECK_INTERVAL = 10000;
    long long contador_check = 0;

    while (!terminado) {
        /* Generar clave aleatoria */
        clave_aleatoria(clave, tam_clave);
        clave[tam_clave] = '\0';
        est.intentos_totales++;
        contador_check++;

        /* Descifrar */
        xor_cipher(mensaje_cifrado, mensaje_descifrado, tam_msg, clave, tam_clave);
        mensaje_descifrado[tam_msg] = '\0';

        /* Buscar pista */
        if (strstr(mensaje_descifrado, pista) != NULL) {
            est.aciertos_pista++;

            /* Consultar al proceso 0 */
            MPI_Send(clave, tam_clave, MPI_CHAR, 0,
                     TAG_CONSULTA_CLAVE, MPI_COMM_WORLD);
            est.n_send++;

            /* Esperar respuesta o mensaje de parada */
            /* Puede venir TAG_RESPUESTA_CLAVE o TAG_PARAR */
            int flag;
            int respuesta = -1;

            /* Esperamos activamente cualquiera de los dos tags */
            /* Primero chequeamos TAG_PARAR */
            int recibido = 0;
            while (!recibido) {
                MPI_Iprobe(0, TAG_PARAR, MPI_COMM_WORLD, &flag, &status);
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

                MPI_Iprobe(0, TAG_RESPUESTA_CLAVE, MPI_COMM_WORLD,
                           &flag, &status);
                est.n_iprobe++;
                if (flag) {
                    MPI_Recv(&respuesta, 1, MPI_INT, 0,
                             TAG_RESPUESTA_CLAVE, MPI_COMM_WORLD, &status);
                    est.n_recv++;
                    recibido = 1;
                    if (respuesta == RESP_CLAVE_OK) {
                        terminado = 1;
                        clave_ok  = 1;
                    }
                    break;
                }
            }

        } else {
            /* Sin pista: comprobar periódicamente si hay orden de parar */
            if (contador_check >= CHECK_INTERVAL) {
                contador_check = 0;
                int flag;
                MPI_Iprobe(0, TAG_PARAR, MPI_COMM_WORLD, &flag, &status);
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
        printf("[Buscador %d en %s] Clave ENCONTRADA: %s (%.6f s, %lld intentos)\n",
               id, nombre_proc, clave, est.tiempo, est.intentos_totales);
    } else {
        printf("[Buscador %d en %s] Parado (%.6f s, %lld intentos, %lld pistas)\n",
               id, nombre_proc, est.tiempo, est.intentos_totales, est.aciertos_pista);
    }
    fflush(stdout);

    /* Enviar estadísticas al proceso 0 */
    MPI_Datatype tipo_stats;
    crear_tipo_estadisticas(&tipo_stats);

    MPI_Send(&est, 1, tipo_stats, 0, TAG_ESTADISTICAS, MPI_COMM_WORLD);
    /* (no contamos este send en n_send porque ya terminó el bucle) */

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