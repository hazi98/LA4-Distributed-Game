/*
	Alcázar Becerra Sergio Iván
	García Guzmán Yoltic Jassiel
	Kuzmanovski Reyes Bojko Yair
	Solorza Buenrostro Jose Carlos
*/
/* -------------------------------------------------------------------------- */
/* chat server                                                                */
/*                                                                            */
/* server program that works with datagram-type sockets by receiving the mes- */
/* sages of a set of clients, displaying them, and returning the message sent */
/* by each client to the rest, until the last client sends an "exit" command  */
/*                                                                            */
/* -------------------------------------------------------------------------- */
// Directives
#pragma GCC diagnostic ignored "-Wformat-truncation"
#pragma GCC diagnostic ignored "-Wformat-overflow"

/* -------------------------------------------------------------------------- */
/* libraries needed for the program execution                                 */
/* -------------------------------------------------------------------------- */
#include <netinet/in.h> /* TCP/IP library                      */
#include <arpa/inet.h>  /* Newer functionality for  the TCP/IP */
                        /* library                             */
#include <sys/socket.h> /* sockets library                     */
#include <sys/types.h>  /* shared data types                   */
#include <stdio.h>      /* standard input/output               */
#include <unistd.h>     /* unix standard functions             */
#include <string.h>     /* text handling functions             */
#include <time.h>       // Library for time
#include <stdlib.h>
#include <pthread.h> /* libraries for thread handling       */
#include <signal.h>  /* libraries for starting game timer   */
#include <wchar.h>
#include <locale.h>
#include <stdbool.h>
/* -------------------------------------------------------------------------- */
/* global definitions                                                         */
/* -------------------------------------------------------------------------- */
#define BUFFERSIZE 4096 /* buffer size                         */
#define MAX_MEMBERS 20  /* maximum number of members in room   */
#define MAX_RONDAS 5
#define TIMEOUT 30 // Heartbeat timeout
const char *ip = "127.0.0.1";
const int max_time = 10; // Wait for players timeout

/* -------------------------------------------------------------------------- */
/* Prototipos de las funciones                                                */
/* -------------------------------------------------------------------------- */
void timeout(int);

/* -------------------------------------------------------------------------- */
/* global variables and structures                                            */
/* -------------------------------------------------------------------------- */
struct data
{
  int data_type;                                  /* type of data sent         */
  int chat_id;                                    /* chat id sent by server    */
  char data_text[BUFFERSIZE - (sizeof(int) * 2)]; /* data sent                 */
};

struct member
{
  int chat_id;                                /* chat id                      */
  char alias[BUFFERSIZE - (sizeof(int) * 2)]; /* member alias                 */
  struct sockaddr_in address;                 /* address of the member        */
  clock_t *heartbeat_time;                    // Using pointers to dynamically modify the time within the thread
};

typedef struct
{
  struct member *list;
  int *participants;
  int sfd;
} arguments;

typedef enum
{
  espada,   // ♠
  corazon,  // ♥
  diamante, // ♦
  trebol    // ♣
} palos;

typedef struct
{
  int numero;
  palos palo;
  int jugador_id;
  bool eliminada;
} cartas;

typedef struct
{
  cartas cartas[5];
} mazos;

typedef struct
{
  int chat_id;
  int mazo_id;
} jugadores;

typedef struct
{
  // Juego
  cartas baraja[52];
  int ganadores[MAX_RONDAS]; // Array que guarda los IDs de los ganadores de cada ronda
  // Ronda
  int num_rondas;        // Numero de rondas
  int cont_cartas_ronda; // Contador de las cartas en la ronda
  mazos mazo[4];         // Mazos por cada jugador
  mazos mazo_ronda;      // Cartas en la mesa en la ronda
  // Jugadores
  int contador_jugadores;
  struct member list[20];
  jugadores pila_jugadores[4];
  // Comunicacion
  int sfd;
} game_args;

// Global variables (Shared between threads)
int reloj_control;                       // Solo puede existir un contador para iniciar la partida.
volatile bool game_started = false;      // Bandera para controlar el estado del juego.
volatile bool init_game = false;         // Bandera para indicar si se tiene que inicializar el juego.
volatile bool start_next_round = false;  // Bandera para saber si el hilo de control de partida debe de iniciar la siguiente ronda.
volatile bool round_deck_full = false;   // Bandera para esperar a que todos los jugadores pongan una carta en la mesa.
volatile bool end_game = false;          // Bandera para indicar al hilo de juego que termine su ejecución.
volatile bool clean_player_list = false; // Bandera para indicar que se debe de limpiar la lista de jugadores
volatile bool client_dc = false;         // Bandera para gestionar que un cliente se ha desconectado
volatile int dc_chat_id = -1;            // Valor del chat_id del cliente que se ha desconectado

// Hilo encargado de checar la actividad de los clientes.
void *check_heartbeats(void *arg)
{
  arguments *args = arg;
  char text1[BUFFERSIZE]; // reading buffer
  clock_t target_time;
  target_time = clock() + 5 * CLOCKS_PER_SEC;

  // Validamos que el cliente siga dentro del chat.
  while (1)
  {
    int i = 0;
    for (i = 0; i < MAX_MEMBERS; i++)
    {
      if ((args->list[i].chat_id != -1) && (clock() > *args->list[i].heartbeat_time))
      {
        // Time exceeded for the heartbeat, log out the client.
        printf("Client [%s] is leaving the chat room due to inactivity.\n", args->list[i].alias);
        sprintf(text1, "Client (%s) is leaving the chat room due to inactivity.\n", args->list[i].alias);
        // Tell the game thread that a client has disconnected
        client_dc = true;
        dc_chat_id = args->list[i].chat_id;
        args->list[i].chat_id = -1;
        args->participants -= 1;

        for (i = 0; i < MAX_MEMBERS; ++i)
          if (args->list[i].chat_id != -1)
            sendto(args->sfd, text1, strlen(text1), 0, (struct sockaddr *)&(args->list[i].address), sizeof(struct sockaddr_in));
      }
    }
  }
}

void print_card(int i, cartas carta, void *text1)
{
  char simbolo[15] = "";
  switch (carta.palo)
  {
  case espada:
    strcpy(simbolo, "espada");
    break;

  case corazon:
    strcpy(simbolo, "corazon");
    break;

  case diamante:
    strcpy(simbolo, "diamante");
    break;

  case trebol:
    strcpy(simbolo, "trebol");
    break;

  default:
    break;
  }
  printf("Carta [%d]: %d %s\n", (i + 1), (carta.numero), simbolo);
  if (text1 != false)
    sprintf(text1, "Carta [%d]: %d %s\n", (i + 1), (carta.numero), simbolo);
}

void game_round(game_args *args)
{
  // Variables
  int i, j, k, origen, destino, contador_cartas;
  char text1[BUFFERSIZE]; // reading buffer
  cartas carta_aux;
  struct sockaddr_in player_address;
  bool add_encontrado = false;
  // Revolver las cartas
  for (i = 0; i < 52; i++)
  {
    // Escoger origen y destino
    do
    {
      origen = rand() % 52;
      destino = rand() % 52;
    } while (origen == destino);
    // Intercambiar cartas
    carta_aux = args->baraja[destino];
    args->baraja[destino] = args->baraja[origen];
    args->baraja[origen] = carta_aux;
  }
  char text_mazo[BUFFERSIZE] = "";
  char text_aux[BUFFERSIZE] = "";
  // Resetear contador de cartas para repartir la baraja
  contador_cartas = 0;
  // Apartar 5 cartas para cada jugador activo
  for (i = 0; i < args->contador_jugadores; i++)
  {
    memset(text_mazo, 0, sizeof text_mazo);
    if (args->pila_jugadores[i].chat_id != -1)
    {
      // Obtener el chat_id del jugador actual
      int id_actual = args->pila_jugadores[i].chat_id;
      // Limpiar el buffer de texto
      memset(text_aux, 0, sizeof text_aux);
      printf("-----Round %d-----\nDeck %d\n", args->num_rondas + 1, i);
      // Copiar en el buffer el siguiente texto
      sprintf(text_aux, "-----Round %d-----\nDeck %d\n", args->num_rondas + 1, i);
      strcat(text_mazo, text_aux);
      // Crear el deck para el jugador
      for (j = 0; j < 5; j++)
      {
        // Saltar la carta si ya está usada
        while (args->baraja[contador_cartas].eliminada == true)
          contador_cartas++;
        // Asignar el id de la carta al jugador actual
        args->baraja[contador_cartas].jugador_id = id_actual;
        // Poner la carta en el mazo del jugador
        args->mazo[i].cartas[j] = args->baraja[contador_cartas];
        // Limpiar el texto auxiliar
        memset(text_aux, 0, sizeof text_aux);
        // Imprimir la carta en el texto auxiliar
        print_card(j, args->mazo[i].cartas[j], text_aux);
        // Concatenar el texto auxiliar con el buffer de texto
        strcat(text_mazo, text_aux);
        // Aumentar la cuenta de cartas
        contador_cartas++;
      }
      // Asignar el mazo ID al jugador
      args->pila_jugadores[i].mazo_id = i;
      // Buscar la direccion del jugador actual
      for (k = 0; k < 20; k++)
      {
        if (args->list[k].chat_id == id_actual)
        {
          // Recuperar el address
          player_address = args->list[k].address;
          add_encontrado = true;
          break;
        }
      }
      if (add_encontrado == true)
      {
        // Enviar el mazo al jugador
        sendto(args->sfd, text_mazo, strlen(text_mazo), 0, (struct sockaddr *)&(player_address), sizeof(struct sockaddr_in));
      }
      else
      {
        printf("Error finding a player to send the deck.\n");
        end_game = true;
      }
    }
  }
  // Enviar mensaje a jugadores para que elijan una carta
  sprintf(text1, "Players, choose a card from your deck.\n");
  for (i = 0; i < args->contador_jugadores; ++i)
    if (args->pila_jugadores[i].chat_id != -1)
      sendto(args->sfd, text1, strlen(text1), 0, (struct sockaddr *)&(args->list[args->pila_jugadores[i].chat_id].address), sizeof(struct sockaddr_in));

  // Resetear el contador de cartas en la ronda
  args->cont_cartas_ronda = 0;
}

// A comparator function used by qsort
int compare(const void *a, const void *b)
{
  return (*(int *)a - *(int *)b);
}

// Hilo encargado de gestionar el juego
void *game_thread(void *arg)
{
  game_args *args = arg;
  char text1[BUFFERSIZE];    // write buffer
  char text_aux[BUFFERSIZE]; // write buffer
  char *ret;

  // Loop principal del juego
  do
  {
    // 1. Iniciar el juego si se da la indicación e inicializar la baraja
    if (game_started == true && init_game == true)
    {
      // Inicializacion de datos de control
      int contador_cartas, i, j;
      // Inicializacion de la baraja
      for (i = 0; i < 52; i++)
      {
        args->baraja[i].numero = -1;
        args->baraja[i].palo = 0;
        args->baraja[i].jugador_id = -1;
        args->baraja[i].eliminada = false;
      }
      for (i = 0; i < 5; i++)
      {
        args->mazo_ronda.cartas[i].jugador_id = -1;
        args->mazo_ronda.cartas[i].numero = -1;
      }
      // Asignar 13 numeros a cada palo
      contador_cartas = 0;
      for (i = 0; i < 4; i++)
      {
        for (j = 0; j < 13; j++)
        {
          args->baraja[contador_cartas].numero = j + 1;
          args->baraja[contador_cartas].palo = (palos)i;
          contador_cartas++;
        }
      }
      // Con éste mensaje, el servidor se pone en modo juego.
      sprintf(text1, "Starting game");
      for (i = 0; i < args->contador_jugadores; ++i)
        sendto(args->sfd, text1, strlen(text1), 0, (struct sockaddr *)&(args->list[args->pila_jugadores[i].chat_id].address), sizeof(struct sockaddr_in));

      // Indicar que la baraja ha sido inicializada
      init_game = false;
      // Dar la señal para que comiencen las rondas
      args->num_rondas = 0;
      start_next_round = true;
    }

    // 2. Gestionar las rondas
    if (game_started == true && start_next_round == true && args->num_rondas < MAX_RONDAS)
    {
      // Iniciar ronda
      game_round(args);
      // Esperar a la siguiente ronda
      start_next_round = false;
    }

    // 3. Esperar a que los jugadores pongan las cartas sobre la mesa, y cuando terminen, se calcula al ganador de la ronda.
    if (game_started == true && round_deck_full == true)
    {
      // Calcular el ganador
      int i, j, num_max = 0, jugador_id, contador_cartas = 0;
      bool cartas_eliminadas = false;
      for (i = 0; i < 5; i++)
      {
        if (args->mazo_ronda.cartas[i].numero > num_max)
        {
          num_max = args->mazo_ronda.cartas[i].numero;
          jugador_id = args->mazo_ronda.cartas[i].jugador_id;
        }
      }
      // Guardar el ganador de la ronda
      args->ganadores[args->num_rondas] = jugador_id;
      // Publicar el ganador
      memset(text1, 0, sizeof text1);
      memset(text_aux, 0, sizeof text_aux);
      sprintf(text_aux, "Round %d winner is player [%d]\n", (args->num_rondas + 1), jugador_id);
      strcat(text1, text_aux);
      sprintf(text_aux, "Round deck was:\n");
      strcat(text1, text_aux);
      for (i = 0; i < 5; i++)
      {
        // Si se encuentra una carta valida en el mazo de la ronda
        if (args->mazo_ronda.cartas[i].jugador_id != -1)
        {
          // Imprimirla
          print_card(i, args->mazo_ronda.cartas[i], text_aux);
          strcat(text1, text_aux);
        }
      }
      sprintf(text_aux, "-----Round %d has ended-----\n", args->num_rondas + 1);
      strcat(text1, text_aux);
      for (i = 0; i < args->contador_jugadores; ++i)
        sendto(args->sfd, text1, strlen(text1), 0, (struct sockaddr *)&(args->list[args->pila_jugadores[i].chat_id].address), sizeof(struct sockaddr_in));
      // Aumentar el numero de ronda
      args->num_rondas++;
      // Marcar el inicio de la siguiente ronda
      start_next_round = true;
      memset(text1, 0, sizeof text1);
      printf("Starting next round...\n");
      sprintf(text1, "Starting next round...\n");
      for (i = 0; i < args->contador_jugadores; ++i)
        sendto(args->sfd, text1, strlen(text1), 0, (struct sockaddr *)&(args->list[args->pila_jugadores[i].chat_id].address), sizeof(struct sockaddr_in));

      // Sacar las cartas usadas de la baraja
      do
      {
        printf("Finding cards to remove...\n");
        // Buscar IDs en la baraja
        for (i = 0; i < 52; i++)
        {
          // Si la carta en la baraja ya tiene ID válido, hay que quitarla
          if (args->baraja[i].jugador_id >= 0)
          {
            contador_cartas++;
            cartas_eliminadas = false;
            printf("Removing card: ");
            print_card(i, args->baraja[i], false);
            for (j = i; j < 51; j++)
            {
              // Si la ultima carta se tiene que eliminar
              if (j + 1 > 51 - contador_cartas)
              {
                // Marcar como eliminada para que al recorrer, las sobrantes también se marquen como eliminadas
                args->baraja[j + 1].eliminada = true;
                args->baraja[j + 1].numero = 0;
              }
              // Recorrer las cartas a la izquierda
              args->baraja[j] = args->baraja[j + 1];
            }

            // Ir una carta atrás
            i--;
          }
          else
          {
            cartas_eliminadas = true;
          }
        }
      } while (cartas_eliminadas == false);
      printf("Current deck\n");
      for (i = 0; i < 52; i++)
      {
        print_card(i, args->baraja[i], false);
      }

      // Limpiar el mazo de la mesa
      for (i = 0; i < 5; i++)
      {
        args->mazo_ronda.cartas[i].jugador_id = -1;
        args->mazo_ronda.cartas[i].numero = -1;
      }
      // Decir que la mesa está limpia
      round_deck_full = false;
    }

    // 4. Comprobar si se debe de continuar el juego o se acaba y se elige al ganador.
    if (game_started == true && args->num_rondas >= MAX_RONDAS)
    {
      // Elegir al ganador
      int i, max_count = 1, res = args->ganadores[0], curr_count = 1;
      // Ordenar array
      qsort(args->ganadores, 5, sizeof(int), compare);
      // Encontrar la frecuencia máxima
      for (i = 1; i < 5; i++)
      {
        if (args->ganadores[i] == args->ganadores[i - 1])
          curr_count++;
        else
        {
          if (curr_count > max_count)
          {
            max_count = curr_count;
            res = args->ganadores[i - 1];
          }
          curr_count = 1;
        }
      }
      // Si el ultimo elemento es el más frecuente
      if (curr_count > max_count)
      {
        max_count = curr_count;
        res = args->ganadores[5 - 1];
      }
      // Publicar el ganador del juego
      memset(text1, 0, sizeof text1);
      printf("Game winner is player [%d], congrats!\n=====GAME OVER=====\n", res);
      sprintf(text1, "Game winner is player [%d], congrats!\n=====GAME OVER=====\n", res);
      for (i = 0; i < args->contador_jugadores; ++i)
        sendto(args->sfd, text1, strlen(text1), 0, (struct sockaddr *)&(args->list[args->pila_jugadores[i].chat_id].address), sizeof(struct sockaddr_in));

      // Marcar el fin del juego
      end_game = true;
    }

    // 6. Si un cliente se desconecta, devolver sus cartas al mazo
    if (game_started == true && client_dc == true)
    {
      // Regenerar la lista de jugadores activos
      int i, j;
      bool encontrado = false;
      char alias[BUFFERSIZE - (sizeof(int) * 2)];
      for (i = 0; i < args->contador_jugadores; i++)
      {
        if (args->pila_jugadores[i].chat_id == dc_chat_id && args->pila_jugadores[i].chat_id != -1)
        {
          // Hemos encontrado al jugador desconectado
          encontrado = true;
          strcpy(alias, args->list[dc_chat_id].alias);
          for (j = i; j < args->contador_jugadores; j++)
          {
            if (j + 1 >= args->contador_jugadores)
            {
              args->pila_jugadores[j].chat_id = -1;
              args->pila_jugadores[j].mazo_id = -1;
            }
            else
            {
              // Recorrer
              args->pila_jugadores[j] = args->pila_jugadores[j + 1];
            }
          }
          break;
        }
      }
      // Comprobar que el cliente desconectado ha sido un jugador
      if (encontrado == true)
      {
        // Devolver las cartas
        for (i = 0; i < 52; i++)
        {
          if (args->baraja[i].jugador_id == dc_chat_id)
          {
            // Quitar dueño
            args->baraja[i].jugador_id = -1;
            args->baraja[i].eliminada = false;
          }
        }
        // Reducir el contador de jugadores
        args->contador_jugadores--;
        // Informar a los jugadores que se ha desconectado un jugador.
        printf("Player [%s] has disconnected.\n", alias);
        sprintf(text1, "Player [%s] has disconnected.\n", alias);
        for (i = 0; i < args->contador_jugadores; ++i)
          sendto(args->sfd, text1, strlen(text1), 0, (struct sockaddr *)&(args->list[args->pila_jugadores[i].chat_id].address), sizeof(struct sockaddr_in));

        // Comprobar si el juego todavía puede seguir
        if (args->contador_jugadores < 2)
        {
          printf("Game is ending because there are not enough players.\n");
          sprintf(text1, "Game is ending because there are not enough players.\n");
          for (i = 0; i < args->contador_jugadores; ++i)
            sendto(args->sfd, text1, strlen(text1), 0, (struct sockaddr *)&(args->list[args->pila_jugadores[i].chat_id].address), sizeof(struct sockaddr_in));

          // Marcar el fin del juego.
          end_game = true;
        }
        else
        {
          // Recalcular si el mazo en la mesa está lleno
          if (args->cont_cartas_ronda == args->contador_jugadores)
          {
            round_deck_full = true;
          }
        }
      }
    }

    // 5. Terminar el juego
    if (game_started == true && end_game == true)
    {
      // Finalizar el hilo
      printf("Game thread shutting down\n");
      game_started = false;
      clean_player_list = true;
    }

  } while (end_game == false);
  printf("Game thread exiting loop\n");
  end_game = false;
  pthread_exit(ret);
}

/* -------------------------------------------------------------------------- */
/* main ()                                                                    */
/*                                                                            */
/* main function of the system                                                */
/* -------------------------------------------------------------------------- */
int main()
{
  srand(time(NULL));
  struct sockaddr_in sock_read;    /* structure for the read socket       */
  struct sockaddr_in sock_write;   /* structure for the write socket      */
  struct data message;             /* message to sendto the server        */
  struct member list[MAX_MEMBERS]; /* list of members in room             */
  char text1[BUFFERSIZE];          /* reading buffer                      */
  char *client_address;            /* address of the sending client       */
  int participants;                /* number of participats in the chat   */
  int i, j, k;                     /* counters                            */
  int sfd;                         /* socket descriptor                   */
  int read_char;                   /* read characters                     */
  int ret_hb;                      // Heartbeat return
  int ret_game;                    // Retorno del hilo de juego
  socklen_t length;                /* size of the read socket             */
  pthread_t thread1;               // Thread ID
  // PARA EL CONTROL DE LOS JUGADORES *******************************************
  pthread_t thread2; // Game thread ID
  // PARA EL CONTROL DEL JUEGO **************************************************
  int esperar_jugadores = 0;
  game_args gameargs;
  /* ---------------------------------------------------------------------- */
  /* structure of the socket that will read what comes from the client      */
  /* ---------------------------------------------------------------------- */
  sock_read.sin_family = AF_INET;
  sock_read.sin_port = 27007;
  inet_aton(ip, (struct in_addr *)&sock_read.sin_addr);
  memset(sock_read.sin_zero, 0, 8);

  /* ---------------------------------------------------------------------- */
  /* Instrucctions for publishing the socket                                */
  /* ---------------------------------------------------------------------- */
  sfd = socket(AF_INET, SOCK_DGRAM, 0);
  bind(sfd, (struct sockaddr *)&(sock_read), sizeof(sock_read));

  /* ---------------------------------------------------------------------- */
  /* Inicialization of several variables                                    */
  /* ---------------------------------------------------------------------- */
  setlocale(LC_ALL, "");
  for (i = 0; i < 4; i++)
  {
    gameargs.pila_jugadores[i].chat_id = -1;
    gameargs.pila_jugadores[i].mazo_id = -1;
  }
  gameargs.contador_jugadores = 0;
  // Inicializacion de la baraja
  for (i = 0; i < 52; i++)
  {
    gameargs.baraja[i].numero = -1;
    gameargs.baraja[i].palo = 0;
    gameargs.baraja[i].jugador_id = -1;
  }
  for (i = 0; i < 5; i++)
  {
    gameargs.mazo_ronda.cartas[i].jugador_id = -1;
    gameargs.mazo_ronda.cartas[i].numero = -1;
  }
  length = sizeof(struct sockaddr);
  message.data_text[0] = '\0';
  participants = 0;
  for (i = 0; i < MAX_MEMBERS; ++i)
  {
    list[i].chat_id = -1;
    // Create the dynamic clock
    list[i].heartbeat_time = malloc(sizeof(clock_t));
    *list[i].heartbeat_time = clock() + TIMEOUT * CLOCKS_PER_SEC;
  }

  // Creacion de argumentos para el heartbeat
  arguments *args = malloc(sizeof(arguments));
  struct member *list_ptr;
  list_ptr = list;
  args->list = list_ptr;
  args->participants = &participants;
  args->sfd = sfd;

  // Creacion del hilo de heartbeat
  ret_hb = pthread_create(&thread1, NULL, check_heartbeats, (void *)args);
  if (ret_hb)
    printf("Error creating heartbeat thread.\n");
  else
    printf("Heartbeat checker thread created.\n");

  /* ---------------------------------------------------------------------- */
  /* The socket is  read and the messages are  answered until the word exit */
  /* is received by the last member of the chat                             */
  /* ---------------------------------------------------------------------- */
  while (strcmp(message.data_text, "shutdown") != 0)
  {

    /* first we read the message sent from any client                     */
    read_char = recvfrom(sfd, (struct data *)&(message), sizeof(struct data), 0, (struct sockaddr *)&(sock_write), &(length));
    //printf("Type:[%d], Part:[%d], Mess:[%s]\n", message.data_type, message.chat_id, message.data_text);

    /* ------------------------------------------------------------------ */
    /* Now we display the address of the client that sent the message     */
    /* ------------------------------------------------------------------ */
    client_address = inet_ntoa(sock_write.sin_addr);
    printf("Server: From Client with Address-[%s], Port-[%d], AF-[%d].\n", client_address, sock_write.sin_port, sock_write.sin_family);

    /* if data_type == 0, it means that the client is logging in          */
    if (message.data_type == 0) /* Add new member to chat room         */
    {
      i = 0;
      while ((list[i].chat_id != -1) && (i < MAX_MEMBERS))
        ++i;
      if (i >= MAX_MEMBERS)
        i = -1; /* i = -1 means client rejected        */
      else
      {
        // Update the clock
        *list[i].heartbeat_time = clock() + TIMEOUT * CLOCKS_PER_SEC;
        list[i].chat_id = i;
        strcpy(list[i].alias, message.data_text);
        memcpy((struct sockaddr_in *)&(list[i].address), (struct sockaddr_in *)&(sock_write), sizeof(struct sockaddr_in));
        ++participants;
      }
      printf("Participants in Room:[%d]\n\n", participants);

      // Send i to client
      sendto(sfd, (void *)&(i), sizeof(int), 0, (struct sockaddr *)&(sock_write), sizeof(struct sockaddr));

      /* We notify the entrance to the chat of the new participant      */
      /* Variable i still keeps the number of the last member added     */
      sprintf(text1, "Participant [%s] joined to the group.\n", message.data_text);
      for (j = 0; j < MAX_MEMBERS; ++j)
        if ((j != i) && (list[j].chat_id != -1))
          sendto(sfd, text1, strlen(text1), 0, (struct sockaddr *)&(list[j].address), sizeof(struct sockaddr_in));
    }

    /* if data_type == 1, it means that this is a message                 */
    if (message.data_type == 1)
    {
      /* if the message received is the word "exit", it  means that the */
      /* client is leaving the room, so we report that to the other cli */
      /* ents, and change the value of its id in the list to -1         */
      if (strcmp(message.data_text, "exit") == 0)
      {
        sprintf(text1, "Client [%s] is leaving the chat room.", list[message.chat_id].alias);
        list[message.chat_id].chat_id = -1;
        --participants;
      }
      // *************************************************************************
      // EN ESTA SECCIÓN INICIALIZAMOS EL JUEGO.
      // *************************************************************************
      else if (strcmp(message.data_text, "Start Game") == 0)
      {
        // Inicializamos el timer.
        if (reloj_control == 0)
        { // Sólo si el contador no está en acción, lo inicializamos.
          reloj_control++;
          signal(SIGALRM, timeout);
          alarm(max_time);
        }
        // Agregamos a los jugadores que empiecen el juego a la pila de jugadores.
        if (gameargs.contador_jugadores < 4)
        {
          gameargs.pila_jugadores[gameargs.contador_jugadores].chat_id = message.chat_id;
          gameargs.contador_jugadores++;
        }
        else
        {
          sprintf(text1, "Sorry, the game lobby is full.\n");
          sendto(sfd, text1, strlen(text1), 0, (struct sockaddr *)&(list[message.chat_id].address), sizeof(struct sockaddr_in));
        }
        printf("Type:[%d], Part:[%d], [%s] Started the Game\n\n", message.data_type, message.chat_id, list[message.chat_id].alias);
        if (esperar_jugadores == 0)
          sprintf(text1, "Player [%s] has started the Game, waiting for other players...\n", list[message.chat_id].alias);
        else
          sprintf(text1, "Player [%s] has joined the Game, waiting for other players...\n", list[message.chat_id].alias);
        esperar_jugadores++;
      }
      else
      {
        sprintf(text1, "[%s]:[%s]", list[message.chat_id].alias, message.data_text);
      }

      // El mensaje se replica para todos los participantes.
      for (i = 0; i < MAX_MEMBERS; ++i)
        if ((i != message.chat_id) && (list[i].chat_id != -1))
        {
          sendto(sfd, text1, strlen(text1), 0, (struct sockaddr *)&(list[i].address), sizeof(struct sockaddr_in));
        }

      // *************************************************************************
      // RECEPCIÓN DE MENSAJES DE JUEGO
      // *************************************************************************
      // Pedir cartas de la ronda a los jugadores
      if (game_started == true && round_deck_full == false)
      {
        // Recibir las cartas de los jugadores
        int num_carta = -1, mazo_id = -1, jugador_id = -1, numeros_escaneados = 0;
        bool permitir_carta = true;
        // Buscar en la pila de jugadores el numero de mazo del jugador
        for (i = 0; i < gameargs.contador_jugadores; i++)
        {
          if (gameargs.pila_jugadores[i].chat_id == message.chat_id)
          {
            // Encontramos al jugador
            jugador_id = gameargs.pila_jugadores[i].chat_id;
            mazo_id = gameargs.pila_jugadores[i].mazo_id;
            // Salir de la búsqueda
            break;
          }
        }
        // Verificar en las cartas de la mesa si ya existe una carta de dicho jugador
        for (i = 0; i < 5; i++)
        {
          if (gameargs.mazo_ronda.cartas[i].jugador_id == jugador_id)
          {
            // El jugador ya tiene una carta en la mesa, por lo que no debemos de permitir que meta otra
            permitir_carta = false;
          }
        }
        // Si no se encuentra al deck del jugador, marcar error
        if (mazo_id == -1)
        {
          printf("Error trying to find the player's deck.\n");
          permitir_carta = false;
        }
        if (permitir_carta == true)
        {
          // Obtener el numero de carta del mazo del jugador
          numeros_escaneados = sscanf(message.data_text, "%d", &num_carta);
          // Validar que se haya escaneado un número válido
          if (numeros_escaneados > 0 && num_carta > 0 && num_carta <= 5)
          {
            // Sacar la carta del mazo y ponerla en la mesa
            gameargs.mazo_ronda.cartas[gameargs.cont_cartas_ronda] = gameargs.mazo[mazo_id].cartas[num_carta - 1];
            printf("Card received from player [%d].", jugador_id);
            print_card(num_carta - 1, gameargs.mazo_ronda.cartas[gameargs.cont_cartas_ronda], false);
            gameargs.cont_cartas_ronda++;
            // Comprobar si ya todos los jugadores han puesto su carta
            if (gameargs.cont_cartas_ronda == gameargs.contador_jugadores)
            {
              round_deck_full = true;
            }
          }
          else
          {
            printf("Player sent an invalid type. Ignoring input...\n");
          }
        }
      }
    }

    if (message.data_type == 2)
    {
      // Receive a heartbeat
      // Update the heartbeat time
      *list[message.chat_id].heartbeat_time = clock() + TIMEOUT * CLOCKS_PER_SEC;
      printf("=== <3 === <3 === <3 === [%s] === <3 === <3 === <3 ===\n\n", list[message.chat_id].alias);
    }
    // *************************************************************************
    // EN ESTA SECCIÓN VERIFICAMOS SI EL JUEGO DEBE EMPEZAR O NO.
    // *************************************************************************
    // Cuando el límite del contador para iniciar el juego, se ha alcanzado..
    // Mostramos los nombres de los jugadores, sólo sí existen entre dos y cuatro jugadores.
    if (reloj_control == max_time)
    {
      reloj_control = 0;
      if (gameargs.contador_jugadores >= 2 && gameargs.contador_jugadores <= 4)
      {
        printf("Los jugadores de la partida son:\n");
        for (i = 0; i < 4; i++)
        {
          if (gameargs.pila_jugadores[i].chat_id != -1)
          {
            printf("%d): [%s]\n", (i + 1), list[gameargs.pila_jugadores[i].chat_id].alias);
            sprintf(text1, "%d): [%s]\n", (i + 1), list[gameargs.pila_jugadores[i].chat_id].alias);
          }
          // El mensaje se replica para todos los participantes.
          for (j = 0; j < MAX_MEMBERS; ++j)
            for (k = 0; k < 4; k++)
            {
              if ((gameargs.pila_jugadores[k].chat_id != -1) && (list[i].chat_id != -1) && (gameargs.pila_jugadores[k].chat_id == list[j].chat_id))
                sendto(sfd, text1, strlen(text1), 0, (struct sockaddr *)&(list[j].address), sizeof(struct sockaddr_in));
            }
        }
        // Setear las variables globales para que el hilo de juego comienze a trabajar.
        game_started = true;
        init_game = true;
        // Crear los argumentos del juego
        for (i = 0; i < MAX_MEMBERS; i++)
        {
          gameargs.list[i] = list[i];
        }
        gameargs.sfd = sfd;
        // Crear el hilo de juego
        ret_game = pthread_create(&thread2, NULL, game_thread, (void *)&gameargs);
        if (ret_hb)
          printf("Error creating heartbeat thread.\n");
        else
          printf("Heartbeat checker thread created.\n");
      }
      else
      {
        // En el caso de que hagan falta jugadores..
        sprintf(text1, "Faltan jugadores.\n");
        for (i = 0; i < gameargs.contador_jugadores; ++i)
          sendto(sfd, text1, strlen(text1), 0, (struct sockaddr *)&(list[gameargs.pila_jugadores[i].chat_id].address), sizeof(struct sockaddr_in));
        for (i = 0; i < 4; i++)
        {
          gameargs.pila_jugadores[i].chat_id = -1;
          gameargs.pila_jugadores[i].mazo_id = -1;
        }
        gameargs.contador_jugadores = 0;
      }
      // Una vez desplegado el nombre a todos los jugadores... validamos que existan al menos dos jugadores.
    }

    if (clean_player_list == true)
    {
      for (i = 0; i < 4; i++)
      {
        gameargs.pila_jugadores[i].chat_id = -1;
        gameargs.pila_jugadores[i].mazo_id = -1;
      }
      gameargs.contador_jugadores = 0;
      clean_player_list = false;
    }
  }
  shutdown(sfd, SHUT_RDWR);
  close(sfd);
  return (0);
}

//-------------------------------------------------------------------------------------------------------
// FUNCIONES PARA EL JUEGO DE CARTAS
//-------------------------------------------------------------------------------------------------------
// Con esta función validamos que existan entre 2 y 4 jugadores en el juego.
void timeout(int ignored)
{
  reloj_control = max_time;
}
