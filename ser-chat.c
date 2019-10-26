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

/* -------------------------------------------------------------------------- */
/* global definitions                                                         */
/* -------------------------------------------------------------------------- */
#define BUFFERSIZE 4096 /* buffer size                         */
#define MAX_MEMBERS 20  /* maximum number of members in room   */

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

void *check_heartbeats(void *arg)
{
  arguments *args = arg;
  char text1[BUFFERSIZE]; // reading buffer
  clock_t target_time;
  target_time = clock() + 5 * CLOCKS_PER_SEC;
  int i = 0;

  while (1)
  {
    for (i = 0; i < MAX_MEMBERS; i++)
    {
      if ((args->list[i].chat_id != -1) && (clock() > *args->list[i].heartbeat_time))
      {
        // Time exceeded for the heartbeat, log out the client.
        printf("Client [%s] is leaving the chat room due to inactivity.\n", args->list[i].alias);
        sprintf(text1, "Client [%s] is leaving the chat room due to inactivity.\n", args->list[i].alias);
        args->list[i].chat_id = -1;
        args->participants -= 1;

        for (i = 0; i < MAX_MEMBERS; ++i)
          if (args->list[i].chat_id != -1)
            sendto(args->sfd, text1, strlen(text1), 0, (struct sockaddr *)&(args->list[i].address), sizeof(struct sockaddr_in));
      }
    }
    // if (clock() > target_time)
    // {
    //   printf("5 secs.\n");
    //   target_time = clock() + 5 * CLOCKS_PER_SEC;
    //   for (int i = 0; i < MAX_MEMBERS; ++i)
    //   {
    //     if (clock() > *args->list[i].heartbeat_time)
    //     {
    //       printf("Client %d has exceeded maximum time.\n", i);
    //     }
    //     else
    //     {
    //       printf("Client %d with id %d is alive.\n", i, args->list[i].chat_id);
    //     }
    //   }
    // }
  }
}

/* -------------------------------------------------------------------------- */
/* main ()                                                                    */
/*                                                                            */
/* main function of the system                                                */
/* -------------------------------------------------------------------------- */
int main()
{
  struct sockaddr_in sock_read;    /* structure for the read socket       */
  struct sockaddr_in sock_write;   /* structure for the write socket      */
  struct data message;             /* message to sendto the server        */
  struct member list[MAX_MEMBERS]; /* list of members in room             */
  char text1[BUFFERSIZE];          /* reading buffer                      */
  char *client_address;            /* address of the sending client       */
  int participants;                /* number of participats in the chat   */
  int i, j;                        /* counters                            */
  int sfd;                         /* socket descriptor                   */
  int read_char;                   /* read characters                     */
  int ret_hb;                      // Heartbeat return
  socklen_t length;                /* size of the read socket             */
  pthread_t thread1;               // Thread ID

  /* ---------------------------------------------------------------------- */
  /* structure of the socket that will read what comes from the client      */
  /* ---------------------------------------------------------------------- */
  sock_read.sin_family = AF_INET;
  sock_read.sin_port = 17091;
  inet_aton("200.13.89.15", (struct in_addr *)&sock_read.sin_addr);
  memset(sock_read.sin_zero, 0, 8);

  /* ---------------------------------------------------------------------- */
  /* Instrucctions for publishing the socket                                */
  /* ---------------------------------------------------------------------- */
  sfd = socket(AF_INET, SOCK_DGRAM, 0);
  bind(sfd, (struct sockaddr *)&(sock_read), sizeof(sock_read));

  /* ---------------------------------------------------------------------- */
  /* Inicialization of several variables                                    */
  /* ---------------------------------------------------------------------- */
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

  // Creation of arguments for thread
  arguments *args = malloc(sizeof(arguments));
  struct member *list_ptr;
  list_ptr = list;
  args->list = list_ptr;
  args->participants = &participants;
  args->sfd = sfd;

  // Thread creation
  // Creation of the heartbeat thread
  ret_hb = pthread_create(&thread1, NULL, check_heartbeats, (void *)args);
  if (ret_hb)
  {
    printf("Error creating heartbeat thread.\n");
  }
  else
  {
    printf("Heartbeat checker thread created.\n");
  }

  /* ---------------------------------------------------------------------- */
  /* The socket is  read and the messages are  answered until the word exit */
  /* is received by the last member of the chat                             */
  /* ---------------------------------------------------------------------- */
  while (strcmp(message.data_text, "shutdown") != 0)
  {

    /* first we read the message sent from any client                     */
    read_char = recvfrom(sfd, (struct data *)&(message), sizeof(struct data), 0, (struct sockaddr *)&(sock_write), &(length));
    printf("Type:[%d], Part:[%d], Mess:[%s]\n", message.data_type, message.chat_id, message.data_text);

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
      printf("Participants in Room:[%d]\n", participants);
      printf("Value of i:[%d]\n", i);

      // Send i to client
      sendto(sfd, (void *)&(i), sizeof(int), 0, (struct sockaddr *)&(sock_write), sizeof(struct sockaddr));

      /* We notify the entrance to the chat of the new participant      */
      /* Variable i still keeps the number of the last member added     */
      sprintf(text1, "Participante [%s] se ha unido al grupo.\n", message.data_text);
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
      else
      {
        sprintf(text1, "[%s]:[%s]", list[message.chat_id].alias, message.data_text);
      }
      for (i = 0; i < MAX_MEMBERS; ++i)
        if ((i != message.chat_id) && (list[i].chat_id != -1))
          sendto(sfd, text1, strlen(text1), 0, (struct sockaddr *)&(list[i].address), sizeof(struct sockaddr_in));
    }

    if (message.data_type == 2)
    {
      // Receive a heartbeat

      // Update the heartbeat time
      *list[message.chat_id].heartbeat_time = clock() + 30 * CLOCKS_PER_SEC;
      printf("========= <3 =========\n");
      printf("Updated client [%s] heartbeat.\n", list[message.chat_id].alias);
      printf("\n");
    }
  }
  shutdown(sfd, SHUT_RDWR);
  close(sfd);
  return (0);
}
