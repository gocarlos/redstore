/*
    RedHTTP - a lightweight HTTP server library
    Copyright (C) 2010-2011 Nicholas J Humfrey <njh@aelius.com>

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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <math.h>

#include <errno.h>
#include <sys/types.h>

#include "redhttp_private.h"
#include "redhttp.h"


int redhttp_negotiate_compare_types(const char *server_type, const char *client_type)
{
  // FIXME: support type/* as well as */*
  if (strcmp(client_type, "*/*") == 0) {
    return 1;
  } else {
    return strcmp(server_type, client_type) == 0;
  }
}

char *redhttp_negotiate_choose(redhttp_negotiate_t ** server, redhttp_negotiate_t ** client)
{
  const char *best_type = NULL;
  redhttp_negotiate_t *s, *c;
  int best_score = -1;
  char *result = NULL;

  for (s = *server; s; s = s->next) {
    for (c = *client; c; c = c->next) {
      if (redhttp_negotiate_compare_types(s->type, c->type)) {
        int score = s->q * c->q;
        if (score > best_score) {
          best_score = score;
          best_type = s->type;
          break;
        }
      }
    }
  }

  // Copy best match into the result buffer
  if (best_type) {
    result = redhttp_strdup(best_type);
  }

  return result;
}

static int redhttp_negotiate_round(double d) {
  if (d >= 0)
    return (int) (d+0.5);
  else
    return (int) (d-0.5);
}

redhttp_negotiate_t *redhttp_negotiate_parse(const char *str)
{
  redhttp_negotiate_t *first = NULL;
  const char *start = str;
  const char *ptr;

  if (str == NULL || *str == '\0')
    return NULL;

  for (ptr = str; 1; ptr++) {
    if (*ptr == ',' || *ptr == '\0') {
      const char *params = start;
      int q = 10;

      // Re-scan for start of parameters
      for (params = start; params < ptr; params++) {
        if (*params == ';') {
          const char *p;
          // Scan for q= parameter
          // FIXME: this could be improved
          for (p = params; p < (ptr - 3); p++) {
            if (p[0] == 'q' && p[1] == '=') {
              const char * nptr = &p[2];
              char * endptr = NULL;
              double d = strtod(nptr, &endptr);
              if (endptr != nptr && d >= 0.0 && d <= 1.0)
                q = redhttp_negotiate_round(d * 10.0);
            }
          }
          break;
        }
      }

      // FIXME: store the other MIME type parameters, for example
      // text/html;version=2.0
      redhttp_negotiate_add(&first, start, (params - start), q);
      start = ptr + 1;
    }

    if (*ptr == '\0')
      break;
  }

  return first;
}

int redhttp_negotiate_get(redhttp_negotiate_t ** first, int i, const char **type, int *q)
{
  redhttp_negotiate_t *it;
  int count = 0;

  assert(first != NULL);

  if (i < 0)
    return -1;

  for (it = *first; it; it = it->next) {
    if (count == i) {
      if (type)
        *type = it->type;

      if (q)
        *q = it->q;

      return 0;
    }
    count++;
  }

  return -1;
}

int redhttp_negotiate_count(redhttp_negotiate_t ** first)
{
  redhttp_negotiate_t *it;
  int count = 0;

  assert(first != NULL);

  for (it = *first; it; it = it->next) {
    count++;
  }

  return count;
}

void redhttp_negotiate_sort(redhttp_negotiate_t ** first)
{
  redhttp_negotiate_t *a = NULL;
  redhttp_negotiate_t *b = NULL;
  redhttp_negotiate_t *c = NULL;
  redhttp_negotiate_t *e = NULL;
  redhttp_negotiate_t *tmp = NULL;

  if (first == NULL || *first == NULL)
    return;

  while (e != (*first)->next) {
    c = a = *first;
    b = a->next;
    while (a != e) {
      if (a->q < b->q) {
        if (a == *first) {
          tmp = b->next;
          b->next = a;
          a->next = tmp;
          *first = b;
          c = b;
        } else {
          tmp = b->next;
          b->next = a;
          a->next = tmp;
          c->next = b;
          c = b;
        }
      } else {
        c = a;
        a = a->next;
      }
      b = a->next;
      if (b == e)
        e = a;
    }
  }
}

void redhttp_negotiate_add(redhttp_negotiate_t ** first, const char *type, size_t type_len, int q)
{
  redhttp_negotiate_t *new;

  assert(first != NULL);
  assert(type != NULL);
  assert(type_len > 0);
  assert(q <= 10 && q >= 0);

  // Skip whitespace at the start
  while (isspace(*type)) {
    type++;
    type_len--;
  }

  // Skip whitespace at the end
  while (isspace(type[type_len - 1])) {
    type_len--;
  }

  // Create new MIME Type stucture
  new = calloc(1, sizeof(redhttp_negotiate_t));
  new->type = redhttp_strndup(type, type_len);
  new->q = q;
  new->next = NULL;

  // append it to the list
  if (*first) {
    redhttp_negotiate_t *it;
    for (it = *first; it->next; it = it->next);
    it->next = new;
  } else {
    *first = new;
  }

  // sort the list
  redhttp_negotiate_sort(first);
}

char* redhttp_negotiate_to_string(redhttp_negotiate_t ** first)
{
  redhttp_negotiate_t *it;
  size_t str_len = 0;
  char *str = NULL;
  char *ptr = NULL;

  assert(first != NULL);

  // First, calculate the length of the string
  for (it = *first; it; it = it->next) {
    str_len += strlen(it->type);
    if (it->q != 10)
      str_len += 6; // ;q=0.5
    if (it->next)
      str_len += 1; // ,
  }

  ptr = str = malloc(str_len+1);
  if (!str)
    return NULL;

  // Second, build up the string
  for (it = *first; it; it = it->next) {
    size_t type_len = strlen(it->type);
    memcpy(ptr, it->type, type_len);
    ptr += type_len;
    if (it->q != 10) {
      snprintf(ptr, 7, ";q=%1.1f", (double)it->q / 10.0);
      ptr += 6;
    }
    if (it->next) {
      *ptr = ',';
      ptr += 1;
    }
  }
  *ptr = '\0';

  return str;
}

void redhttp_negotiate_print(redhttp_negotiate_t ** first, FILE* socket)
{
  redhttp_negotiate_t *it;

  assert(first != NULL);
  assert(socket != NULL);

  // First, calculate the length of the string
  for (it = *first; it; it = it->next) {
    fprintf(socket, "%s;q=%1.1f\n", it->type, (double)it->q / 10.0);
  }
}

void redhttp_negotiate_free(redhttp_negotiate_t ** first)
{
  redhttp_negotiate_t *it, *next;

  assert(first != NULL);

  for (it = *first; it; it = next) {
    next = it->next;
    free(it->type);
    free(it);
  }
}
