/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * rdf_stream.c - RDF Statement Stream Implementation
 *
 * Copyright (C) 2000-2008, David Beckett http://www.dajobe.org/
 * Copyright (C) 2000-2004, University of Bristol, UK http://www.bristol.ac.uk/
 * 
 * This package is Free Software and part of Redland http://librdf.org/
 * 
 * It is licensed under the following three licenses as alternatives:
 *   1. GNU Lesser General Public License (LGPL) V2.1 or any newer version
 *   2. GNU General Public License (GPL) V2 or any newer version
 *   3. Apache License, V2.0 or any newer version
 * 
 * You may not use this file except in compliance with at least one of
 * the above three licenses.
 * 
 * See LICENSE.html or LICENSE.txt at the top of this package for the
 * complete terms and further detail along with the license texts for
 * the licenses in COPYING.LIB, COPYING and LICENSE-2.0.txt respectively.
 * 
 * 
 */


#ifdef HAVE_CONFIG_H
#include <rdf_config.h>
#endif

#ifdef WIN32
#include <win32_rdf_config.h>
#endif

#include <stdio.h>
#include <sys/types.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h> /* for abort() as used in errors */
#endif

#include <redland.h>


#ifndef STANDALONE

/* prototypes of local helper functions */
static librdf_statement* librdf_stream_update_current_statement(librdf_stream* stream);


/**
 * librdf_new_stream:
 * @world: redland world object
 * @context: context to pass to the stream implementing objects
 * @is_end_method: pointer to function to test for end of stream
 * @next_method: pointer to function to move to the next statement in stream
 * @get_method: pointer to function to get the current statement
 * @finished_method: pointer to function to finish the stream.
 *
 * Constructor - create a new #librdf_stream.
 *
 * Creates a new stream with an implementation based on the passed in
 * functions.  The functions next_statement and end_of_stream will be called
 * multiple times until either of them signify the end of stream by
 * returning NULL or non 0 respectively.  The finished function is called
 * once only when the stream object is destroyed with librdf_free_stream()
 *
 * A mapping function can be set for the stream using librdf_stream_add_map()
 * function which allows the statements generated by the stream to be
 * filtered and/or altered as they are generated before passing back
 * to the user.
 *
 * Return value:  a new #librdf_stream object or NULL on failure
 **/
REDLAND_EXTERN_C
librdf_stream*
librdf_new_stream(librdf_world *world, 
                  void* context,
                  int (*is_end_method)(void*),
                  int (*next_method)(void*),
                  void* (*get_method)(void*, int),
                  void (*finished_method)(void*))
{
  librdf_stream* new_stream;
  
  librdf_world_open(world);

  new_stream=(librdf_stream*)LIBRDF_CALLOC(librdf_stream, 1, 
					   sizeof(librdf_stream));
  if(!new_stream)
    return NULL;

  new_stream->world=world;
  new_stream->context=context;

  new_stream->is_end_method=is_end_method;
  new_stream->next_method=next_method;
  new_stream->get_method=get_method;
  new_stream->finished_method=finished_method;

  new_stream->is_finished=0;
  new_stream->current=NULL;
  
  return new_stream;
}


/* helper function for deleting list map */
static void
librdf_stream_free_stream_map(void *list_data, void *user_data) 
{
  librdf_stream_map* map=(librdf_stream_map*)list_data;
  if(map->free_context)
    map->free_context(map->context);
  LIBRDF_FREE(librdf_stream_map, map);
}

  

/**
 * librdf_free_stream:
 * @stream: #librdf_stream object
 *
 * Destructor - destroy an #libdf_stream object.
 *
 **/
void
librdf_free_stream(librdf_stream* stream) 
{
  if(!stream)
    return;
  
  if(stream->finished_method)
    stream->finished_method(stream->context);

  if(stream->map_list) {
    librdf_list_foreach(stream->map_list,
                        librdf_stream_free_stream_map, NULL);
    librdf_free_list(stream->map_list);
  }
  
  LIBRDF_FREE(librdf_stream, stream);
}


/*
 * librdf_stream_update_current_statement - helper function to get the next element with map applied
 * @stream: #librdf_stream object
 * 
 * A helper function that gets the next element subject to the user
 * defined map function, if set by librdf_stream_add_map(),
 * 
 * Return value: the next statement or NULL at end of stream
 */
static librdf_statement*
librdf_stream_update_current_statement(librdf_stream* stream)
{
  librdf_statement* statement=NULL;

  if(stream->is_updated)
    return stream->current;

  stream->is_updating=1;

  /* find next statement subject to map */
  while(!stream->is_end_method(stream->context)) {
    librdf_iterator* map_iterator; /* Iterator over stream->map_list librdf_list */
    statement=(librdf_statement*)stream->get_method(stream->context,
                                 LIBRDF_STREAM_GET_METHOD_GET_OBJECT);
    if(!statement)
      break;

    if(!stream->map_list || !librdf_list_size(stream->map_list))
      break;
    
    map_iterator=librdf_list_get_iterator(stream->map_list);
    if(!map_iterator) {
      statement=NULL;
      break;
    }
    
    while(!librdf_iterator_end(map_iterator)) {
      librdf_stream_map *map=(librdf_stream_map*)librdf_iterator_get_object(map_iterator);
      if(!map)
        break;
      
      /* apply the map to the element  */
      statement=map->fn(stream, map->context, statement);
      if(!statement)
        break;

      librdf_iterator_next(map_iterator);
    }
    librdf_free_iterator(map_iterator);
    

    /* found something, return it */
    if(statement)
      break;

    stream->next_method(stream->context);
  }

  stream->current=statement;
  if(!stream->current)
    stream->is_finished=1;

  stream->is_updated=1;
  stream->is_updating=0;

  return statement;
}


/**
 * librdf_stream_end:
 * @stream: #librdf_stream object
 *
 * Test if the stream has ended.
 * 
 * Return value: non 0 at end of stream.
 **/
int
librdf_stream_end(librdf_stream* stream) 
{
  /* always end of NULL stream */
  if(!stream || stream->is_finished)
    return 1;
  
  librdf_stream_update_current_statement(stream);

  return stream->is_finished;
}


/**
 * librdf_stream_next:
 * @stream: #librdf_stream object
 *
 * Move to the next librdf_statement in the stream.
 *
 * Return value: non 0 if the stream has finished
 **/
int
librdf_stream_next(librdf_stream* stream) 
{
  if(!stream || stream->is_finished)
    return 1;

  if(stream->next_method(stream->context)) {
    stream->is_finished=1;
    return 1;
  }
  
  stream->is_updated=0;
  librdf_stream_update_current_statement(stream);

  return stream->is_finished;
}


/**
 * librdf_stream_get_object:
 * @stream: #librdf_stream object
 *
 * Get the current librdf_statement in the stream.
 *
 * This method returns a SHARED pointer to the current statement object
 * which should be copied by the caller to preserve it if the stream
 * is moved on librdf_stream_next or if it should last after the
 * stream is closed.
 * 
 * Return value: the current #librdf_statement object or NULL at end of stream.
 **/
librdf_statement*
librdf_stream_get_object(librdf_stream* stream) 
{
  if(stream->is_finished)
    return NULL;

  return librdf_stream_update_current_statement(stream);
}


/**
 * librdf_stream_get_context:
 * @stream: the #librdf_stream object
 *
 * Get the context of the current object on the stream.
 *
 * This method returns a SHARED pointer to the current context node object
 * which should be copied by the caller to preserve it if the stream
 * is moved on librdf_stream_next or if it should last after the
 * stream is closed.
 * 
 * Return value: The context node (can be NULL) or NULL if the stream has finished.
 **/
void*
librdf_stream_get_context(librdf_stream* stream) 
{
  if(stream->is_finished)
    return NULL;

  /* Update current statement only if we are not already in the middle of the
     statement update process.
     Allows inspection of context nodes in stream map callbacks. */
  if(!stream->is_updating && !librdf_stream_update_current_statement(stream))
    return NULL;

  return stream->get_method(stream->context, 
                            LIBRDF_STREAM_GET_METHOD_GET_CONTEXT);
}


/**
 * librdf_stream_add_map:
 * @stream: the stream
 * @map_function: the function to perform the mapping
 * @free_context: the function to use to free the context (or NULL)
 * @map_context: the context to pass to the map function
 *
 * Add a librdf_stream mapping function.
 * 
 * Adds an stream mapping function which operates over the stream to
 * select which elements are returned; it will be applied as soon as
 * this method is called.
 *
 * Several mapping functions can be added and they are applied in
 * the order given.
 *
 * The mapping function should return the statement to return, or NULL
 * to remove it from the stream.
 *
 * Return value: Non 0 on failure
 **/
int
librdf_stream_add_map(librdf_stream* stream, 
                      librdf_stream_map_handler map_function,
                      librdf_stream_map_free_context_handler free_context,
                      void *map_context)
{
  librdf_stream_map *map;
  
  if(!stream->map_list) {
    stream->map_list=librdf_new_list(stream->world);
    if(!stream->map_list) {
      if(free_context && map_context)
        (*free_context)(map_context);
      return 1;
    }
  }

  map=(librdf_stream_map*)LIBRDF_CALLOC(librdf_stream_map, sizeof(librdf_stream_map), 1);
  if(!map) {
    if(free_context && map_context)
      (*free_context)(map_context);
    return 1;
  }

  map->fn=map_function;
  map->free_context=free_context;
  map->context=map_context;

  if(librdf_list_add(stream->map_list, map)) {
    LIBRDF_FREE(librdf_stream_map, map);
    if(free_context && map_context)
      (*free_context)(map_context);
    return 1;
  }
  
  return 0;
}



static int librdf_stream_from_node_iterator_end_of_stream(void* context);
static int librdf_stream_from_node_iterator_next_statement(void* context);
static void* librdf_stream_from_node_iterator_get_statement(void* context, int flags);
static void librdf_stream_from_node_iterator_finished(void* context);

typedef struct {
  librdf_iterator *iterator;
  librdf_statement *current; /* shared statement */
  librdf_statement_part field;
} librdf_stream_from_node_iterator_stream_context;



/**
 * librdf_new_stream_from_node_iterator:
 * @iterator: #librdf_iterator of #librdf_node objects
 * @statement: #librdf_statement prototype with one NULL node space
 * @field: node part of statement
 *
 * Constructor - create a new #librdf_stream from an iterator of nodes.
 *
 * Creates a new #librdf_stream using the passed in #librdf_iterator
 * which generates a series of #librdf_node objects.  The resulting
 * nodes are then inserted into the given statement and returned.
 * The field attribute indicates which statement node is being generated.
 *
 * Return value: a new #librdf_stream object or NULL on failure
 **/
librdf_stream*
librdf_new_stream_from_node_iterator(librdf_iterator* iterator,
                                     librdf_statement* statement,
                                     librdf_statement_part field)
{
  librdf_stream_from_node_iterator_stream_context *scontext;
  librdf_stream *stream;

  scontext=(librdf_stream_from_node_iterator_stream_context*)LIBRDF_CALLOC(librdf_stream_from_node_iterator_stream_context, 1, sizeof(librdf_stream_from_node_iterator_stream_context));
  if(!scontext)
    return NULL;

  /* copy the prototype statement */
  statement=librdf_new_statement_from_statement(statement);
  if(!statement) {
    LIBRDF_FREE(librdf_stream_from_node_iterator_stream_context, scontext);
    return NULL;
  }

  scontext->iterator=iterator;
  scontext->current=statement;
  scontext->field=field;
  
  stream=librdf_new_stream(iterator->world,
                           (void*)scontext,
                           &librdf_stream_from_node_iterator_end_of_stream,
                           &librdf_stream_from_node_iterator_next_statement,
                           &librdf_stream_from_node_iterator_get_statement,
                           &librdf_stream_from_node_iterator_finished);
  if(!stream) {
    librdf_stream_from_node_iterator_finished((void*)scontext);
    return NULL;
  }
  
  return stream;  
}


static int
librdf_stream_from_node_iterator_end_of_stream(void* context)
{
  librdf_stream_from_node_iterator_stream_context* scontext=(librdf_stream_from_node_iterator_stream_context*)context;

  return librdf_iterator_end(scontext->iterator);
}


static int
librdf_stream_from_node_iterator_next_statement(void* context)
{
  librdf_stream_from_node_iterator_stream_context* scontext=(librdf_stream_from_node_iterator_stream_context*)context;

  return librdf_iterator_next(scontext->iterator);
}


static void*
librdf_stream_from_node_iterator_get_statement(void* context, int flags)
{
  librdf_stream_from_node_iterator_stream_context* scontext=(librdf_stream_from_node_iterator_stream_context*)context;
  librdf_node* node;
  
  switch(flags) {
    case LIBRDF_ITERATOR_GET_METHOD_GET_OBJECT:

      if(!(node=(librdf_node*)librdf_iterator_get_object(scontext->iterator)))
        return NULL;

      /* The node object above is shared, no need to free it before
       * assigning to the statement, which is also shared, and
       * return to the user.
       */
      switch(scontext->field) {
        case LIBRDF_STATEMENT_SUBJECT:
          librdf_statement_set_subject(scontext->current, node);
          break;
        case LIBRDF_STATEMENT_PREDICATE:
          librdf_statement_set_predicate(scontext->current, node);
          break;
        case LIBRDF_STATEMENT_OBJECT:
          librdf_statement_set_object(scontext->current, node);
          break;

        case LIBRDF_STATEMENT_ALL:
        default:
          librdf_log(scontext->iterator->world,
                     0, LIBRDF_LOG_ERROR, LIBRDF_FROM_STREAM, NULL,
                     "Illegal statement field %d seen", scontext->field);
          return NULL;
      }
      
      return scontext->current;

    case LIBRDF_ITERATOR_GET_METHOD_GET_CONTEXT:
      return librdf_iterator_get_context(scontext->iterator);
    default:
      librdf_log(scontext->iterator->world,
                 0, LIBRDF_LOG_ERROR, LIBRDF_FROM_STREAM, NULL,
                 "Unknown iterator method flag %d", flags);
      return NULL;
  }

}


static void
librdf_stream_from_node_iterator_finished(void* context)
{
  librdf_stream_from_node_iterator_stream_context* scontext=(librdf_stream_from_node_iterator_stream_context*)context;
  
  if(scontext->iterator)
    librdf_free_iterator(scontext->iterator);

  if(scontext->current) {
    switch(scontext->field) {
      case LIBRDF_STATEMENT_SUBJECT:
        librdf_statement_set_subject(scontext->current, NULL);
        break;
      case LIBRDF_STATEMENT_PREDICATE:
        librdf_statement_set_predicate(scontext->current, NULL);
        break;
      case LIBRDF_STATEMENT_OBJECT:
        librdf_statement_set_object(scontext->current, NULL);
        break;

      case LIBRDF_STATEMENT_ALL:
      default:
        librdf_log(scontext->iterator->world,
                   0, LIBRDF_LOG_ERROR, LIBRDF_FROM_STREAM, NULL, 
                   "Illegal statement field %d seen", scontext->field);
    }
    librdf_free_statement(scontext->current);
  }

  LIBRDF_FREE(librdf_stream_from_node_iterator_stream_context, scontext);
}


#ifndef REDLAND_DISABLE_DEPRECATED
/**
 * librdf_stream_print:
 * @stream: the stream object
 * @fh: the FILE stream to print to
 *
 * Print the stream.
 *
 * This prints the remaining statements of the stream to the given
 * file handle.  Note that after this method is called the stream
 * will be empty so that librdf_stream_end() will always be true
 * and librdf_stream_next() will always return NULL.  The only
 * useful operation is to dispose of the stream with the
 * librdf_free_stream() destructor.
 * 
 * This method is for debugging and the format of the output should
 * not be relied on.
 *
 * @Deprecated: Use librdf_stream_write() to write to
 * #raptor_iostream which can be made to write to a string.  Use a
 * #librdf_serializer to write proper syntax formats.
 *
 **/
void
librdf_stream_print(librdf_stream *stream, FILE *fh)
{
  raptor_iostream *iostr;

  if(!stream)
    return;

  iostr = raptor_new_iostream_to_file_handle(stream->world->raptor_world_ptr, fh);
  if(!iostr)
    return;
  
  while(!librdf_stream_end(stream)) {
    librdf_statement* statement = librdf_stream_get_object(stream);
    librdf_node* context_node = (librdf_node*)librdf_stream_get_context(stream);
    if(!statement)
      break;

    fputs("  ", fh);
    librdf_statement_write(statement, iostr);
    if(context_node) {
      fputs(" with context ", fh);
      librdf_node_print(context_node, fh);
    }
    fputs("\n", fh);

    librdf_stream_next(stream);
  }

  raptor_free_iostream(iostr);
}
#endif


/**
 * librdf_stream_write:
 * @stream: the stream object
 * @iostr: the iostream to write to
 *
 * Write a stream of triples to an iostream in a debug format.
 *
 * This prints the remaining statements of the stream to the given
 * #raptor_iostream in a debug format.
 *
 * Note that after this method is called the stream will be empty so
 * that librdf_stream_end() will always be true and
 * librdf_stream_next() will always return NULL.  The only useful
 * operation is to dispose of the stream with the
 * librdf_free_stream() destructor.
 * 
 * This method is for debugging and the format of the output should
 * not be relied on.  In particular, when contexts are used the
 * result may be 4 nodes.
 *
 * Return value: non-0 on failure
 **/
int
librdf_stream_write(librdf_stream *stream, raptor_iostream *iostr)
{
  LIBRDF_ASSERT_OBJECT_POINTER_RETURN_VALUE(stream, librdf_stream, 1);
  LIBRDF_ASSERT_OBJECT_POINTER_RETURN_VALUE(iostr, raptor_iostream, 1);

  while(!librdf_stream_end(stream)) {
    librdf_statement* statement;
    librdf_node* context_node;

    statement = librdf_stream_get_object(stream);
    if(!statement)
      break;

    raptor_iostream_counted_string_write("  ", 2, iostr);
    if(librdf_statement_write(statement, iostr))
      return 1;
    
    context_node = (librdf_node*)librdf_stream_get_context(stream);
    if(context_node) {
      raptor_iostream_counted_string_write(" with context", 13, iostr);
      librdf_node_write(context_node, iostr);
    }
    raptor_iostream_counted_string_write(". \n", 3, iostr);

    librdf_stream_next(stream);
  }

  return 0;
}


librdf_statement*
librdf_stream_statement_find_map(librdf_stream *stream,
                                 void* context, librdf_statement* statement) 
{
  librdf_statement* partial_statement=(librdf_statement*)context;

  /* any statement matches when no partial statement is given */
  if(!partial_statement)
    return statement;
  
  if (librdf_statement_match(statement, partial_statement)) {
    return statement;
  }

  /* not suitable */
  return NULL;
}


/**
 * librdf_new_empty_stream:
 * @world: redland world object
 *
 * Constructor - create a new #librdf_stream with no content.
 * 
 * Return value: a new #librdf_stream object or NULL on failure
**/
librdf_stream*
librdf_new_empty_stream(librdf_world *world)
{
  librdf_stream* new_stream;
  
  librdf_world_open(world);

  new_stream=(librdf_stream*)LIBRDF_CALLOC(librdf_stream, 1, 
                                               sizeof(librdf_stream));
  if(!new_stream)
    return NULL;
  
  new_stream->world=world;

  /* This ensures end, next, get_object, get_context factory methods
   * never get called and the methods always return finished.
   */
  new_stream->is_finished=1;

  return new_stream;
}


#endif


/* TEST CODE */


#ifdef STANDALONE

/* one more prototype */
int main(int argc, char *argv[]);

#define STREAM_NODES_COUNT 6
#define NODE_URI_PREFIX "http://example.org/node"

int
main(int argc, char *argv[]) 
{
  librdf_statement *statement;
  librdf_stream* stream;
  const char *program=librdf_basename((const char*)argv[0]);
  librdf_world *world;
  librdf_uri* prefix_uri;
  librdf_node* nodes[STREAM_NODES_COUNT];
  int i;
  librdf_iterator* iterator;
  int count;
  
  world=librdf_new_world();
  librdf_world_open(world);

  prefix_uri=librdf_new_uri(world, (const unsigned char*)NODE_URI_PREFIX);
  if(!prefix_uri) {
    fprintf(stderr, "%s: Failed to create prefix URI\n", program);
    return(1);
  }

  for(i=0; i < STREAM_NODES_COUNT; i++) {
    unsigned char buf[2];
    buf[0]='a'+i;
    buf[1]='\0';
    nodes[i]=librdf_new_node_from_uri_local_name(world, prefix_uri, buf);
    if(!nodes[i]) {
      fprintf(stderr, "%s: Failed to create node %i (%s)\n", program, i, buf);
      return(1);
    }
  }
  
  fprintf(stdout, "%s: Creating static node iterator\n", program);
  iterator = librdf_node_new_static_node_iterator(world, nodes, STREAM_NODES_COUNT);
  if(!iterator) {
    fprintf(stderr, "%s: Failed to create static node iterator\n", program);
    return(1);
  }

  statement=librdf_new_statement_from_nodes(world,
                                            librdf_new_node_from_uri_string(world, (const unsigned char*)"http://example.org/resource"),
                                            librdf_new_node_from_uri_string(world, (const unsigned char*)"http://example.org/property"),
                                            NULL);
  if(!statement) {
    fprintf(stderr, "%s: Failed to create statement\n", program);
    return(1);
  }

  fprintf(stdout, "%s: Creating stream from node iterator\n", program);
  stream=librdf_new_stream_from_node_iterator(iterator, statement, LIBRDF_STATEMENT_OBJECT);
  if(!stream) {
    fprintf(stderr, "%s: Failed to createstatic  node stream\n", program);
    return(1);
  }
  

  /* This is to check that the stream_from_node_iterator code
   * *really* takes a copy of what it needs from statement 
   */
  fprintf(stdout, "%s: Freeing statement\n", program);
  librdf_free_statement(statement);


  fprintf(stdout, "%s: Listing static node stream\n", program);
  count=0;
  while(!librdf_stream_end(stream)) {
    librdf_statement* s_statement=librdf_stream_get_object(stream);
    if(!s_statement) {
      fprintf(stderr, "%s: librdf_stream_current returned NULL when not end of stream\n", program);
      return(1);
    }

    fprintf(stdout, "%s: statement %d is: ", program, count);
    librdf_statement_print(s_statement, stdout);
    fputc('\n', stdout);
    
    librdf_stream_next(stream);
    count++;
  }

  if(count != STREAM_NODES_COUNT) {
    fprintf(stderr, "%s: Stream returned %d statements, expected %d\n", program,
            count, STREAM_NODES_COUNT);
    return(1);
  }

  fprintf(stdout, "%s: stream from node iterator worked ok\n", program);


  fprintf(stdout, "%s: Freeing stream\n", program);
  librdf_free_stream(stream);


  fprintf(stdout, "%s: Freeing nodes\n", program);
  for (i=0; i<STREAM_NODES_COUNT; i++) {
    librdf_free_node(nodes[i]);
  }

  librdf_free_uri(prefix_uri);
  
  librdf_free_world(world);
  
  /* keep gcc -Wall happy */
  return(0);
}

#endif
