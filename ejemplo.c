#include <string.h>
#include <math.h>
#include <stdio.h>
#include "simgrid/msg.h"
#include "simgrid/mutex.h"
#include "simgrid/cond.h"
#include "rand.h"

#define NUM_SERVERS	100

#define MFLOPS_BASE     (1000*1000*1000)  // para el cálculo del tiempo de servicio de cada tarea


#define NUM_CLIENTS	1
#define NUM_DISPATCHERS	1
#define NUM_TASKS	100000		// número de tareas a generar


#define SERVICE_RATE    1.0     // Mu = 1  service time = 1 / 1; tasa de servicio de cada servidor
double ARRIVAL_RATE;		// tasa de llegada, como argumento del programa

const long MAX_TIMEOUT_SERVER=(86400*10); //Timeout= 10 días sin actividad

// variables para gestionar la cola de tareas en cada servidor

// cola de peticiones en cada servidor, array dinamico de SimGrid
// Cada componente del vector representa la cola de tareas de cada servidor
xbt_dynar_t client_requests[NUM_SERVERS];


sg_mutex_t mutex[NUM_SERVERS];
sg_cond_t  cond[NUM_SERVERS];
int 	EmptyQueue[NUM_SERVERS];	// indicacion de fin de cola en cada servidor

// ALGORITMOS DE PLANIFICACION
const int FCFS = 0;
const int SJF = 1;
const int LJF = 2;

// ALGORITMOS DE DISTRIBUCION
const int RANDOM = 0;
const int CICLICA = 1;
const int SHORTEST_QUEUE_FIRST = 2;
const int TWO_RANDOM_CHOICES = 3;
const int TWO_RR_RANDOM_CHOICES = 4;

//DECLARACION DE FUNCIONES
double calculate_std_dev(double data[], int size, double mean);
void sort_array(double array[], int size);


// variables para estadísticas
int 	Nqueue[NUM_SERVERS];          	// elementos en la cola de cada servidor esperando a ser servidos
double 	Navgqueue[NUM_SERVERS];         // tamanio medio de la cola de cada servidor

int 	Nsystem[NUM_SERVERS];		// número de tareas en cada servidor (esperando y siendo atendidas)
double 	Navgsystem[NUM_SERVERS];	// número medio de tareas por servidor (esperando y siendo atendidas)

double tiempoMedioServicio[NUM_TASKS];    // tiempo medio de servicio para cada tarea


// estructura de la petcición que envía el cliente 
struct ClientRequest {
	int    n_task;      // número de tarea
	double t_arrival;   /* momento en el que llega la tarea (tiempo de creacion)*/
	double t_service;   /* tiempo de servicio asignado en FLOPS*/
};




// ordena dos elementos de tipo struct ClientRequest
// utilizado para poder ordenar la colaisutilizando la fucion xbt__dynar_sort
static int sort_function_asc(const void *e1, const void *e2)
{
	struct ClientRequest *c1 = *(void **) e1;
	struct ClientRequest *c2 = *(void **) e2;

	if (c1->t_service == c2->t_service)
		return 0;

	else if (c1->t_service < c2->t_service)
		return -1;
	else
		// if it returns a positive value when the first argument is greater than the second,
		// then xbt_dynar_sort will sort the array in ascending order
		//Here c1 > c2
		return 1;
}

// ordena dos elementos de tipo struct ClientRequest
// utilizado para poder ordenar la colaisutilizando la fucion xbt__dynar_sort
static int sort_function_desc(const void *e1, const void *e2)
{
	struct ClientRequest *c1 = *(void **) e1;
	struct ClientRequest *c2 = *(void **) e2;

	if (c1->t_service == c2->t_service)
		return 0;

	// If the comparison function returns a positive value when the first argument 
	// is less than the second, then xbt_dynar_sort will sort the array in descending order.
	else if (c1->t_service < c2->t_service)
		return 1;
	else
		return -1;
}


// Client function: genera las peticiones
int client(int argc, char *argv[])
{
	//size of the computation
	double task_comp_size = 0;

	double task_comm_size = 0;
	char sprintf_buffer[64];
	char mailbox[256];
	msg_task_t task = NULL;
	struct ClientRequest *req ;
	double t_arrival;
	int my_c;
	double t;
	int k;

	my_c = atoi(argv[0]);  // identifiador del cliente
	MSG_mailbox_set_async("c-0");   //mailbox asincrono

	for (k=0; k <NUM_TASKS; k++) {
		req = (struct ClientRequest *) malloc(sizeof(struct ClientRequest));
    
		/* espera la llegada de una peticion */
		/* ARRIVAL_RATE peticiones por segundo, lamda = ARRIVAL_RATE5 */
		t_arrival = exponential((double)ARRIVAL_RATE);
		MSG_process_sleep(t_arrival);

    /* crea la tarea */
		sprintf(sprintf_buffer, "Task_%d", k);

		// tiempo de llegada de la tarea
		req->t_arrival = MSG_get_clock();

		// tiempo de servicio asignada a la tarea = 1/SERVICE_RATE de seg
		// Como base se toma que en 1 seg se encutan MFLOPS_BASE flops
		t = exponential((double)SERVICE_RATE);

		// calculo del tiempo de servicio en funcion
		// de la velocidad del host del servidor
		req->t_service = MFLOPS_BASE * t;

		req->n_task = k;
		task_comp_size = req->t_service;
		task_comm_size = 0;

    task = MSG_task_create(sprintf_buffer, task_comp_size, task_comm_size,NULL);

		// asigna datos a la tarea
    MSG_task_set_data(task, (void *) req );

		// Código para el algoritmo de distribución
		
		// ahora se la envía a un único dispather
		sprintf(mailbox, "d-%d", 0);
	
		MSG_task_send(task, mailbox);   
	}

  /* finalizar */
  return 0;
}                               

/**
 * Dispatcher function, recibe las peticiones de los clientes y las envía a los servidores
*/
int dispatcher(int argc, char *argv[])
{
	int res;
  struct ClientRequest *req;
  msg_task_t task = NULL;
  msg_task_t new_task = NULL;
	int my_d;
	char mailbox[64];
	int k = 0;
	int s = 0;

	int server_a, server_b;
	int r1, r2;

	my_d = atoi(argv[0]);
	MSG_mailbox_set_async("d-0");   //mailbox asincrono

	int r =0;
	// int algoritmo_distribucion = atoi(argv[1]);
	int algoritmo_distribucion = RANDOM;
	// printf("algoritmo_distribucion dispatcher = %d\n", algoritmo_distribucion);
	
	while (1) {
    res = MSG_task_receive_with_timeout(&(task), MSG_host_get_name(MSG_host_self()) , MAX_TIMEOUT_SERVER);

    if (res != MSG_OK)
			break;

    req = MSG_task_get_data(task);

		// copia la tarea en otra
	  new_task = MSG_task_create(MSG_task_get_name(task),
		MSG_task_get_flops_amount(task), 0, NULL);

    MSG_task_set_data(new_task, (void *) req );	

    MSG_task_destroy(task);
    task = NULL;

		// ahora solo se envian los trabajos al servidor 0
		s = 0;

		////////////////////////////////////////////////////////////
		// ahora viene el algoritmo concreto del dispatcher	

		switch (algoritmo_distribucion)
		{
			case 0:
				/**
				 * ALGORITMOS DE DISTRIBUCIÓN ALEATORIA
				 * Para el algoritmo aleatorio se puede utilizar la función uniform_int definida en rand.c
				 * para generar un número aleatorio entre 0 y NUM_SERVERS-1
				*/
				s = uniform_int(0, NUM_SERVERS-1);
				// printf("s = %d\n", s);
				break;

			case 1:
				/**
				 * ALGORITMOS DE DISTRIBUCIÓN CICLICA
				 * Las tareas se van asignando en orden ciclico
				*/
				s = k % NUM_SERVERS;
				break;

			case 2:
				/**
				 * ALGORITMO DE DISTRIBUCIÓN SQF
				 * para el algoritmo SQF se puede consultar directamente el array Nsystem que almacena
				 * el número de elementos en cada uno de los servidores. Se trata de buscar el servidor con 
				 * el menor numero de elementos en la cola.
				*/
				for(r = 1; r < NUM_SERVERS; r++){
					if(Nsystem[r] < Nsystem[s] && Nqueue[r] < Nqueue[s]){
						s = r;
					}
				}
				break;

			case 3:
				/**
				 * ALGORITMO DE TWO RANDOM CHOICES
				 * para el algoritmo de two random choices se puede utilizar la función uniform_int definida en rand.c
				*/
				r1 = uniform_int(0, NUM_SERVERS-1);
				r2 = uniform_int(0, NUM_SERVERS-1);
				if(Nsystem[r1] < Nsystem[r2]){
					s = r1;
				}else{
					s = r2;
				}
				break;

		case 4:
			/**
			 * ALGORITMO DE DISTRIBUCIÓN TWO-RR-RANDOM-CHOICES
			*/
			server_a = k % NUM_SERVERS;
			server_b = uniform_int(0, NUM_SERVERS-1);
			if(Nsystem[server_a] < Nsystem[server_b]){
				s = server_a;
			}else{
				s = server_b;
			}
			break;
		
		default:
			printf("Error en el algoritmo de distribución\n");
			break;
		}

    sprintf(mailbox, "s-%d", s);
    MSG_task_send(new_task, mailbox);
		k++;
  }
	return 0;
}


/** server function  */
int server(int argc, char *argv[])
{
  msg_task_t task = NULL;
  msg_task_t t = NULL;
  struct ClientRequest *req;
  int res;
	int my_server;
	char buf[64];
	int algoritmo_planificacion = FCFS;
	
	my_server = atoi(argv[0]);
	// printf("\nSize of argv: %lu\n", sizeof(argv));

	// algoritmo_planificacion = atoi(argv[1]);
	// algoritmo_planificacion = 0;
	// printf("algoritmo_planificacion: %d", algoritmo_planificacion);
	// printf("algoritmo_planificacion server = %d\n", algoritmo_planificacion);

	sprintf(buf, "s-%d", my_server);
	MSG_mailbox_set_async(buf);   //mailbox asincrono

  while (1) {
    res = MSG_task_receive_with_timeout(&(task), MSG_host_get_name(MSG_host_self()) , MAX_TIMEOUT_SERVER);

		if (res != MSG_OK)
			break;

		req = MSG_task_get_data(task);
			
		// inserta la petición en la cola
		sg_mutex_lock(mutex[my_server]);
		Nqueue[my_server]++;   // un elemento mas en la cola 
		Nsystem[my_server]++;  // un elemento mas en el sistema 

		// Con otras políticas, habrá que ordenar después la cola utilizando
		// xbt_dynar_sort
		xbt_dynar_push(client_requests[my_server], (const char *)&req);
		switch (algoritmo_planificacion)
		{
			case 0:
				/**
				 * Politica de planificación FCFS
				 * se inserta la tarea en orden en que llega
				 * no es necesario ordenar la cola
				*/
				break;

			case 1:
				/**
				 * Politica de planificación SJF
				 * se inserta la tarea en orden de menor a mayor tiempo de servicio
				*/
				xbt_dynar_sort(client_requests[my_server], sort_function_asc);
				break;

			
			case 2:
				/**
				 * Politica de planificación LJF
				 * se inserta la tarea en orden de mayor a menor tiempo de servicio
				*/
				xbt_dynar_sort(client_requests[my_server], sort_function_desc);
				break;
		
			default:
				printf("Error en el algoritmo de planificación\n");
			break;
		}
		
		sg_cond_notify_one(cond[my_server]);  // despierta al proceso server
		sg_mutex_unlock(mutex[my_server]);

		MSG_task_destroy(task);
		task = NULL;
	}

	// marca el fin
 	sg_mutex_lock(mutex[my_server]);
	EmptyQueue[my_server] = 1;
	sg_cond_notify_all(cond[my_server]);
	sg_mutex_unlock(mutex[my_server]);

	return 0;
}       


/** server function  */
int dispatcherServer(int argc, char *argv[])
{

	int res;
  struct ClientRequest *req;
	msg_task_t task = NULL;
	msg_task_t ans_task = NULL;
	double Nqueue_avg = 0.0;
	double Nsystem_avg = 0.0;
	double c;
	int n_tasks = 0;
	int my_s;

	my_s = atoi(argv[0]);

	while (1) {
    sg_mutex_lock(mutex[my_s]);

		while ((Nqueue[my_s] ==  0) && (EmptyQueue[my_s] == 0)) {
			sg_cond_wait(cond[my_s], mutex[my_s]);
		}

		if ((EmptyQueue[my_s] == 1) && (Nqueue[my_s] == 0)) {
			sg_mutex_unlock(mutex[my_s]);
			break;
		}

		// extrae un elemento de la cola
		xbt_dynar_shift(client_requests[my_s], (char *) &req);

		Nqueue[my_s]--;  // un elemento menos en la cola

		n_tasks ++;

		// calculo de estadisticas
		Navgqueue[my_s] = (Navgqueue[my_s] * (n_tasks-1) + Nqueue[my_s]) / n_tasks;
		Navgsystem[my_s] = (Navgsystem[my_s] * (n_tasks-1) + Nsystem[my_s]) / n_tasks;

		sg_mutex_unlock(mutex[my_s]);

		// crea una tarea para su ejecución
		task = MSG_task_create("task", req->t_service, 0, NULL);

		MSG_task_execute(task);

		sg_mutex_lock(mutex[my_s]);
		Nsystem[my_s]--;  // un elemento menos en el sistema
		sg_mutex_unlock(mutex[my_s]);

		c = MSG_get_clock();  // tiempo de terminacion de la tarea
		tiempoMedioServicio[req->n_task] = c - (req->t_arrival);

		free(req);
		MSG_task_destroy(task);
	}
}

/**
 * Received the file with the cluster configuration need to run the tests
*/
void test_all(char *file)
{
	int argc;
	char str[50];
	int i;
	msg_process_t p;

  MSG_create_environment(file);

	// el proceso client es el que genera las peticiones
  MSG_function_register("client", client);

	// el proceso dispatcher es el que distribuye las peticiones que le llegan a los servidores
  MSG_function_register("dispatcher", dispatcher);

	// cada servidor tiene un proceso server que recibe las peticiones: server
	// y un proceso dispatcher que las ejecuta
	MSG_function_register("server", server);
	MSG_function_register("dispatcherServer", dispatcherServer);

	for (i=0; i < NUM_SERVERS; i++) {
		sprintf(str,"s-%d", i);
		argc = 1;
		char **argvc=xbt_new(char*,2);

		argvc[0] = bprintf("%d",i);
		//To define the algorithm of distribution
		argvc[1] = bprintf("%d",0);
		// argvc[1] = NULL;uuuuiu8

		// printf("argvc[0] = %s\n", argvc[0]);
		// printf("argvc[1] = %s\n", argvc[1]);

		p = MSG_process_create_with_arguments(str, server, NULL, MSG_get_host_by_name(str), argc, argvc);
		if (p == NULL) {
						printf("Error en ......... %d\n", i);
						exit(0);
		}
	}

	for (i=0; i < NUM_SERVERS; i++) {
					sprintf(str,"s-%d", i);
					argc = 1;
					char **argvc=xbt_new(char*,2);

					argvc[0] = bprintf("%d",i);
					argvc[1] = NULL;

					p = MSG_process_create_with_arguments(str, dispatcherServer, NULL, MSG_get_host_by_name(str), argc, argvc);
					if (p == NULL) {
									printf("Error en ......... %d\n", i);
									exit(0);
					}
	}

	for (i=0; i < NUM_CLIENTS; i++) {
							sprintf(str,"c-%d", i);
							argc = 1;
							char **argvc=xbt_new(char*,2);

							argvc[0] = bprintf("%d",i);
							argvc[1] = NULL;

							p = MSG_process_create_with_arguments(str, client, NULL, MSG_get_host_by_name(str), argc, argvc);
							if (p == NULL) {
											printf("Error en ......... %d\n", i);
											exit(0);
							}
	}

	 for (i=0; i < NUM_DISPATCHERS; i++) {
                sprintf(str,"d-%d", i);
                argc = 1;
                char **argvc=xbt_new(char*,2);

                argvc[0] = bprintf("%d",i);
								//To define the algorithm of distribution
								argvc[1] = bprintf("%d",0);
								// argvc[1] = NULL;

                p = MSG_process_create_with_arguments(str, dispatcher, NULL, MSG_get_host_by_name(str), argc, argvc);
                if (p == NULL) {
                        printf("Error en ......... %d\n", i);
                        exit(0);
		}
	}

	return;
}

void generate_bootstrap_sample(double data[], int size, double bootstrap_sample[], int sample_size) {
    for (int i = 0; i < sample_size; i++) {
        int random_index = rand() % size;
        bootstrap_sample[i] = data[random_index];
    }
}

void sort_array(double array[], int size) {
	int i, j;
	double temp;

	for (i = 0; i < size - 1; i++) {
			for (j = 0; j < size - 1 - i; j++) {
					if (array[j] > array[j + 1]) {
							// Intercambia los elementos si están en el orden incorrecto
							temp = array[j];
							array[j] = array[j + 1];
							array[j + 1] = temp;
					}
			}
	}
}


/**
 * 		Main function
*/
int main(int argc, char *argv[])
{
	printf("argv[0] = %s\n", argv[0]);
	printf("argv[1] = %s\n", argv[1]);
	printf("argv[2] = %s\n", argv[2]);

  msg_error_t res = MSG_OK;
	int i, j;

	double t_medio_servicio = 0.0;	// tiempo medio de servicio de cada tarea
	double q_medio = 0.0; 					// tamaño medio de la cola (esperando a ser servidos)
  double n_medio = 0.0;						// número medio de tareas en el sistema (esperando y ejecutando)

	if (argc < 3) {
		printf("Usage: %s platform_file lamda \n", argv[0]);
		exit(1);
	}

	seed((int) time(NULL));
	ARRIVAL_RATE = atof(argv[2]) *  NUM_SERVERS;

	MSG_init(&argc, argv);

	for (i = 0; i < NUM_SERVERS; i++) {
		Nqueue[i] = 0;
		Nsystem[i] = 0;
		EmptyQueue[i]= 0;
		mutex[i] = sg_mutex_init();
		cond[i] = sg_cond_init();
		client_requests[i] = xbt_dynar_new(sizeof(struct ClientRequest *), NULL);
	}

	test_all(argv[1]);

	res = MSG_main();

	int index_temporal;
	int numero_samples = 1000;
	double tiempoMedioServicio_samples[numero_samples];
	double q_medio_samples[numero_samples];
	double n_medio_samples[numero_samples];

	printf("Valores obtenidos a nivel general\n");
	for (i = 0; i < NUM_TASKS; i++){
		// printf("%g ", tiempoMedioServicio[i]);
		t_medio_servicio = t_medio_servicio + tiempoMedioServicio[i];
	}
	t_medio_servicio = t_medio_servicio / (NUM_TASKS);

	for (i = 0; i < NUM_SERVERS; i++){
		q_medio = q_medio + Navgqueue[i];
		n_medio = n_medio + Navgsystem[i];
	}
	q_medio = q_medio / (NUM_SERVERS);
	n_medio = n_medio / (NUM_SERVERS);
	printf("TiempoMedioServicio: %g\n", t_medio_servicio);
	printf("TamañoMediocola: %g\n", q_medio);
	printf("TareasMediasEnElSistema: %g\n", n_medio);
	printf("tareas: %d\n\n", NUM_TASKS);

	
	for (int j = 0; j < numero_samples; j++)
	{
		t_medio_servicio = 0.0;
		for (i = 0; i < NUM_TASKS; i++){
			index_temporal = uniform_int(0, NUM_TASKS-1);
			// printf("%g ", tiempoMedioServicio[i]);
			t_medio_servicio = t_medio_servicio + tiempoMedioServicio[index_temporal];
		}
		t_medio_servicio = t_medio_servicio / (NUM_TASKS);
		tiempoMedioServicio_samples[j] = t_medio_servicio;
	}
	sort_array(tiempoMedioServicio_samples, numero_samples);
	// Calcular los percentiles para el intervalo de confianza (por ejemplo, para un intervalo del 95%)
	double lower_bound = tiempoMedioServicio_samples[(int)(0.025 * numero_samples)];
	double upper_bound = tiempoMedioServicio_samples[(int)(0.975 * numero_samples)];

	// Calcular el ancho del intervalo de confianza bootstrap
  double bootstrap_interval_width = upper_bound - lower_bound;

	// Calcular el error cometido (mitad del ancho del intervalo)
  double error_cometido = bootstrap_interval_width / 2;

	// Imprimir resultados
	printf("TiempoMedioServicio\n");
	printf("Intervalo de Confianza Bootstrap: (%g, %g)\n", lower_bound, upper_bound);
	printf("Ancho del Intervalo Bootstrap: %g\n", bootstrap_interval_width);
	printf("Error Cometido: %g\n\n", error_cometido);
	
	for (int j = 0; j < numero_samples; j++)
	{
		q_medio = 0.0;
		n_medio = 0.0;
		for (i = 0; i < NUM_SERVERS; i++){
			index_temporal = uniform_int(0, NUM_SERVERS-1);
			q_medio = q_medio + Navgqueue[index_temporal];
			n_medio = n_medio + Navgsystem[index_temporal];
		}
		q_medio = q_medio / (NUM_SERVERS);
		q_medio_samples[j] = q_medio;

		n_medio = n_medio / (NUM_SERVERS);
		n_medio_samples[j] = n_medio;
	}
	sort_array(q_medio_samples, numero_samples);
	// Calcular los percentiles para el intervalo de confianza (por ejemplo, para un intervalo del 95%)
	lower_bound = q_medio_samples[(int)(0.025 * numero_samples)];
	upper_bound = q_medio_samples[(int)(0.975 * numero_samples)];

	// Calcular el ancho del intervalo de confianza bootstrap
  bootstrap_interval_width = upper_bound - lower_bound;

	// Calcular el error cometido (mitad del ancho del intervalo)
  error_cometido = bootstrap_interval_width / 2;
	printf("TamañoMediocola\n");
	printf("Intervalo de Confianza Bootstrap: (%g, %g)\n", lower_bound, upper_bound);
	printf("Ancho del Intervalo Bootstrap: %g\n", bootstrap_interval_width);
	printf("Error Cometido: %g\n\n", error_cometido);

	sort_array(n_medio_samples, numero_samples);
		// Calcular los percentiles para el intervalo de confianza (por ejemplo, para un intervalo del 95%)
	lower_bound = n_medio_samples[(int)(0.025 * numero_samples)];
	upper_bound = n_medio_samples[(int)(0.975 * numero_samples)];

	// Calcular el ancho del intervalo de confianza bootstrap
  bootstrap_interval_width = upper_bound - lower_bound;

	// Calcular el error cometido (mitad del ancho del intervalo)
  error_cometido = bootstrap_interval_width / 2;
	printf("TareasMediasEnElSistema\n");
	printf("Intervalo de Confianza Bootstrap: (%g, %g)\n", lower_bound, upper_bound);
	printf("Ancho del Intervalo Bootstrap: %g\n", bootstrap_interval_width);
	printf("Error Cometido: %g\n\n", error_cometido);

	// t_medio_servicio = t_medio_servicio / (NUM_TASKS);
	// q_medio = q_medio / (NUM_SERVERS);
	// n_medio = n_medio / (NUM_SERVERS);

	// double std_dev_t_medio_servicio = calculate_std_dev(tiempoMedioServicio, NUM_TASKS, t_medio_servicio);

	// Calcula el intervalo de confianza del 95%
	// double lower_bound_t_medio_servicio, upper_bound_t_medio_servicio;
	// calculate_confidence_interval(std_dev_t_medio_servicio, NUM_TASKS, t_medio_servicio, &lower_bound_t_medio_servicio, &upper_bound_t_medio_servicio);

	// printf("tiempoMedioServicio \t TamañoMediocola \t    TareasMediasEnElSistema  \t   tareas\n");
	// printf("%g \t\t\t %g \t\t\t  %g  \t\t\t  %d \n", t_medio_servicio, q_medio,  n_medio, NUM_TASKS );
	// printf("iteracion[%d] t_medio_servicio = %g\n", j, t_medio_servicio);
	// printf("std_dev_t_medio_servicio = %g\n", std_dev_t_medio_servicio);
	// printf("lower_bound_t_medio_servicio = %g\n", lower_bound_t_medio_servicio);
	// printf("upper_bound_t_medio_servicio = %g\n", upper_bound_t_medio_servicio);
	// double error = (upper_bound_t_medio_servicio - lower_bound_t_medio_servicio) / 2;
	// printf("error = %g\n", upper_bound_t_medio_servicio);


	//printf("Simulation time %g\n", MSG_get_clock());

	for (i = 0; i < NUM_SERVERS; i++) {
		xbt_dynar_free(&client_requests[i]);
	}
	
	if (res == MSG_OK)
			return 0;
	else
			return 1;
}

// Función para calcular la desviación estándar
double calculate_std_dev(double data[], int size, double mean) {
    double sum_squared_diff = 0.0;
    for (int i = 0; i < size; i++) {
        sum_squared_diff += pow(data[i] - mean, 2);
    }
    return sqrt(sum_squared_diff / size);
}

// Función para calcular el intervalo de confianza
void calculate_confidence_interval(double std_dev, int size, double mean, double *lower_bound, double *upper_bound) {
	double margin_error = 1.96 * (std_dev / sqrt(size));  // 1.96 para un intervalo del 95%
	// printf("margin_error = %g\n", margin_error);
	*lower_bound = mean - margin_error;
	*upper_bound = mean + margin_error;
}