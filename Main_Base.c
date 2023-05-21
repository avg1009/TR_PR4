/*
 * FreeRTOS V202112.00
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */


#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <math.h>
/* Kernel includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "semphr.h"

/* Local includes. */
#include "console.h"

#include "variable_global.h"
#include <time.h>


/* Priorities at which the tasks are created. */
#define T1_PRIORITY    ( tskIDLE_PRIORITY + 1 )
#define T2_PRIORITY    ( tskIDLE_PRIORITY + 1 )
#define T3_PRIORITY    ( tskIDLE_PRIORITY + 1 )
#define T4_PRIORITY    ( tskIDLE_PRIORITY + 1 )

/* The rate at which data is sent to the queue.  The times are converted from
 * milliseconds to ticks using the pdMS_TO_TICKS() macro. */
#define FREQUENCY_MS_TASK1         pdMS_TO_TICKS( 3000UL )
#define FREQUENCY_MS_TASK2         pdMS_TO_TICKS( 100UL )
#define SLEEP_TASK1                pdMS_TO_TICKS( 10000UL )
#define SLEEP_TASK2                pdMS_TO_TICKS( 1000UL )

/*-----------------------------------------------------------*/
#define N_T3 5 // Instancias de la T3
#define LEN_ARCHIVO 6 // Tamaño del nombre del archivo


/*-------------------------- funciones ---------------------------*/
float calcularPromedio(const char* nameFile);
float masRepetido(float arr[], int n, int * maxrep);

/*-------------------------- tareas ------------------------------*/

// 20 numeros enteros 0-100 en un fichero con nombre aleatorio T=5s
static void tarea1( void * pvParameters );
// Activa 5 tareas T3.x comunica el nom de archivo 
static void tarea2( void * pvParameters );
// Se generan 5 de tipo 3 reciben el nom de fichero y calculan la media
// Falla el 90%
static void tarea3( void * pvParameters );
// Solo consume 1 sec
static void tarea4( void * pvParameters );


/*-------------------------- Colas ------------------------------*/

QueueHandle_t colaT32; // Cola para que se comuniquen T3 y T2 las medias
QueueHandle_t colaT12; // Cola para que se comuniquen T1 y T2 el nombre del archivo

//SemaphoreHandle_t mutex;

/*--------------------- Semaforos -------------------------------*/

SemaphoreHandle_t semT12; // T1 y T2
SemaphoreHandle_t semT24; // T2 y T4

/*--------------------- Otros ------------------------------------*/

int global1 = 0;
int global2 = 0;

/*** SEE THE COMMENTS AT THE TOP OF THIS FILE ***/
void main_base( void )
{
    // Cola
    colaT32 = xQueueCreate(N_T3, sizeof(float));
    colaT12 = xQueueCreate(N_T3, sizeof(char)*(LEN_ARCHIVO+1));

    // Semaforos (Cambiar por mutex?)
    semT12 = xSemaphoreCreateBinary();
    semT24 = xSemaphoreCreateBinary();
    
    // T1, T2, T4 (T3 se lanza distinto)
    xTaskCreate(tarea1, "T1", configMINIMAL_STACK_SIZE, NULL, T1_PRIORITY, NULL);
    xTaskCreate(tarea2, "T2", configMINIMAL_STACK_SIZE, NULL, T2_PRIORITY, NULL);
    xTaskCreate(tarea4, "T4", configMINIMAL_STACK_SIZE, NULL, T4_PRIORITY, NULL);

    // Lanzamiento del scheduler
    vTaskStartScheduler();

    for( ; ; ){}
}
/*-----------------------------------------------------------*/

/*
 * TAREA 1 - Productor:
 * - Nueros aleatorios del 0 al 100
 * - Los mete en un fichero
 * - El nombre del fichero es aleatorio
 * - El nombre tiene un tamaño maximo LEN_ARCHIVO
 * -------
 * T = 5s
 * T1 --sem--> T2
 * T1:nombre --cola--> T2/
 */
static void tarea1(void * pvParameters){
    ( void ) pvParameters;
    for( ; ; ){
        console_print("[T1] Tarea1\n");
        // Generar un nombre de fichero aleatorio +1 null terminator
        char filename[LEN_ARCHIVO + 1];

        for (int i = 0; i < LEN_ARCHIVO; i++) {
            filename[i] = 'a' + rand() % 26;
        }
        
        filename[LEN_ARCHIVO] = '\0';

        FILE* file = fopen(filename, "w");

        // Aleatorios
        for (int i = 0; i < 20; i++) {
            int num = rand() % 101; // Entre 0 y 100
            fprintf(file, "%d\n", num);
        }
        fclose(file);

        console_print("[T1] Archivo %s \n",filename);

        // Notificar a T2 el nombre del fichero generado
        xQueueSend(colaT12, &filename, portMAX_DELAY);

        // Abrimos paso a T2
        xSemaphoreGive(semT12);

        // TODO pasar a constante
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/*
 * TAREA 2 - Consumidor:
 * - Espera a T1
 * - Activa las tareas T3
 * - Pasa el nombre a T3.x
 * - Espera a que acaben todas
 * - Mayoria absoluta segun respuestas
 * - Borra el fichero
 * - Imprime resultados
 * 
 */
static void tarea2(void * pvParameters){
    ( void ) pvParameters;
    for( ; ; ){
        char filename[LEN_ARCHIVO + 1]; // Nombre del archivo
        float medias[N_T3];             // medias obtenidas en los T3
        int numRespuestas = 0;          // respuestas obtenidas
        float sumaResultados = 0;       // dep
        float resultadoT3;              //intermedia

        // Esperar la notificación de T1
        xSemaphoreTake(semT12, portMAX_DELAY);

        console_print("[T2] Tarea2\n");

        // Notificar a T2 el nombre del fichero generado
        xQueueReceive(colaT12, &filename, portMAX_DELAY);

        // Activar las tareas T3.1-T3.5 y pasarles el nombre del fichero
        for (int i = 1; i <= N_T3; i++) {
            char nombreTarea[10];
            sprintf(nombreTarea, "T3.%d", i);
            xTaskCreate(tarea3, nombreTarea, configMINIMAL_STACK_SIZE, (char *) filename, T3_PRIORITY, NULL);
            
        }

        // Esperar las respuestas de las tareas T3.1-T3.5
        for (int i = 0; i < N_T3; i++) {
            xQueueReceive(colaT32, &medias[i], portMAX_DELAY);
            // Calculo
            console_print("[T2] Obtiene %.3f\n",medias[i]);
        }

        // TODO: Conteo de votos
        int maxrep;
        resultadoT3 = masRepetido(medias, N_T3, &maxrep);

        // Si tiene mas que el 50% + 1 de los votos
        if (maxrep > ceil(N_T3/2)){
            console_print("[T2] %d procesos han votado por %.3f\n",maxrep,resultadoT3);
        } else {
            console_print("[T2] No ha habido consenso\n");
        }

        
        // Borrar archivo
        if (remove(filename) == 0) {
            console_print("Fichero \"%s\" borrado correctamente.\n", filename);
        } else {
            console_print("Error al borrar el fichero \"%s\".\n", filename);
        }
    }
}

/*
 * TAREA 3:
 * - Calcula valor medio del fichero (solo lectura)
 * - 90% de prob de mandar el correcto
 * - Introduce los datos en la cosa
 * - Se autodestruye
 */
static void tarea3(void * pvParameters){

    // Variables para el nombre del fichero y el resultado
    char *filename = (char *) pvParameters;
    float resultado;

    // Obtener el nombre de la tarea
    TaskStatus_t xTaskDetails;
    vTaskGetInfo( NULL,
                  &xTaskDetails,
                  pdTRUE,
                  eInvalid );

    console_print("[%s] %s lanzado \n",xTaskDetails.pcTaskName, xTaskDetails.pcTaskName);
    
    if (rand() % 100 < 10) { // < 10 para poder tocar la probabilidad
        // Resultado válido (valor promedio)
        resultado = calcularPromedio(filename); 
    } else {
        // Resultado aleatorio
        resultado = (float) rand() / RAND_MAX;
    }

    console_print("[%s] obtiene %.3f \n",xTaskDetails.pcTaskName, resultado);

    // Enviar el resultado a T2 mediante la cola
    xQueueSend(colaT32, &resultado, portMAX_DELAY);

    // Responsabilidad de destruccion
    vTaskDelete(NULL);
    
}

/*
 * TAREA 4:
 * - Solo espera
 */
static void tarea4(void * pvParameters) {
    ( void ) pvParameters;
    for( ; ; ) {

        xSemaphoreTake(semT24, portMAX_DELAY); // Espera a T2

        vTaskDelay(pdMS_TO_TICKS(1000)); // Consume 1 sec

        vTaskDelay(pdMS_TO_TICKS(2000)); // Espera 2 sec
    }
}

// Función para calcular el promedio de los números en el fichero
float calcularPromedio(const char* nameFile) {
    FILE* fichero = fopen(nameFile, "r");
    if (fichero == NULL) {
        printf("Error al abrir el fichero \"%s\".\n", nameFile);
        return 0.0;
    }

    int totalNumeros = 0;
    int suma = 0;
    int numero;

    while (fscanf(fichero, "%d", &numero) != EOF) {
        suma += numero;
        totalNumeros++;
    }

    fclose(fichero);

    float promedio = (float)suma / totalNumeros;
    return promedio;

}

// Función para encontrar el valor más repetido en un arreglo de float
float masRepetido(float arr[], int n, int * maxrep) {
    float valorMasRepetido = arr[0];
    int maxRepeticiones, repeticiones = 0;

    for (int i = 0; i < n; i++) {
        repeticiones=1;

        for (int j = i + 1; j < n; j++) {
            if (arr[i] == arr[j])
                repeticiones++;
        }

        if (repeticiones > maxRepeticiones) {
            maxRepeticiones = repeticiones;
            valorMasRepetido = arr[i];
        }
    }

    *maxrep = maxRepeticiones;

    return valorMasRepetido;
}