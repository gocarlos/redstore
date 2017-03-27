/*
    RedHTTP - a lightweight HTTP server library
    Copyright (C) 2010-2012 Nicholas J Humfrey <njh@aelius.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#define _POSIX_C_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <assert.h>

#include <errno.h>
#include <sys/types.h>

#include "redhttp_private.h"
#include "redhttp.h"

static struct redhttp_message_s {
  unsigned int code;
  const char *message;
} redhttp_status_messages[] = {
  {
  REDHTTP_OK, "OK"}, {
  REDHTTP_CREATED, "Created"}, {
  REDHTTP_ACCEPTED, "Accepted"}, {
  REDHTTP_NO_CONTENT, "No Content"}, {
  REDHTTP_MOVED_PERMANENTLY, "Moved Permanently"}, {
  REDHTTP_MOVED_TEMPORARILY, "Moved Temporarily"}, {
  REDHTTP_SEE_OTHER, "See Other"}, {
  REDHTTP_NOT_MODIFIED, "Not Modified"}, {
  REDHTTP_BAD_REQUEST, "Bad Request"}, {
  REDHTTP_UNAUTHORIZED, "Unauthorized"}, {
  REDHTTP_FORBIDDEN, "Forbidden"}, {
  REDHTTP_NOT_FOUND, "Not Found"}, {
  REDHTTP_METHOD_NOT_ALLOWED, "Method Not Allowed"}, {
  REDHTTP_NOT_ACCEPTABLE, "Not Acceptable"}, {
  REDHTTP_INTERNAL_SERVER_ERROR, "Internal Server Error"}, {
  REDHTTP_NOT_IMPLEMENTED, "Not Implemented"}, {
  REDHTTP_BAD_GATEWAY, "Bad Gateway"}, {
  REDHTTP_SERVICE_UNAVAILABLE, "Service Unavailable"}, {
0, NULL}};


const char* redhttp_response_status_message_for_code(int code)
{
  int i;
  for (i = 0; redhttp_status_messages[i].message; i++) {
    if (redhttp_status_messages[i].code == code) {
      return redhttp_status_messages[i].message;
    }
  }
  return NULL;
}

redhttp_response_t *redhttp_response_new(int code, const char *message)
{
  redhttp_response_t *response;

  assert(code >= 100 && code < 1000);

  response = calloc(1, sizeof(redhttp_response_t));
  if (!response) {
    perror("failed to allocate memory for redhttp_response_t");
    return NULL;
  }

  // Set default content length to -1 (unknown)
  response->content_length = -1;

  redhttp_response_set_status_code(response, code);
  if (message)
    redhttp_response_set_status_message(response, message);

  return response;
}

redhttp_response_t *redhttp_response_new_empty(int status)
{
  redhttp_response_t *response = redhttp_response_new(status, NULL);
  if (response)
    response->content_length = 0;
  return response;
}

redhttp_response_t *redhttp_response_new_with_type(int status, const char *message,
                                                   const char *type)
{
  redhttp_response_t *response = redhttp_response_new(status, message);
  if (response)
    redhttp_response_add_header(response, "Content-Type", type);
  return response;
}

redhttp_response_t *redhttp_response_new_error_page(int code, const char *explanation)
{
  redhttp_response_t *response = redhttp_response_new_with_type(code, NULL, "text/html");
  static const char ERROR_PAGE_FMT[] =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
      "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\"\n"
      " \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\n"
      "<html xmlns=\"http://www.w3.org/1999/xhtml\">\n"
      "<head><meta charset=\"utf-8\" /><title>%d %s</title></head>\n"
      "<body>\n<h1>%d %s</h1>\n" "<p>%s</p>\n" "</body></html>\n";

  assert(code >= 100 && code < 1000);
  if (!response)
    return NULL;

  if (!explanation)
    explanation = "";

  // Calculate the page length
  response->content_length =
      snprintf(NULL, 0, ERROR_PAGE_FMT, code, response->status_message, code,
               response->status_message, explanation);
  if (response->content_length <= 0) {
    redhttp_response_free(response);
    return NULL;
  }

  response->content_buffer = calloc(1, response->content_length + 1);
  if (response->content_buffer == NULL) {
    redhttp_response_free(response);
    return NULL;
  }
  snprintf(response->content_buffer, response->content_length + 1, ERROR_PAGE_FMT, code,
           response->status_message, code, response->status_message, explanation);
  response->content_free_callback = free;

  return response;
}

redhttp_response_t *redhttp_response_new_redirect(const char *url, int code)
{
  static const char MESSAGE_FMT[] = "The document has moved <a href=\"%s\">here</a>.";
  redhttp_response_t *response;
  size_t message_length;
  char *message;

  if (!code)
    code = REDHTTP_MOVED_PERMANENTLY;

  message_length = snprintf(NULL, 0, MESSAGE_FMT, url);
  message = malloc(message_length + 1);
  if (!message)
    return NULL;
  snprintf(message, message_length + 1, MESSAGE_FMT, url);

  // Build the response
  response = redhttp_response_new_error_page(code, message);
  if (response)
    redhttp_response_add_header(response, "Location", url);
  free(message);

  return response;
}

int redhttp_response_count_headers(redhttp_response_t * response)
{
  return redhttp_headers_count(&response->headers);
}

void redhttp_response_print_headers(redhttp_response_t * response, FILE * socket)
{
  redhttp_headers_print(&response->headers, socket);
}

const char *redhttp_response_get_header(redhttp_response_t * response, const char *key)
{
  return redhttp_headers_get(&response->headers, key);
}

void redhttp_response_add_header(redhttp_response_t * response, const char *key, const char *value)
{
  redhttp_headers_add(&response->headers, key, value);
}

void redhttp_response_set_header(redhttp_response_t * response, const char *key, const char *value)
{
  redhttp_headers_set(&response->headers, key, value);
}

void redhttp_response_add_time_header(redhttp_response_t * response, const char *key, time_t timer)
{
  static const char RFC1123FMT[] = "%a, %d %b %Y %H:%M:%S GMT";
  struct tm *time_tm = gmtime(&timer);
  char date_str[BUFSIZ];

  if (!time_tm)
    return;

  if (strftime(date_str, sizeof(date_str)-1, RFC1123FMT, time_tm)) {
    redhttp_headers_add(&response->headers, key, date_str);
  }
}

void redhttp_response_copy_content(redhttp_response_t * response,
                                   const char *content, size_t length)
{
  char *new_buf;

  assert(response != NULL);
  assert(content != NULL);
  assert(length > 0);

  new_buf = realloc(response->content_buffer, length+1);
  if (new_buf) {
    memcpy(new_buf, content, length);
    new_buf[length] = '\0';
    redhttp_response_set_content(response, new_buf, length, free);
  }
}

void redhttp_response_set_content(redhttp_response_t * response, char *content, size_t length, void (*content_free_callback) (void *ptr))
{
  assert(response != NULL);
  assert(content != NULL);
  assert(length > 0);

  response->content_buffer = content;
  response->content_length = length;
  response->content_free_callback = content_free_callback;
}

void redhttp_response_send(redhttp_response_t * response, redhttp_request_t * request)
{
  assert(request != NULL);
  assert(response != NULL);

  if (!response->headers_sent) {
    // Add a content-length header, if content length has been defined
    if (response->content_length >= 0) {
      char length_str[32] = "";
      snprintf(length_str, sizeof(length_str), "%d", response->content_length);
      redhttp_response_add_header(response, "Content-Length", length_str);
    }

    redhttp_response_add_time_header(response, "Date", time(NULL));
    redhttp_response_add_header(response, "Connection", "Close");

    if (request->server) {
      const char *signature = redhttp_server_get_signature(request->server);
      if (signature)
        redhttp_response_add_header(response, "Server", signature);
    }

    if (request->version && strncmp(request->version, "0.9", 3) != 0) {
      fprintf(request->socket, "HTTP/1.0 %d %s\r\n",
              response->status_code, response->status_message);
      redhttp_response_print_headers(response, request->socket);
      fputs("\r\n", request->socket);
    }

    response->headers_sent = 1;
  }

  if (response->content_buffer) {
    int written = 0;
    assert(response->content_length > 0);
    written = fwrite(response->content_buffer, 1, response->content_length, request->socket);
    if (written != response->content_length) {
      perror("failed to write response to client");
    }
  }
}

void redhttp_response_set_status_code(redhttp_response_t * response, int code)
{
  assert(code >= 100 && code < 1000);
  response->status_code = code;

  redhttp_response_set_status_message(
    response, redhttp_response_status_message_for_code(code)
  );
}

int redhttp_response_get_status_code(redhttp_response_t * response)
{
  return response->status_code;
}

void redhttp_response_set_status_message(redhttp_response_t * response, const char* message)
{
  if (response->status_message) {
    free(response->status_message);
  }
  if (message == NULL) {
    response->status_message = redhttp_strdup("Unknown");
  } else {
    response->status_message = redhttp_strdup(message);
  }
}

const char *redhttp_response_get_status_message(redhttp_response_t * response)
{
  return response->status_message;
}

char *redhttp_response_get_content_buffer(redhttp_response_t * response)
{
  return response->content_buffer;
}

// Returns -1 when content_length is unknown
int redhttp_response_get_content_length(redhttp_response_t * response)
{
  return response->content_length;
}

void *redhttp_response_get_user_data(redhttp_response_t * response)
{
  return response->user_data;
}

void redhttp_response_set_user_data(redhttp_response_t * response, void *user_data)
{
  response->user_data = user_data;
}

void redhttp_response_free(redhttp_response_t * response)
{
  assert(response != NULL);

  if (response->status_message)
    free(response->status_message);
  if (response->content_free_callback && response->content_buffer)
    response->content_free_callback(response->content_buffer);

  redhttp_headers_free(&response->headers);
  free(response);
}
