/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * rdf_iterator.c - RDF Iterator Implementation
 *
 * $Id$
 *
 * Copyright (C) 2000-2003 David Beckett - http://purl.org/net/dajobe/
 * Institute for Learning and Research Technology - http://www.ilrt.org/
 * University of Bristol - http://www.bristol.ac.uk/
 * 
 * This package is Free Software or Open Source available under the
 * following licenses (these are alternatives):
 *   1. GNU Lesser General Public License (LGPL)
 *   2. GNU General Public License (GPL)
 *   3. Mozilla Public License (MPL)
 * 
 * See LICENSE.html or LICENSE.txt at the top of this package for the
 * full license terms.
 * 
 * 
 */


#include <rdf_config.h>

#include <stdio.h>

#include <librdf.h>


#ifndef STANDALONE
/* prototypes of local helper functions */
static void* librdf_iterator_update_current_element(librdf_iterator* iterator);


/**
 * librdf_new_iterator - Constructor - create a new librdf_iterator object
 * @world: redland world object
 * @context: context to pass to the iterator functions
 * @is_end_method: function to call to see if the iteration has ended
 * @next_method: function to get the next element
 * @get_method: function to get the next element
 * @finished_method: function to destroy the iterator context (or NULL if not needed)
 * 
 * Return value: a new &librdf_iterator object or NULL on failure
**/
librdf_iterator*
librdf_new_iterator(librdf_world *world,
                    void* context,
		    int (*is_end_method)(void*),
		    int (*next_method)(void*),
		    void* (*get_method)(void*, int),
		    void (*finished_method)(void*))
{
  librdf_iterator* new_iterator;
  
  new_iterator=(librdf_iterator*)LIBRDF_CALLOC(librdf_iterator, 1, 
                                               sizeof(librdf_iterator));
  if(!new_iterator)
    return NULL;
  
  new_iterator->world=world;

  new_iterator->context=context;
  
  new_iterator->is_end_method=is_end_method;
  new_iterator->next_method=next_method;
  new_iterator->get_method=get_method;
  new_iterator->finished_method=finished_method;

  new_iterator->is_finished=0;
  new_iterator->current=NULL;

  return new_iterator;
}


/* helper function for deleting list map */
static void
librdf_iterator_free_iterator_map(void *list_data, void *user_data) 
{
  librdf_iterator_map* map=(librdf_iterator_map*)list_data;
  if(map->free_context)
    map->free_context(map->context);
  LIBRDF_FREE(librdf_iterator_map, map);
}

  

/**
 * librdf_free_iterator - Destructor - destroy a librdf_iterator object
 * @iterator: the &librdf_iterator object
 * 
 **/
void
librdf_free_iterator(librdf_iterator* iterator) 
{
  if(!iterator)
    return;
  
  if(iterator->finished_method)
    iterator->finished_method(iterator->context);

  if(iterator->map_list) {
    librdf_list_foreach(iterator->map_list,
                        librdf_iterator_free_iterator_map, NULL);
    librdf_free_list(iterator->map_list);
  }
  
  LIBRDF_FREE(librdf_iterator, iterator);
}



/*
 * librdf_iterator_update_current_element - Update the current iterator element with filtering
 * @iterator: the &librdf_iterator object
 * 
 * Helper function to set the iterator->current to the current
 * element as filtered optionally by a user defined 
 * map function as set by librdf_iterator_add_map()
 * 
 * Return value: the next element or NULL if the iterator has ended
 */
static void*
librdf_iterator_update_current_element(librdf_iterator* iterator) 
{
  void *element=NULL;

  if(iterator->is_updated)
    return iterator->current;
  
  /* find next element subject to map */
  while(!iterator->is_end_method(iterator->context)) {
    librdf_iterator* map_iterator; /* Iterator over iterator->map_list librdf_list */
    element=iterator->get_method(iterator->context, 
                                 LIBRDF_ITERATOR_GET_METHOD_GET_OBJECT);
    if(!element)
      break;

    if(!iterator->map_list || !librdf_list_size(iterator->map_list))
      break;
    
    map_iterator=librdf_list_get_iterator(iterator->map_list);
    if(!map_iterator)
      break;
    
    while(!librdf_iterator_end(map_iterator)) {
      librdf_iterator_map *map=(librdf_iterator_map*)librdf_iterator_get_object(map_iterator);
      if(!map)
        break;
      
      /* apply the map to the element  */
      element=map->fn(iterator, element, map->context);
      if(!element)
        break;

      librdf_iterator_next(map_iterator);
    }
    librdf_free_iterator(map_iterator);
    

    /* found something, return it */
    if(element)
      break;

    iterator->next_method(iterator->context);
  }

  iterator->current=element;
  if(!iterator->current)
    iterator->is_finished=1;

  iterator->is_updated=1;

  return element;
}


/**
 * librdf_iterator_have_elements - Test if the iterator has finished
 * @iterator: the &librdf_iterator object
 * 
 * DEPRECATED - use !librdf_iterator_end(iterator)
 *
 * Return value: 0 if the iterator has finished
 **/
int
librdf_iterator_have_elements(librdf_iterator* iterator) 
{
  return !librdf_iterator_end(iterator);
}


/**
 * librdf_iterator_end - Test if the iterator has finished
 * @iterator: the &librdf_iterator object
 * 
 * Return value: non 0 if the iterator has finished
 **/
int
librdf_iterator_end(librdf_iterator* iterator) 
{
  if(!iterator || iterator->is_finished)
    return 1;

  librdf_iterator_update_current_element(iterator);

  return iterator->is_finished;
}


/**
 * librdf_iterator_next - Move to the next iterator element
 * @iterator: the &librdf_iterator object
 *
 * Return value: non 0 if the iterator has finished
 **/
int
librdf_iterator_next(librdf_iterator* iterator)
{
  if(!iterator || iterator->is_finished)
    return 1;

  if(iterator->next_method(iterator->context)) {
    iterator->is_finished=1;
    return 1;
  }

  iterator->is_updated=0;
  librdf_iterator_update_current_element(iterator);
  
  return iterator->is_finished;
}


/**
 * librdf_iterator_get_object - Get the current object from the iterator
 * @iterator: the &librdf_iterator object
 *
 * This method returns a SHARED pointer to the current iterator object
 * which should be copied by the caller to preserve it if the iterator
 * is moved on librdf_iterator_next or if it should last after the
 * iterator is closed.
 * 
 * Return value: The next element or NULL if the iterator has finished.
 **/
void*
librdf_iterator_get_object(librdf_iterator* iterator)
{
  if(iterator->is_finished)
    return NULL;

  return librdf_iterator_update_current_element(iterator);
}


/**
 * librdf_iterator_get_context - Get the context of the current object on the iterator
 * @iterator: the &librdf_iterator object
 *
 * This method returns a SHARED pointer to the current context node object
 * which should be copied by the caller to preserve it if the iterator
 * is moved on librdf_iterator_next or if it should last after the
 * iterator is closed.
 * 
 * Return value: The context or NULL if the iterator has finished.
 **/
void*
librdf_iterator_get_context(librdf_iterator* iterator) 
{
  if(iterator->is_finished)
    return NULL;

  if(!librdf_iterator_update_current_element(iterator))
    return NULL;

  return iterator->get_method(iterator->context, 
                              LIBRDF_ITERATOR_GET_METHOD_GET_CONTEXT);
}



/**
 * librdf_iterator_get_key - Get the key of the current object on the iterator
 * @iterator: the &librdf_iterator object
 *
 * Return value: The context or NULL if the iterator has finished.
 **/
void*
librdf_iterator_get_key(librdf_iterator* iterator) 
{
  if(iterator->is_finished)
    return NULL;

  if(!librdf_iterator_update_current_element(iterator))
    return NULL;

  return iterator->get_method(iterator->context, 
                              LIBRDF_ITERATOR_GET_METHOD_GET_KEY);
}



/**
 * librdf_iterator_get_value - Get the value of the current object on the iterator
 * @iterator: the &librdf_iterator object
 *
 * Return value: The context or NULL if the iterator has finished.
 **/
void*
librdf_iterator_get_value(librdf_iterator* iterator) 
{
  if(iterator->is_finished)
    return NULL;

  if(!librdf_iterator_update_current_element(iterator))
    return NULL;

  return iterator->get_method(iterator->context, 
                              LIBRDF_ITERATOR_GET_METHOD_GET_VALUE);
}



/**
 * librdf_iterator_add_map - Add a librdf_iterator mapping function
 * @iterator: the iterator
 * @map_function: the function to operate
 * @free_context: the function to use to free the context (or NULL)
 * @map_context: the context to pass to the map function
 * 
 * Adds an iterator mapping function which operates over the iterator to
 * select which elements are returned; it will be applied as soon as
 * this method is called.
 *
 * Several mapping functions can be added and they are applied in
 * the order given
 *
 * The mapping function should return non 0 to allow the element to be
 * returned.
 *
 * Return value: Non 0 on failure
 **/
int
librdf_iterator_add_map(librdf_iterator* iterator, 
                        librdf_iterator_map_handler map_function,
                        librdf_iterator_map_free_context_handler free_context,
                        void *map_context)
{
  librdf_iterator_map *map;
  
  if(!iterator->map_list) {
    iterator->map_list=librdf_new_list(iterator->world);
    if(!iterator->map_list)
      return 1;
  }

  map=(librdf_iterator_map*)LIBRDF_CALLOC(librdf_iterator_map, sizeof(librdf_iterator_map), 1);
  if(!map)
    return 1;

  map->fn=map_function;
  map->free_context=free_context;
  map->context=map_context;

  if(librdf_list_add(iterator->map_list, map)) {
    LIBRDF_FREE(librdf_iterator_map, map);
    return 1;
  }
  
  return 0;
}


#endif


/* TEST CODE */


#ifdef STANDALONE

/* one more prototype */
int main(int argc, char *argv[]);


#define ITERATOR_NODES_COUNT 6
#define NODE_URI_PREFIX "http://example.org/node"

int
main(int argc, char *argv[]) 
{
  librdf_world *world;
  librdf_uri* prefix_uri;
  librdf_node* nodes[ITERATOR_NODES_COUNT];
  int i;
  librdf_iterator* iterator;
  int count;
  
  char *program=argv[0];
	
  world=librdf_new_world();
  librdf_world_init_mutex(world);

  librdf_init_hash(world);
  librdf_init_uri(world);
  librdf_init_node(world);

  prefix_uri=librdf_new_uri(world, (const unsigned char*)NODE_URI_PREFIX);
  if(!prefix_uri) {
    fprintf(stderr, "%s: Failed to create prefix URI\n", program);
    return(1);
  }

  for(i=0; i < ITERATOR_NODES_COUNT; i++) {
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
  iterator=librdf_node_static_iterator_create(nodes, ITERATOR_NODES_COUNT);
  if(!iterator) {
    fprintf(stderr, "%s: Failed to createstatic  node iterator\n", program);
    return(1);
  }
  
  fprintf(stdout, "%s: Listing static node iterator\n", program);
  count=0;
  while(!librdf_iterator_end(iterator)) {
    librdf_node* i_node=(librdf_node*)librdf_iterator_get_object(iterator);
    if(!i_node) {
      fprintf(stderr, "%s: librdf_iterator_current return NULL when not end o fiterator\n", program);
      return(1);
    }

    fprintf(stdout, "%s: node %d is: ", program, count);
    librdf_node_print(i_node, stdout);
    fputc('\n', stdout);

    if(!librdf_node_equals(i_node, nodes[count])) {
      fprintf(stderr, "%s: static node iterator node %i returned unexpected node\n", program, count);
      librdf_node_print(i_node, stderr);
      fputs(" rather than ", stdout);
      librdf_node_print(nodes[count], stderr);
      fputc('\n', stdout);
      return(1);
    }
    
    librdf_iterator_next(iterator);
    count++;
  }

  librdf_free_iterator(iterator);

  if(count != ITERATOR_NODES_COUNT) {
    fprintf(stderr, "%s: Iterator returned %d nodes, expected %d\n", program,
            count, ITERATOR_NODES_COUNT);
    return(1);
  }

  fprintf(stdout, "%s: Static node iterator worked ok\n", program);


  fprintf(stdout, "%s: Freeing nodes\n", program);
  for (i=0; i<ITERATOR_NODES_COUNT; i++) {
    librdf_free_node(nodes[i]);
  }

  librdf_free_uri(prefix_uri);
  
  librdf_finish_node(world);
  librdf_finish_uri(world);
  librdf_finish_hash(world);

  LIBRDF_FREE(librdf_world, world);

  /* keep gcc -Wall happy */
  return(0);
}

#endif
