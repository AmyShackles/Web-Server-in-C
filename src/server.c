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
 *    curl -D - -X POST -H 'Content-Type: text/plain' -d 'Hello, sample data!'
 * http://localhost:3490/save
 *
 * (Posting data is harder to test from a browser.)
 */

#include "cache.h"
#include "file.h"
#include "mime.h"
#include "net.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define PORT "3490" // the port users will be connecting to
#define SERVER_FILES "./serverfiles"
#define SERVER_ROOT "./serverroot"
// /**
//  * Handle SIGCHILD signal
//  *
//  * We get this signal when a child process dies. This function wait()s for
//  * Zombie processes.
//  *
//  * This is only necessary if we've implemented a multiprocessed version with
//  * fork().
//  */
// void sigchld_handler(int s) {
//   (void)s; // quiet unused variable warning

//   // waitpid() might overwrite errno, so we save and restore it:
//   int saved_errno = errno;

//   // Wait for all children that have died, discard the exit status
//   while (waitpid(-1, NULL, WNOHANG) > 0)
//     ;

//   errno = saved_errno;
// }

// /**
//  * Set up a signal handler that listens for child processes to die so
//  * they can be reaped with wait()
//  *
//  * Whenever a child process dies, the parent process gets signal
//  * SIGCHLD; the handler sigchld_handler() takes care of wait()ing.
//  *
//  * This is only necessary if we've implemented a multiprocessed version with
//  * fork().
//  */
// void start_reaper(void) {
//   struct sigaction sa;

//   sa.sa_handler = sigchld_handler; // Reap all dead processes
//   sigemptyset(&sa.sa_mask);
//   sa.sa_flags = SA_RESTART; // Restart signal handler if interrupted
//   if (sigaction(SIGCHLD, &sa, NULL) == -1) {
//     perror("sigaction");
//     exit(1);
//   }
// }

/**
 * Send an HTTP response
 *
 * header:       "HTTP/1.1 404 NOT FOUND" or "HTTP/1.1 200 OK", etc.
 * content_type: "text/plain", etc.
 * body:         the data to send.
 *
 * Return the value from the send() function.
 */
int send_response(int fd, char *header, char *content_type, void *body,
                  int content_length) {
  const int max_response_size = 65536;
  char response[max_response_size];

  // !!!!  IMPLEMENT ME
  time_t rawtime;
  struct tm *info;

  info = localtime(&rawtime);

  int response_length =
      sprintf(response,
              "%s\n"
              "Date: %s"
              "Connection: close\n"
              "Content-Length: %d\n"
              "Content-Type: %s\n"
              "\n",
              header, asctime(info), content_length, content_type);
  memcpy(response + response_length, body, content_length);
  // Send it all!
  int rv = send(fd, response, response_length + content_length, 0);

  if (rv < 0) {
    perror("send");
  }

  return rv;
}

/**
 * Send a 404 response
 */
void resp_404(int fd) {
  char filepath[4096];
  struct file_data *filedata;
  char *mime_type;

  // Fetch the 404.html file
  snprintf(filepath, sizeof filepath, "%s/404.html", SERVER_FILES);
  filedata = file_load(filepath);

  if (filedata == NULL) {
    fprintf(stderr, "Cannot find system 404 file\n");
    exit(3);
  }

  mime_type = mime_type_get(filepath);

  send_response(fd, "HTTP/1.1 404 NOT FOUND", mime_type, filedata->data,
                filedata->size);

  file_free(filedata);
}

/**
 * Send a /d20 endpoint response
 */
void get_d20(int fd) {
  // !!!! IMPLEMENT ME
  srand(time(NULL) + getpid());

  char str[8];

  int random = rand() % 20 + 1;
  int length = sprintf(str, "%d\n", random);

  send_response(fd, "HTTP/1.1 200 OK", "text/plain", str, length);
}

/**
 * Send a /date endpoint response
 */
// void get_date(int fd) {
//   // !!!! IMPLEMENT ME
//   time_t gmt_format;
//   time(&gmt_format);
//   char current[26]; // gmtime documentation stated that a user-supplied
//   buffer
//                     // should have at least 26 bytes.
//   int length = sprintf(current, "%s", asctime(gmtime(&gmt_format)));
//   send_response(fd, "HTTP/1.1 200 OK", "text/plain", current, length);
// }

/**
 * Post /save endpoint data
 */
void post_save(int fd, char *body) {
  char *status;

  // !!!! IMPLEMENT ME
  int file = open("data.txt", O_CREAT | O_WRONLY | O_APPEND, 0644);

  // lseek(file, SEEK_SET, SEEK_DATA);

  // int bytes_written = write(file, buffer, size);
  // if (bytes_written < 0)
  // {
  //   perror("write");
  // }
  if (file < 0) {
    status = "failed";
  } else {
    flock(file, LOCK_EX);
    write(file, body, strlen(body));
    flock(file, LOCK_UN);
    close(file);
    status = "ok";
  }

  char response_body[128];
  int length = sprintf(response_body, "{\"status\": \"%s\"}\n", status);

  send_response(fd, "HTTP/1.1 200 OK", "application/json", response_body,
                length);
  // Save the body and send a response
}

int get_file_or_cache(int fd, struct cache *cache, char *filepath) {
  struct file_data *filedata;
  struct cache_entry *cacheent;
  char *mime_type;

  cacheent = cache_get(cache, filepath);

  if (cacheent != NULL) {
    send_response(fd, "HTTP/1.1 200 OK", cacheent->content_type,
                  cacheent->content, cacheent->content_length);
  } else {
    filedata = file_load(filepath);
    if (filedata == NULL) {
      return -1;
    }

    mime_type = mime_type_get(filepath);
    send_response(fd, "HTTP/1.1 200 OK", mime_type, filedata->data,
                  filedata->size);

    cache_put(cache, filepath, mime_type, filedata->data, filedata->size);

    file_free(filedata);
  }

  return 0;
}

void get_file(int fd, struct cache *cache, char *request_path) {
  char filepath[65536];
  struct file_data *filedata;
  char *mime_type;

  //     // Try to find the file
  snprintf(filepath, sizeof(filepath), "%s%s", SERVER_ROOT, request_path);
  filedata = file_load(filepath);

  if (filedata == NULL) {
    snprintf(filepath, sizeof filepath, "%s%s/index.html", SERVER_ROOT,
             request_path);
    filedata = file_load(filepath);

    if (filedata == NULL) {
      resp_404(fd);
      return;
    }
  }

  mime_type = mime_type_get(filepath);
  send_response(fd, "HTTP/1.1 200 OK", mime_type, filedata->data,
                filedata->size);

  file_free(filedata);
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
char *find_start_of_body(char *header) {
  char *start;

  if ((start = strstr(header, "\r\n\r\n")) != NULL) {
    return start + 2;
  } else if ((start = strstr(header, "\n\n")) != NULL) {
    return start + 2;
  } else if ((start = strstr(header, "\r\r")) != NULL) {
    return start + 2;
  } else {
    return start;
  }
}

/**
 * Handle HTTP request and send response
 */
void handle_http_request(int fd, struct cache *cache) {
  const int request_buffer_size = 65536; // 64K
  char request[request_buffer_size];
  char *p;
  char request_type[8];       // GET or POST
  char request_path[1024];    // /info etc.
  char request_protocol[128]; // HTTP/1.1

  // Read request
  int bytes_recvd = recv(fd, request, request_buffer_size - 1, 0);

  if (bytes_recvd < 0) {
    perror("recv");
    return;
  }

  // NUL terminate request string
  request[bytes_recvd] = '\0';

  p = find_start_of_body(request);

  char *body = p + 1;
  // !!!! IMPLEMENT ME
  // Get the request type and path from the first line
  // Hint: sscanf()!
  sscanf(request, "%s %s %s", request_type, request_path, request_protocol);
  printf("Request: %s %s %s\n", request_type, request_path, request_protocol);

  // !!!! IMPLEMENT ME (stretch goal)

  // !!!! IMPLEMENT ME
  // call the appropriate handler functions, above, with the incoming data
  if (strcmp(request_type, "GET") == 0) {
    if (strcmp(request_path, "/d20") == 0) {
      get_d20(fd);
    } else {
      get_file(fd, cache, request_path);
    }
  } else if (strcmp(request_type, "POST") == 0) {
    if (strcmp(request_path, "/save") == 0) {
      post_save(fd, body);
    } else {
      resp_404(fd);
    }
  } else {
    fprintf(stderr, "Unknown request type \"%s\"\n", request_type);
    return;
  }
}
char *get_in_addr(const struct sockaddr *sa, char *s, size_t maxlen);
/**
 * Main
 */
int main(void) {
  int newfd; // listen on sock_fd, new connection on newfd
  struct sockaddr_storage their_addr; // connector's address information
  char s[INET6_ADDRSTRLEN];

  // Start reaping child processes
  // start_reaper();

  struct cache *cache = cache_create(10, 0);

  // Get a listening socket
  int listenfd = get_listener_socket(PORT);

  if (listenfd < 0) {
    fprintf(stderr, "webserver: fatal error getting listening socket\n");
    exit(1);
  }

  printf("webserver: waiting for connections on port %s...\n", PORT);

  // This is the main loop that accepts incoming connections and
  // fork()s a handler process to take care of it. The main parent
  // process then goes back to waiting for new connections.

  while (1) {
    socklen_t sin_size = sizeof their_addr;

    // Parent process will block on the accept() call until someone
    // makes a new connection:
    newfd = accept(listenfd, (struct sockaddr *)&their_addr, &sin_size);
    if (newfd == -1) {
      perror("accept");
      continue;
    }

    // Print out a message that we got the connection
    get_in_addr(((struct sockaddr *)&their_addr), s, sizeof s);
    printf("server: got connection from %s\n", s);

    // newfd is a new socket descriptor for the new connection.
    // listenfd is still listening for new connections.

    handle_http_request(newfd, cache);

    close(newfd);
  }

  // Unreachable code

  return 0;
}
