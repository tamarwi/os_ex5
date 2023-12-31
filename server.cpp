#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include "globals.h"
#include <arpa/inet.h>
#include <fstream>
#include <iostream>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <errno.h>

#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <cstring>
#include <arpa/inet.h>
#include <netdb.h>

void
start_communication (const server_setup_information &setup_info, live_server_info &server)
{
  int server_fd;
  int shmid;
  struct sockaddr_in server_addr;

  // Create a socket
  server_fd = socket (AF_INET, SOCK_STREAM, 0);
  if (server_fd == -1)
    {
      std::cerr << "system error: Socket creation failed." << std::endl;
      exit (EXIT_FAILURE);
    }

  // Prepare the server address structure
  memset (&server_addr, 0, sizeof (server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = htonl (INADDR_ANY);
  server_addr.sin_port = htons (setup_info.port);

  // Bind the socket to the IP and port
  if (bind (server_fd, (struct sockaddr *) &server_addr, sizeof (server_addr))
      == -1)
    {
      std::cerr << "system error: Socket bind failed in server" << std::endl;
      close (server_fd);
      exit (EXIT_FAILURE);
    }

  // Listen for incoming connections
  if (listen (server_fd, 5) == -1)
    {
      std::cerr << "system error: Socket listen failed in server" << std::endl;
      close (server_fd);
      exit (EXIT_FAILURE);
    }

  // Allocate shared memory
  key_t key = ftok (setup_info.shm_pathname.c_str (), setup_info.shm_proj_id);
  if (key == -1)
    {
      std::cerr << "system error: ftok failed in server" << std::endl;
      close (server_fd);
      exit (EXIT_FAILURE);
    }

  shmid = shmget (key, SHARED_MEMORY_SIZE, IPC_CREAT | 0666);
  if (shmid == -1)
    {
      std::cerr << "system error: shmget failed in server" << std::endl;
      close (server_fd);
      exit (EXIT_FAILURE);
    }

  // Fill the live_server_info struct
  server.server_fd = server_fd;
  server.shmid = shmid;
}

void
create_info_file (const server_setup_information &setup_info, live_server_info &server)
{
  std::string info_file_path =
      setup_info.info_file_directory + "/" + setup_info.info_file_name;
  std::ofstream info_file (info_file_path);
  if (!info_file.is_open ())
    {
      std::cerr << "system error: Failed to create info file in server" << std::endl;
      exit (EXIT_FAILURE);
    }

  //get host ip
  char myname[MAXHOSTNAME + 1];
  struct hostent *hp;

  //hostnet initialization
  gethostname (myname, MAXHOSTNAME);
  if ((hp = gethostbyname (myname)) == NULL)
    {
      std::cerr << "system error: Failed to get host name" << std::endl;
      close (server.server_fd);
      exit (EXIT_FAILURE);
    }

  char ip_str[INET_ADDRSTRLEN];
  inet_ntop (AF_INET, hp->h_addr_list[0], ip_str, INET_ADDRSTRLEN);

  info_file << ip_str << "\n";
  info_file << setup_info.port << "\n";
  info_file << setup_info.shm_pathname << "\n";
  info_file << setup_info.shm_proj_id << "\n";
  info_file.close ();

  server.info_file_path = info_file_path;
}

void get_connection (live_server_info &server)
{
  struct timeval timeout;
  timeout.tv_sec = 120;
  timeout.tv_usec = 0;

  fd_set readfds;
  FD_ZERO(&readfds);
  FD_SET(server.server_fd, &readfds);

  int activity = select (server.server_fd + 1, &readfds, NULL, NULL, &timeout);
  if (activity == -1)
    {
      std::cerr << "system error: Select failed in server" << std::endl;
      server.client_fd = -1;
    }
  else if (activity == 0)
    {
      server.client_fd = -1; // Timeout occurred, no connection
    }
  else
    {
      struct sockaddr_in client_addr;
      socklen_t addr_len = sizeof (client_addr);
      server.client_fd = accept (server.server_fd, (struct sockaddr *) &client_addr, &addr_len);
      if (server.client_fd == -1)
        {
          std::cerr << "system error: Accept failed in server" << std::endl;
        }
    }
}

void write_to_socket (const live_server_info &server, const std::string &msg)
{
  if (server.client_fd != -1)
    {
      send (server.client_fd, msg.c_str (), msg.size (), 0);
    }
}

void write_to_shm (const live_server_info &server, const std::string &msg)
{
  char *shared_memory = (char *) shmat (server.shmid, NULL, 0);
  if (shared_memory == (char *) -1)
    {
      std::cerr << "system error: shmat failed in server" << std::endl;
      return;
    }

  strcpy (shared_memory, msg.c_str ());
  shmdt (shared_memory);
}

void shutdown (const live_server_info &server)
{
  close (server.server_fd);
  if (server.client_fd != -1)
    {
      close (server.client_fd);
    }
  shmctl (server.shmid, IPC_RMID, NULL);
  remove (server.info_file_path.c_str ());
}

void
run (const server_setup_information &setup_info, const std::string &shm_msg, const std::string &socket_msg)
{
  live_server_info server;
  start_communication (setup_info, server);
  server.client_fd = -1;

  create_info_file (setup_info, server);
  write_to_shm (server, shm_msg);

  get_connection (server);
  write_to_socket (server, socket_msg);

  // Sleep for 10 seconds
  sleep (10);

  shutdown (server);
}


