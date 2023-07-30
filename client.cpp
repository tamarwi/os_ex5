#include "client.h"
#include "globals.h"

#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <cstring>
#include <dirent.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int
count_servers (const std::string &client_files_directory, std::vector<live_server_info> &servers)
{
  servers.clear (); // Clear the servers vector before populating it with new data

  // Read the files in the specified directory
  DIR *dir = opendir (client_files_directory.c_str ());
  if (dir == NULL)
    {
      std::cerr << "system error: Failed to open directory: "
                << client_files_directory << "\n";
      return 0;
    }

  struct dirent *entry;
  std::vector<std::string> files;

  while ((entry = readdir (dir)) != NULL)
    {
      std::string file_name = entry->d_name;
      if (file_name != "." && file_name != "..")
        {
          files.push_back (file_name);
        }
    }

  closedir (dir);

  // Sort the file names alphabetically
  std::sort (files.begin (), files.end ());

  // Connect to servers and populate the servers vector
  int num_servers = 0;

  for (const auto &file: files)
    {
      std::string info_file_path = client_files_directory + "/" + file;
      std::ifstream info_file (info_file_path);

      if (!info_file.is_open ())
        {
          std::cerr << "system error:Failed to open file: " << info_file_path
                    << "\n";
          continue;
        }

      live_server_info server;
      server.info_file_path = info_file_path;
      server.server_fd = -1;
      server.client_fd = -1;
      server.shmid = -1;

      std::string line;
      int line_count = 0;

      // Connect to the socket
      struct sockaddr_in server_addr;
      memset (&server_addr, 0, sizeof (server_addr));
      server_addr.sin_family = AF_INET;
      //get ip
      std::getline (info_file, line);
      inet_aton (line.c_str (), &(server_addr.sin_addr));

      //get port
      std::getline (info_file, line);
      server_addr.sin_port = htons (std::stoi (line));

      server.client_fd = socket (AF_INET, SOCK_STREAM, 0);
      if (server.client_fd != -1)
        {
          struct timeval timeout;
          timeout.tv_sec = 5;
          timeout.tv_usec = 0;

          fd_set writefds;
          FD_ZERO(&writefds);
          FD_SET(server.client_fd, &writefds);

          int activity = select (server.client_fd + 1, NULL,
                                 &writefds, NULL, &timeout);
          if (activity != -1 && FD_ISSET(server.client_fd, &writefds))
            {
              if (connect (server.client_fd, (struct sockaddr *) &server_addr, sizeof (server_addr))
                  == -1)
                {
                  close (server.client_fd);
                  server.client_fd = -1;
                }
            }
          else
            {
              close (server.client_fd);
              server.client_fd = -1;
            }
        }

      // Check if shared memory segment exists
      std::string path;
      std::getline (info_file, path);
      //token shared memory
      std::getline (info_file, line);

      key_t key = ftok (path.c_str (), std::stoi (line));
      if (key == -1)
        {
          std::cerr << "system error: ftok failed in client" << std::endl;
        }
      if (-1 == (server.shmid = shmget (key, SHARED_MEMORY_SIZE, 0666)))
        {
          std::cerr << "system error: shmget failed in client" << std::endl;
        }
      if (server.client_fd != -1 || server.shmid != -1)
        {
          servers.push_back (server);
          num_servers++;
        }
      info_file.close ();
    }
  return num_servers;
}

void print_server_infos (const std::vector<live_server_info> &servers)
{
  int total_servers = servers.size ();
  int total_running_on_host = 0;
  int total_running_on_container = 0;
  int total_running_on_vm = 0;

  for (const auto &server: servers)
    {
      if (server.shmid != -1)
        {
          std::string msg;
          get_message_from_shm (server, msg);
          std::cout << "Shared Memory: " << msg << "\n";

          if (msg == "Host")
            {
              total_running_on_host++;
            }
          else if (msg == "Container")
            {
              total_running_on_container++;
            }
          else if (msg == "VM")
            {
              total_running_on_vm++;
            }
        }

      if (server.client_fd != -1)
        {
          std::string msg;
          get_message_from_socket (server, msg);
          std::cout << "Socket: " << msg << "\n";

          if (msg == "Host")
            {
              total_running_on_host++;
            }
          else if (msg == "Container")
            {
              total_running_on_container++;
            }
          else if (msg == "VM")
            {
              total_running_on_vm++;
            }
        }
    }

  std::cout << "Total Servers: " << total_servers << "\n";
  std::cout << "Host: " << total_running_on_host << "\n";
  std::cout << "Container: " << total_running_on_container << "\n";
  std::cout << "VM: " << total_running_on_vm << "\n";
}

void
get_message_from_socket (const live_server_info &server, std::string &msg)
{
  char buffer[1024];
  memset (buffer, 0, sizeof (buffer));
  int bytes_received = recv (server.client_fd, buffer,
                             sizeof (buffer) - 1, 0);

  if (bytes_received == -1)
    {
      std::cerr << "system error: Error in receiving message from socket\n";
      return;
    }

  msg = std::string (buffer);
}

void get_message_from_shm (const live_server_info &server, std::string &msg)
{
  char *shared_memory = (char *) shmat (server.shmid, NULL, 0);
  if (shared_memory == (char *) -1)
    {
      std::cerr
          << "system error: Error in attaching to shared memory segment\n";
      return;
    }

  msg = std::string (shared_memory);
  shmdt (shared_memory);
}

void disconnect (const std::vector<live_server_info> &servers)
{
  for (const auto &server: servers)
    {
      if (server.client_fd != -1)
        {
          close (server.client_fd);
        }
    }
}

void run (const std::string &client_files_directory)
{
  std::vector<live_server_info> servers;
  int num_servers = count_servers (client_files_directory, servers);

  if (num_servers == 0)
    {
      std::cout << "No servers found in the directory: "
                << client_files_directory << "\n";
      return;
    }

  print_server_infos (servers);
  disconnect (servers);
}
