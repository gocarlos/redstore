/*
    RedStore - a lightweight RDF triplestore powered by Redland
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
#include <string.h>

#include "redstore.h"


static librdf_node* new_graph_node(const char * base_uri_str)
{
  char *id = redstore_genid();
  char *path = NULL;
  size_t path_len = 6 + strlen(id) + 1;
  librdf_node *graph_node = NULL;
  librdf_uri *base_uri = NULL;
  librdf_uri *graph_uri = NULL;

  path = malloc(path_len);
  if (path) {
    snprintf(path, path_len, "/data/%s", id);
  } else {
    redstore_error("Failed to create path for new graph.");
    goto CLEANUP;
  }

  base_uri = librdf_new_uri(world, (unsigned char*)base_uri_str);
  if (!base_uri) {
    redstore_error("Failed to create librdf_uri for current request.");
    goto CLEANUP;
  }

  graph_uri = librdf_new_uri_relative_to_base(base_uri, (unsigned char*)path);
  if (graph_uri) {
    redstore_debug("New Graph URI: %s", librdf_uri_as_string(graph_uri));
  } else {
    redstore_error("Failed to create librdf_uri for new graph.");
    goto CLEANUP;
  }

  // Create node
  graph_node = librdf_new_node_from_uri(world, graph_uri);
  if (!graph_node) {
    redstore_error("Failed to create graph node from librdf_uri.");
  }

CLEANUP:
  if (path)
    free(path);
  if (base_uri)
    librdf_free_uri(base_uri);
  if (graph_uri)
    librdf_free_uri(graph_uri);

  return graph_node;
}


static librdf_node *get_graph_node(redhttp_request_t * request)
{
  unsigned char *graph = NULL;
  librdf_uri *base_uri = NULL;
  librdf_uri *graph_uri = NULL;
  librdf_node *graph_node = NULL;

  base_uri = librdf_new_uri(world, (unsigned char*)redhttp_request_get_url(request));
  if (!base_uri) {
    redstore_error("Failed to create librdf_uri for current request.");
    goto CLEANUP;
  }

  graph = (unsigned char *)redhttp_request_get_argument(request, "graph");
  if (graph) {
    graph_uri = librdf_new_uri_relative_to_base(base_uri, graph);
  } else {
    graph_uri = librdf_new_uri_from_uri(base_uri);
  }

  if (graph_uri) {
    redstore_debug("Graph URI: %s", librdf_uri_as_string(graph_uri));
  } else {
    redstore_error("Failed to create librdf_uri for graph.");
    goto CLEANUP;
  }

  // Create node
  graph_node = librdf_new_node_from_uri(world, graph_uri);
  if (!graph_node) {
    redstore_error("Failed to create graph node from librdf_uri.");
  }

CLEANUP:
  if (base_uri)
    librdf_free_uri(base_uri);
  if (graph_uri)
    librdf_free_uri(graph_uri);

  return graph_node;
}

static redhttp_response_t *remove_all_statements(redhttp_request_t *request)
{
  librdf_iterator *iterator = NULL;
  librdf_stream *stream = NULL;
  int err = 0;

  // FIXME: re-implement using librdf_model_remove_statements()

  // First:  delete all the named graphs
  iterator = librdf_storage_get_contexts(storage);
  if (!iterator) {
    return redstore_page_new_with_message(
      request, LIBRDF_LOG_ERROR, REDHTTP_INTERNAL_SERVER_ERROR, "Failed to get list of graphs."
    );
  }

  while (!librdf_iterator_end(iterator)) {
    librdf_node *graph = (librdf_node *) librdf_iterator_get_object(iterator);
    if (!graph) {
      redstore_error("librdf_iterator_get_next returned NULL");
      break;
    }

    if (librdf_model_context_remove_statements(model, graph))
      err++;

    librdf_iterator_next(iterator);
  }
  librdf_free_iterator(iterator);


  // Second: delete the remaining triples
  stream = librdf_model_as_stream(model);
  if (!stream) {
    return redstore_page_new_with_message(
      request, LIBRDF_LOG_ERROR, REDHTTP_INTERNAL_SERVER_ERROR,
      "Failed to stream model."
    );
  }

  while (!librdf_stream_end(stream)) {
    librdf_statement *statement = (librdf_statement *) librdf_stream_get_object(stream);
    if (!statement) {
      redstore_error("librdf_stream_next returned NULL in remove_all_statements()");
      break;
    }

    if (librdf_model_remove_statement(model, statement))
      err++;

    librdf_stream_next(stream);
  }
  librdf_free_stream(stream);

  if (err || error_buffer) {
    return redstore_page_new_with_message(
      request, LIBRDF_LOG_ERROR, REDHTTP_INTERNAL_SERVER_ERROR, "Error deleting some statements."
    );
  } else {
    return redstore_page_new_with_message(
      request, LIBRDF_LOG_INFO, REDHTTP_OK, "Successfully deleted all statements."
    );
  }
}

static redhttp_response_t *post_to_new_graph(redhttp_request_t * request)
{
  redhttp_response_t *response = NULL;
  librdf_node *graph_node = NULL;
  int attempts;

  for(attempts=0; attempts<10; attempts++) {
    graph_node = new_graph_node(redhttp_request_get_url(request));
    if (!graph_node) {
      return redstore_page_new_with_message(
        request, LIBRDF_LOG_ERROR, REDHTTP_INTERNAL_SERVER_ERROR,
        "Failed to create node for graph identifier."
      );
    }

    if (!librdf_model_contains_context(model, graph_node)) {
      break;
    } else {
      librdf_uri *graph_uri = librdf_node_get_uri(graph_node);
      redstore_debug("Graph already exists while trying to create new graph identifier: %s",
                     librdf_uri_as_string(graph_uri));
      librdf_free_node(graph_node);
      graph_node = NULL;
    }
  }

  if (graph_node) {
    response = parse_data_from_request_body(request, graph_node, load_stream_into_new_graph);
    librdf_free_node(graph_node);
  } else {
    response = redstore_page_new_with_message(
      request, LIBRDF_LOG_ERROR, REDHTTP_INTERNAL_SERVER_ERROR,
      "Failed to create new graph identifier."
    );
  }

  return response;
}



redhttp_response_t *handle_data_head(redhttp_request_t * request, void *user_data)
{
  int has_graph = redhttp_request_argument_exists(request, "graph");
  int has_default = redhttp_request_argument_exists(request, "default");
  int has_path = redhttp_request_get_path_glob(request) != NULL;
  redhttp_response_t *response = NULL;

  if (has_graph && has_default) {
    return redstore_page_new_with_message(
      request, LIBRDF_LOG_DEBUG, REDHTTP_BAD_REQUEST,
      "The 'graph' and 'default' arguments can not be used together."
    );
  }

  if (!has_graph && !has_path && !has_default) {
    return redstore_page_new_with_message(
      request, LIBRDF_LOG_DEBUG, REDHTTP_BAD_REQUEST, "Missing graph path or argument."
    );
  }

  if (has_default) {
    response = redhttp_response_new(REDHTTP_OK, NULL);
  } else {
    librdf_node *graph_node = get_graph_node(request);

    if (!graph_node) {
      return redstore_page_new_with_message(
        request, LIBRDF_LOG_ERROR, REDHTTP_INTERNAL_SERVER_ERROR, "Failed to get node for graph."
      );
    }

    if (librdf_model_contains_context(model, graph_node)) {
      response = redhttp_response_new(REDHTTP_OK, NULL);
    } else {
      response = redstore_page_new_with_message(
        request, LIBRDF_LOG_INFO, REDHTTP_NOT_FOUND, "Graph not found."
      );
    }

    librdf_free_node(graph_node);
  }

  return response;
}

redhttp_response_t *handle_data_get(redhttp_request_t * request, void *user_data)
{
  int has_graph = redhttp_request_argument_exists(request, "graph");
  int has_default = redhttp_request_argument_exists(request, "default");
  int has_path = redhttp_request_get_path_glob(request) != NULL;
  redhttp_response_t *response = NULL;
  librdf_node *graph_node = NULL;
  librdf_stream *stream = NULL;

  if (has_graph && has_default) {
    return redstore_page_new_with_message(
      request, LIBRDF_LOG_DEBUG, REDHTTP_BAD_REQUEST,
      "The 'graph' and 'default' arguments can not be used together."
    );
  }

  if (!has_graph && !has_path && !has_default) {
    return redstore_page_new_with_message(
      request, LIBRDF_LOG_DEBUG, REDHTTP_BAD_REQUEST, "Missing graph path or argument."
    );
  }

  if (has_default) {
    stream = librdf_model_as_stream(model);
    if (!stream) {
      response = redstore_page_new_with_message(
        request, LIBRDF_LOG_ERROR, REDHTTP_INTERNAL_SERVER_ERROR, "Failed to stream model."
      );
      goto CLEANUP;
    }
  } else {
    graph_node = get_graph_node(request);
    if (!graph_node) {
      response = redstore_page_new_with_message(
        request, LIBRDF_LOG_ERROR, REDHTTP_INTERNAL_SERVER_ERROR,
        "Failed to get node for graph."
      );
      goto CLEANUP;
    }

    // Check if the graph exists
    if (!librdf_model_contains_context(model, graph_node)) {
      response = redstore_page_new_with_message(request,
        LIBRDF_LOG_INFO, REDHTTP_NOT_FOUND, "Graph not found."
      );
      goto CLEANUP;
    }

    // Stream the graph
    stream = librdf_model_context_as_stream(model, graph_node);
    if (!stream) {
      response = redstore_page_new_with_message(
        request, LIBRDF_LOG_ERROR, REDHTTP_INTERNAL_SERVER_ERROR, "Failed to stream context."
      );
      goto CLEANUP;
    }
  }

  response = format_graph_stream(request, stream);

CLEANUP:
  if (stream)
    librdf_free_stream(stream);
  if (graph_node)
    librdf_free_node(graph_node);

  return response;
}

redhttp_response_t *handle_data_post(redhttp_request_t * request, void *user_data)
{
  int has_graph = redhttp_request_argument_exists(request, "graph");
  int has_default = redhttp_request_argument_exists(request, "default");
  int has_path = redhttp_request_get_path_glob(request) != NULL;
  redhttp_response_t *response = NULL;

  if (has_graph && has_default) {
    return redstore_page_new_with_message(
      request, LIBRDF_LOG_DEBUG, REDHTTP_BAD_REQUEST,
      "The 'graph' and 'default' arguments can not be used together."
    );
  }

  if (has_default) {
    response = parse_data_from_request_body(request, NULL, load_stream_into_graph);
  } else if (has_graph || has_path) {
    librdf_node *graph_node = get_graph_node(request);
    if (!graph_node) {
      return redstore_page_new_with_message(
        request, LIBRDF_LOG_ERROR, REDHTTP_INTERNAL_SERVER_ERROR, "Failed to get node for graph."
      );
    }

    response = parse_data_from_request_body(request, graph_node, load_stream_into_graph);

    librdf_free_node(graph_node);
  } else {
    response = post_to_new_graph(request);
  }

  return response;
}

redhttp_response_t *handle_data_put(redhttp_request_t * request, void *user_data)
{
  int has_graph = redhttp_request_argument_exists(request, "graph");
  int has_default = redhttp_request_argument_exists(request, "default");
  int has_path = redhttp_request_get_path_glob(request) != NULL;

  if (has_graph && has_default) {
    return redstore_page_new_with_message(
      request, LIBRDF_LOG_DEBUG, REDHTTP_BAD_REQUEST,
      "The 'graph' and 'default' arguments can not be used together."
    );
  }

  if (!has_graph && !has_path && !has_default) {
    return redstore_page_new_with_message(
      request, LIBRDF_LOG_DEBUG, REDHTTP_BAD_REQUEST, "Missing graph path or argument."
    );
  }

  if (has_default) {
    return redstore_page_new_with_message(
      request, LIBRDF_LOG_DEBUG, REDHTTP_NOT_IMPLEMENTED,
      "PUTing data into the default graph is not implemented."
    );
  } else {
    librdf_node *graph_node = get_graph_node(request);
    if (graph_node) {
      return parse_data_from_request_body(
        request, graph_node, clear_and_load_stream_into_graph
      );
    } else {
      return redstore_page_new_with_message(
        request, LIBRDF_LOG_ERROR, REDHTTP_INTERNAL_SERVER_ERROR,
        "Failed to get node for graph."
      );
    }
  }

}

redhttp_response_t *handle_data_delete(redhttp_request_t * request, void *user_data)
{
  int has_graph = redhttp_request_argument_exists(request, "graph");
  int has_default = redhttp_request_argument_exists(request, "default");
  int has_path = redhttp_request_get_path_glob(request) != NULL;
  redhttp_response_t *response = NULL;

  if (has_graph && has_default) {
    return redstore_page_new_with_message(
      request, LIBRDF_LOG_DEBUG, REDHTTP_BAD_REQUEST,
      "The 'graph' and 'default' arguments can not be used together."
    );
  }

  if (!has_graph && !has_path && !has_default) {
    return redstore_page_new_with_message(
      request, LIBRDF_LOG_DEBUG, REDHTTP_BAD_REQUEST, "Missing graph path or argument."
    );
  }

  if (has_default) {
    response = remove_all_statements(request);
  } else {
    librdf_node *graph_node = get_graph_node(request);

    if (!graph_node) {
      return redstore_page_new_with_message(
        request, LIBRDF_LOG_ERROR, REDHTTP_INTERNAL_SERVER_ERROR,
        "Failed to get node for graph."
      );
    }

    // Check if the graph exists
    if (!librdf_model_contains_context(model, graph_node)) {
      librdf_free_node(graph_node);
      return redstore_page_new_with_message(
        request, LIBRDF_LOG_INFO, REDHTTP_NOT_FOUND, "Graph not found."
      );
    }

    if (librdf_model_context_remove_statements(model, graph_node)) {
      response = redstore_page_new_with_message(
        request, LIBRDF_LOG_ERROR, REDHTTP_INTERNAL_SERVER_ERROR,
        "Error while trying to delete graph"
      );
    } else {
      response = redstore_page_new_with_message(
        request, LIBRDF_LOG_INFO, REDHTTP_OK,
        "Successfully deleted graph."
      );
    }

    librdf_free_node(graph_node);
  }

  return response;
}
