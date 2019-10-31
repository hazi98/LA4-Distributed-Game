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
const char *ip = "127.0.0.1";
const int max_time = 10;

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
int reloj_control;                      // Solo puede existir un contador para iniciar la partida.
volatile bool game_started = false;     // Bandera para controlar el estado del juego.
volatile bool init_game = false;        // Bandera para indicar si se tiene que inicializar el juego.
volatile bool start_next_round = false; // Bandera para saber si el hilo de control de partida debe de iniciar la siguiente ronda.
volatile bool round_deck_full = false;  // Bandera para esperar a que todos los jugadores pongan una carta en la mesa.
volatile bool end_game = false;         // Bandera para indicar al hilo de juego que termine su ejecución.

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
  printf("Carta [%d]: %d %s\n", (i + 1), (carta.numero + 1), simbolo);
  if (text1 != false)
    sprintf(text1, "Carta [%d]: %d %s\n", (i + 1), (carta.numero + 1), simbolo);
}

void game_round(game_args *args)
{
  // Variables
  int i, j, origen, destino, contador_cartas;
  char text1[BUFFERSIZE]; // reading buffer
  cartas carta_aux;
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
      int id_actual = args->list[args->pila_jugadores[i].chat_id].chat_id;
      // Limpiar el buffer de texto
      memset(text_aux, 0, sizeof text_aux);
      printf("Deck %d\n", i);
      // Copiar en el buffer el siguiente texto
      sprintf(text_aux, "Deck %d\n", i);
      strcat(text_mazo, text_aux);
      for (j = 0; j < 5; j++)
      {
        // Saltar la carta si ya está usada
        while (args->baraja[contador_cartas].jugador_id != -1)
          contador_cartas++;
        // Asignar el id de la carta al jugador actual
        args->baraja[contador_cartas].jugador_id = id_actual;
        // Poner la carta en el mazo del jugador
        args->mazo[i].cartas[contador_cartas] = args->baraja[contador_cartas];
        // Limpiar el texto auxiliar
        memset(text_aux, 0, sizeof text_aux);
        // Imprimir la carta en el texto auxiliar
        print_card(j, args->mazo[i].cartas[contador_cartas], text_aux);
        // Concatenar el texto auxiliar con el buffer de texto
        strcat(text_mazo, text_aux);
        // Aumentar la cuenta de cartas
        contador_cartas++;
      }
      // Asignar el mazo ID al jugador
      args->pila_jugadores[i].mazo_id = i;
      // Enviar el mazo al jugador
      sendto(args->sfd, text_mazo, strlen(text_mazo), 0, (struct sockaddr *)&(args->list[i].address), sizeof(struct sockaddr_in));
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
  char text1[BUFFERSIZE]; // write buffer

  // Loop principal del juego
  do
  {
    // 1. Iniciar el juego si se da la indicación e inicializar la baraja
    if (game_started == true && init_game == true)
    {
      // Inicializacion de la baraja
      int contador_cartas, i, j;
      // Asignar 13 numeros a cada palo
      contador_cartas = 0;
      for (i = 0; i < 4; i++)
      {
        for (j = 0; j < 13; j++)
        {
          args->baraja[contador_cartas].numero = j;
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
      sprintf(text1, "Round %d winner is player [%d]\n", (args->num_rondas + 1), jugador_id);
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
          // Si la carta en la baraja ya tiene ID, hay que quitarla
          if (args->baraja[i].jugador_id != -1)
          {
            printf("Removing card: ");
            print_card(i, args->baraja[i], false);
            for (j = i; j < 52; j++)
            {
              args->baraja[j] = args->baraja[j + 1];
            }
            // Ir una carta atrás
            i--;
          }
        }
      } while (cartas_eliminadas == false);
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
      printf("Game winner is player [%d], congrats!\n", res);
      sprintf(text1, "Game winner is player [%d], congrats!\n", res);
      for (i = 0; i < args->contador_jugadores; ++i)
        sendto(args->sfd, text1, strlen(text1), 0, (struct sockaddr *)&(args->list[args->pila_jugadores[i].chat_id].address), sizeof(struct sockaddr_in));

      // Marcar el fin del juego
      end_game = true;
    }

    // 5. Terminar el juego
    if (game_started == true && end_game == true)
    {
      // Finalizar el hilo
      printf("Game thread shutting down\n");
      game_started = false;
      exit(0);
    }
  } while (end_game == false);
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
  pthread_t thread2;             // Game thread ID
  int cant_cartas_mismo_num[13]; // Solo pueden existir 4 cartas con un número dado, p.e. 4 quinas.
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
    gameargs.baraja[i].numero = 0;
    gameargs.baraja[i].palo = 0;
    gameargs.baraja[i].jugador_id = -1;
  }
  for (i = 0; i < 13; i++)
  {
    cant_cartas_mismo_num[i] = 0;
  }

  length = sizeof(struct sockaddr);
  message.data_text[0] = '\0';
  participants = 0;
  for (i = 0; i < MAX_MEMBERS; ++i)
  {
    list[i].chat_id = -1;
    // Create the dynamic clock
    list[i].heartbeat_time = malloc(sizeof(clock_t));
    *list[i].heartbeat_time = clock() + 30 * CLOCKS_PER_SEC;
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
        *list[i].heartbeat_time = clock() + 30 * CLOCKS_PER_SEC;
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
          sendto(sfd, text1, strlen(text1), 0, (struct sockaddr *)&(list[i].address), sizeof(struct sockaddr_in));

      // *************************************************************************
      // RECEPCIÓN DE MENSAJES DE JUEGO
      // *************************************************************************
      // Pedir cartas de la ronda a los jugadores
      if (game_started == true && round_deck_full == false)
      {
        // Recibir las cartas de los jugadores
        int num_carta, mazo_id = -1, jugador_id = -1;
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
        // Si no se encuentra al jugador, marcar error
        if (mazo_id == -1)
        {
          printf("Error trying to find the player's deck.\n");
          permitir_carta = false;
        }
        if (permitir_carta == true)
        {
          // Obtener el numero de carta del mazo del jugador
          sscanf(message.data_text, "%d", &num_carta);
          // Sacar la carta del mazo y ponerla en la mesa
          gameargs.mazo_ronda.cartas[gameargs.cont_cartas_ronda++] = gameargs.mazo[mazo_id].cartas[num_carta];
          printf("Card received from player [%d].", jugador_id);
          print_card(num_carta, gameargs.mazo_ronda.cartas[gameargs.cont_cartas_ronda], false);
          // Comprobar si ya todos los jugadores han puesto su carta
          if (gameargs.cont_cartas_ronda == gameargs.contador_jugadores)
          {
            round_deck_full = true;
          }
        }
      }
    }

    if (message.data_type == 2)
    {
      // Receive a heartbeat
      // Update the heartbeat time
      *list[message.chat_id].heartbeat_time = clock() + 30 * CLOCKS_PER_SEC;
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
