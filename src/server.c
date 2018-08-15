/**
 * webserver.c -- A webserver written in C
 * 
 * Test with curl (if you don't have it, install it):
 * 
 *    curl -D - http://localhost:3490/
 *    curl -D - http://localhost:3490/d20
 *    curl -D - http://localhost:3490/date
 * 
 * You can also test the above URLs in your browser! They should work!
 * 
 * Posting Data:
 * 
 *    curl -D - -X POST -H 'Content-Type: text/plain' -d 'Hello, sample data!' http://localhost:3490/save
 * 
 * (Posting data is harder to test from a browser.)
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <sys/file.h>
#include <fcntl.h>

#define PORT "3490"  // the port users will be connecting to

#define BACKLOG 10	 // how many pending connections queue will hold

/**
 * Handle SIGCHILD signal
 *
 * We get this signal when a child process dies. This function wait()s for
 * Zombie processes.
 *
 * This is only necessary if we've implemented a multiprocessed version with
 * fork().
 */
void sigchld_handler(int s) {
  (void)s; // quiet unused variable warning

  // waitpid() might overwrite errno, so we save and restore it:
  int saved_errno = errno;

  // Wait for all children that have died, discard the exit status
  while(waitpid(-1, NULL, WNOHANG) > 0);

  errno = saved_errno;
}

/**
 * Set up a signal handler that listens for child processes to die so
 * they can be reaped with wait()
 *
 * Whenever a child process dies, the parent process gets signal
 * SIGCHLD; the handler sigchld_handler() takes care of wait()ing.
 * 
 * This is only necessary if we've implemented a multiprocessed version with
 * fork().
 */
void start_reaper(void)
{
  struct sigaction sa;

  sa.sa_handler = sigchld_handler; // Reap all dead processes
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART; // Restart signal handler if interrupted
  if (sigaction(SIGCHLD, &sa, NULL) == -1) {
    perror("sigaction");
    exit(1);
  }
}

/**
 * This gets an Internet address, either IPv4 or IPv6
 *
 * Helper function to make printing easier.
 */
void *get_in_addr(struct sockaddr *sa)
{
  if (sa->sa_family == AF_INET) {
    return &(((struct sockaddr_in*)sa)->sin_addr);
  }

  return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

/**
 * Return the main listening socket
 *
 * Returns -1 or error
 */
int get_listener_socket(char *port)
{
  int sockfd;
  struct addrinfo hints, *servinfo, *p;
  int yes=1;
  int rv;

  // This block of code looks at the local network interfaces and
  // tries to find some that match our requirements (namely either
  // IPv4 or IPv6 (AF_UNSPEC) and TCP (SOCK_STREAM) and use any IP on
  // this machine (AI_PASSIVE).

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE; // use my IP

  if ((rv = getaddrinfo(NULL, port, &hints, &servinfo)) != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    return -1;
  }

  // Once we have a list of potential interfaces, loop through them
  // and try to set up a socket on each. Quit looping the first time
  // we have success.
  for(p = servinfo; p != NULL; p = p->ai_next) {

    // Try to make a socket based on this candidate interface
    if ((sockfd = socket(p->ai_family, p->ai_socktype,
        p->ai_protocol)) == -1) {
      //perror("server: socket");
      continue;
    }

    // SO_REUSEADDR prevents the "address already in use" errors
    // that commonly come up when testing servers.
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
        sizeof(int)) == -1) {
      perror("setsockopt");
      close(sockfd);
      freeaddrinfo(servinfo); // all done with this structure
      return -2;
    }

    /* OLD WAY OF BINDING WOULD BE LIKE:
    int sockfd;
    struct sockaddr_in my_addr;

    sockfd = socket(PF_INET, SOCK_STREAM, 0);
    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(MYPORT);
    my_addr.sin_addr.s_addr = inet_addr(INADDR_ANY);

    memset(my_addr.sin_zero, '\0', sizeof my_addr.sin_zero);

    bind(sockfd, (struct sockaddr *) &my_addr, sizeof my_addr);
    
    // See if we can bind this socket to this local IP address. This
    // associates the file descriptor (the socket descriptor) that
    // we will read and write on with a specific IP address.
    */
    if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
      close(sockfd);
      //perror("server: bind");
      continue;
    }

    // If we got here, we got a bound socket and we're done
    break;
  }

  freeaddrinfo(servinfo); // all done with this structure

  // If p is NULL, it means we didn't break out of the loop, above,
  // and we don't have a good socket.
  if (p == NULL)  {
    fprintf(stderr, "webserver: failed to find local address\n");
    return -3;
  }

  // Start listening. This is what allows remote computers to connect
  // to this socket/IP.
  if (listen(sockfd, BACKLOG) == -1) {
    //perror("listen");
    close(sockfd);
    return -4;
  }

  return sockfd;
}

/**
 * Send an HTTP response
 *
 * header:       "HTTP/1.1 404 NOT FOUND" or "HTTP/1.1 200 OK", etc.
 * content_type: "text/plain", etc.
 * body:         the data to send.
 * 
 * Return the value from the send() function.
 */
int send_response(int fd, char *header, char *content_type, char *body)
{
  const int max_response_size = 65536;
  char response[max_response_size];
  int response_length; // Total length of header plus body

  // !!!!  IMPLEMENT ME
  time_t raw_format;
  time(&raw_format);

  int content_length = strlen(body);
  response_length = sprintf(response,
    "%s\n"
    "Date: %s"
    "Connection: close\n"
    "Connection-Length: %d\n"
    "Content-Type: %s\n"
    "\n"
    "%s",
    header,
    asctime(localtime(&raw_format)),
    content_length,
    content_type,
    body);

  // Send it all!
  int rv = send(fd, response, response_length, 0);

  if (rv < 0) {
    perror("send");
  }

  return rv;
}


/**
 * Send a 404 response
 */
void resp_404(int fd)
{
  send_response(fd, "HTTP/1.1 404 NOT FOUND", "text/html", "<h1>404 Page Not Found</h1>");
}

/**
 * Send a / endpoint response
 */
void get_root(int fd)
{
  // !!!! IMPLEMENT ME
  //send_response(...
  send_response(fd, "HTTP/1.1 200 OK", "text/html", "<!DOCTYPE html><html><head><title>Hello, World!</title></head><body><center><h1>Hello, World!</h1></center></body></html></>");
}

/**
 * Send a /d20 endpoint response
 */
void get_d20(int fd)
{
  // !!!! IMPLEMENT ME
  srand(time(NULL));

  char str[4];

  int random = rand() % 20 + 1;
  sprintf(str, "%d\n", random);

  send_response(fd, "HTTP/1.1 200 OK", "text/plain", str);
}

/**
 * Send a /date endpoint response
 */
void get_date(int fd)
{
  // !!!! IMPLEMENT ME
  time_t gmt_format;
  time(&gmt_format);
  char current[26];  // gmtime documentation stated that a user-supplied buffer should have at least 26 bytes.
  sprintf(current, "%s", asctime(gmtime(&gmt_format)));
  send_response(fd, "HTTP/1.1 200 OK", "text/plain", current);
  
}

/**
 * Post /save endpoint data
 */
void post_save(int fd, char *body)
{
char *status;

  // !!!! IMPLEMENT ME  
  int file = open("data.txt", O_CREAT|O_WRONLY|O_APPEND, 0644);

  char buffer[1024];
  int size = sprintf(buffer, "%s", body);

  // lseek(file, SEEK_SET, SEEK_DATA);

  // int bytes_written = write(file, buffer, size);
  // if (bytes_written < 0)
  // {
  //   perror("write");
  // }
  if (file < 0) {
    status = "failed";
  } else {
    write(file, body, size);
    close(file);
    status = "ok";
  }
  
  char response_body[128];
  sprintf(response_body, "{\"status\": \"%s\"}\n", status);

  send_response(fd, "HTTP/1.1 200 OK", "application/json", response_body);
  // Save the body and send a response
}

/**
 * Search for the start of the HTTP body.
 *
 * The body is after the header, separated from it by a blank line (two newlines
 * in a row).
 *
 * "Newlines" in HTTP can be \r\n (carriage return followed by newline) or \n
 * (newline) or \r (carriage return).
 */
char *find_start_of_body(char *header)
{
  char *start;
  
  if ((start = strstr(header, "\r\n\r\n")) != NULL)
  {
    return start;
  }

  else if ((start = strstr(header, "\n\n")) != NULL)
  {
    return start;
  }
  else if ((start = strstr(header, "\r\r")) != NULL)
  {
    return start;
  }
  else
  {
    start = strstr(header, "\r\n\r\n");
    return start;
  }
}

/**
 * Handle HTTP request and send response
 */
void handle_http_request(int fd)
{
  const int request_buffer_size = 65536; // 64K
  char request[request_buffer_size];
  char *p;
  char request_type[8]; // GET or POST
  char request_path[1024]; // /info etc.
  char request_protocol[128]; // HTTP/1.1

  // Read request
  int bytes_recvd = recv(fd, request, request_buffer_size - 1, 0);

  if (bytes_recvd < 0) {
    perror("recv");
    return;
  }

   // NUL terminate request string
  request[bytes_recvd] = '\0';

  // !!!! IMPLEMENT ME
  // Get the request type and path from the first line
  // Hint: sscanf()!
  sscanf(request, "%s %s %s", request_type, request_path, request_protocol);
  printf("Request: %s %s %s From: %d\n", request_type, request_path, request_protocol, getpid());
  
  // !!!! IMPLEMENT ME (stretch goal)
  
  p = find_start_of_body(request);
  char *body = p;

  // !!!! IMPLEMENT ME
  // call the appropriate handler functions, above, with the incoming data  
  if (strcmp(request_type, "GET") == 0)
  {
    if (strcmp(request_path, "/") == 0)
    {
      get_root(fd);
    }
    else if (strcmp(request_path, "/d20") == 0)
    {
      get_d20(fd);
    }
    else if (strcmp(request_path, "/date") == 0)
    {
      get_date(fd);
    }
    else
    {
      resp_404(fd);
    }
  }
  else if (strcmp(request_type, "POST") == 0)
  {
    if (strcmp(request_path, "/save") == 0)
    {
      post_save(fd, body);
    }
    else
    {
      resp_404(fd);
    }
  }
}

/**
 * Main
 */
int main(void)
{
  int newfd;  // listen on sock_fd, new connection on newfd
  struct sockaddr_storage their_addr; // connector's address information
  char s[INET6_ADDRSTRLEN];

  // Start reaping child processes
  start_reaper();

  // Get a listening socket
  int listenfd = get_listener_socket(PORT);

  if (listenfd < 0) {
    fprintf(stderr, "webserver: fatal error getting listening socket\n");
    exit(1);
  }

  printf("webserver: waiting for connections...\n");

  // This is the main loop that accepts incoming connections and
  // fork()s a handler process to take care of it. The main parent
  // process then goes back to waiting for new connections.
  
  while(1) {
    socklen_t sin_size = sizeof their_addr;

    // Parent process will block on the accept() call until someone
    // makes a new connection:
    newfd = accept(listenfd, (struct sockaddr *)&their_addr, &sin_size);
    if (newfd == -1) {
      perror("accept");
      continue;
    }

    // Print out a message that we got the connection
    inet_ntop(their_addr.ss_family,
      get_in_addr((struct sockaddr *)&their_addr),
      s, sizeof s);
    printf("server: got connection from %s\n", s);
    
    // newfd is a new socket descriptor for the new connection.
    // listenfd is still listening for new connections.

    // !!!! IMPLEMENT ME (stretch goal)
    // Convert this to be multiprocessed with fork()
   int rc = fork();
  //  printf("The process that handled this was %d\n", getpid());
   if (rc < 0)
   {
     fprintf(stderr, "Fork failed\n");
     close(newfd);
   }
   else if (rc == 0)
   {
     handle_http_request(newfd);
     exit(1);
   }
   else
   {
     wait(NULL);
     close(newfd);
     continue;
   }
  }

  // Unreachable code

  return 0;
}

