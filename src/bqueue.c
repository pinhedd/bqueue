/*  bqueue.c - A blocking bqueue for multithreaded message passing
 Copyright (C) 2014  Richard Owen, rowen@ieee.org

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
/*==========================================
*  Includes
*==========================================*/
#include "bqueue.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
/*==========================================
*  Globals
*==========================================*/
#if defined (BQUEUE_WITH_ERRNO)
    __thread int bqueue_errno;
#endif
/*==========================================
*  Static Functions
*==========================================*/
#if defined (BQUEUE_WITH_ERRNO)
 static inline void bqueue_set_errno(int err)
 {
    bqueue_errno = err;
 }
 #else
 static inline void bqueue_set_errno(int err)
 {

 }
 #endif
/*==========================================
 *  Functions
 *==========================================*/
/*
 *  Function: bqueue_create
 *  Description: creates an empty blocking queue object
 *  Returns: pointer to the newly created queue object, or a null pointer if an error occurred
 */
bqueue_t_ptr bqueue_create(int max_elements)
{
    int err = EBQNOERR;
    bqueue_t_ptr bqueue = NULL;
    bqueue = malloc(sizeof(bqueue_t)); //Create a base list structure
    if (bqueue) //Make sure that the memory is allocated before attempting to operate on it
    {
        if (!err && (pthread_mutexattr_init(&(bqueue->lock_attr)) == RETURN_FAILURE));              //Initialize the mutex attribute structure
            err = EBQSYNCERR;
        if (!err && (pthread_mutex_init(&(bqueue->lock), &(bqueue->lock_attr)) == RETURN_FAILURE)); //Initialize the mutex
            err = EBQSYNCERR;
        if (!err && (sem_init(&(bqueue->elements_in_queue),0,0) == RETURN_FAILURE));                 //Initialize the semaphore used for pend synchronization. The queue is initially empty, so it defaults to 0
            err = EBQSYNCERR;
        if (!err && (sem_init(&(bqueue->elements_available),0,max_elements) == RETURN_FAILURE));     //Initialize the semaphore used for post synchronization. The queue is initially empty, so it defaults to max_elements
            err = EBQSYNCERR;
        bqueue->max_elements = max_elements;                     //Set the element size
    }
    else
        err = EBQNOMEM;
    if (err == EBQSYNCERR) //handle edge cases where synchronization objects failed to initialize
    {
        pthread_mutex_destroy(&(bqueue->lock));
        pthread_mutexattr_destroy(&(bqueue->lock_attr));
        sem_destroy(&(bqueue->elements_in_queue));
        sem_destroy(&(bqueue->elements_available));
        free(bqueue);
    }
    bqueue_set_errno(err);
    if (err)
        return NULL;
    else
        return bqueue;
}

/*
 *  Function: bqueue_destroy
 *  Description: destroys a a bqueue and all of the elements within
 *  Returns: RETURN_FAILURE if the operation fails, or RETURN_SUCCESS if the operation succeeds
 *  NB: This function frees up the memory allocated to storing queue elements but does not free up the memory allocated to storing
 *      messages. To prevent memory leaks this function will fail if there are any outstanding messages in the queue.
 */
int bqueue_destroy(bqueue_t_ptr __restrict__ bqueue)
{
    int err = EBQNOERR;
    int elements_in_queue = 0;
    bqueue_element_t_ptr next = NULL;
    bqueue_element_t_ptr forward = NULL;
    if (bqueue)
    {
        pthread_mutex_lock(&(bqueue->lock));
        sem_getvalue(&(bqueue->elements_in_queue),&elements_in_queue);
        if (elements_in_queue == 0)
        {
            next = bqueue->head;
            while (next != NULL)
            {
                forward = next->forward;
                free(next);
                next = forward;
            }
            sem_destroy(&(bqueue->elements_in_queue));
            sem_destroy(&(bqueue->elements_available));
            pthread_mutex_unlock(&(bqueue->lock));
            pthread_mutex_destroy(&(bqueue->lock));
            pthread_mutexattr_destroy(&(bqueue->lock_attr));
            free(bqueue);
        }
        else
        {
            err = EBQWOULDLEAK;
            pthread_mutex_unlock(&(bqueue->lock));
        }
    }
    else
        err = EBQNULLPTR;
    bqueue_set_errno(err);
    if (err)
        return RETURN_FAILURE;
    else
        return RETURN_SUCCESS;
}
/*
 *  Function: bqueue_post
 *  Description: inserts a new message to the end of the bqueue. Blocks if the queue is full. Will return once the message has been entered
 *  or after the queue is destroyed
 *  Returns: RETURN_FAILURE if an error occurred, or RETURN_SUCCESS if the operation succeeded.
 *  NOTE: it is up to the implementer to ensure that bqueue points to a valid queue object
 */
int bqueue_post(bqueue_t_ptr __restrict__ bqueue, void* data)
{
    int err = EBQNOERR;
    int queue_elements_in_queue = 0;
    bqueue_element_t_ptr new_element = NULL;
    if (bqueue)
    {
        if (sem_wait(&(bqueue->elements_available)) >= 0)
        {
            pthread_mutex_lock(&bqueue->lock);
            new_element = malloc(sizeof(bqueue_element_t)); //Create a new list element
            if (!new_element) //Check to see if the memory has been allocated before proceeding
            {
                err = EBQNOMEM; //An error occurred while allocating memory
            }
            else
            {
                new_element->data = data;
                new_element->backward = bqueue->tail; //Insert the new element into the list between element and element+1
                new_element->forward = NULL;
                bqueue->tail->forward = new_element;
                bqueue->tail = new_element;
                sem_post(&(bqueue->elements_in_queue));
            }
            if (err == EBQNOMEM)//Error Case1: memory allocation error occured, need to restore the semaphore to its original value
            {
                sem_post(&(bqueue->elements_available));
            }
            pthread_mutex_unlock(&bqueue->lock);
        }
        else
            err = EBQSYNCERR;   //This code will only be reached if sem_wait() terminates with an error. This can happen if the queue is destroyed
    }                           //while threads are waiting.
    else
        err = EBQNULLPTR;
    bqueue_set_errno(err);
    if (err)
        return RETURN_FAILURE;
    else
        return RETURN_SUCCESS;
}
/*
 *  Function: bqueue_trypost
 *  Description: inserts a new message to the end of the bqueue. Fails if the queue is full, does not block.
 *  Returns: RETURN_FAILURE if an error occurred, or RETURN_SUCCESS if the operation succeeded.
 *  NOTE: it is up to the implementer to ensure that bqueue points to a valid queue object
 */
int bqueue_trypost(bqueue_t_ptr __restrict__ bqueue, void* data)
{
    int err = EBQNOERR;
    int queue_elements_in_queue = 0;
    bqueue_element_t_ptr new_element = NULL;
    if (bqueue)
    {
        if (sem_trywait(&(bqueue->elements_available)) >= 0)
        {
            pthread_mutex_lock(&bqueue->lock);
            new_element = malloc(sizeof(bqueue_element_t)); //Create a new list element
            if (new_element) //Check to see if the memory has been allocated before proceeding
            {
                new_element->data = data;
                new_element->backward = bqueue->tail; //Insert the new element into the list between element and element+1
                new_element->forward = NULL;
                bqueue->tail->forward = new_element;
                bqueue->tail = new_element;
                sem_post(&(bqueue->elements_in_queue));
            }
            else
                err = EBQNOMEM; //An error occurred while allocating memory
            pthread_mutex_unlock(&bqueue->lock);
        }
        else
            err = EBQWOULDBLOCK;
    }
    else
        err = EBQNULLPTR;
    bqueue_set_errno(err);
    if (err)
        return RETURN_FAILURE; //Return null if an error occured
    else
        return RETURN_SUCCESS; //Return the address of the element otherwise
}
/*
 *  Function: bqueue_pend
 *  Description: removes a single message from the bqueue and advances the head to the next element
 *  Returns: a void pointer to the data of the element that was removed if the remove operation was successful, null otherwise.
 *  NOTE1: This function will block the thread if there are no messages in the queue. The function will return when a message has been entered
 *  NOTE2: If the pend operation is successful, the message object will be freed but the message data will not. Freeing the element data
 *         is the responsibility of the implementer.
 */
void* bqueue_pend(bqueue_t_ptr __restrict__ bqueue)
{
    int err = EBQNOERR;
    void* data = NULL;
    bqueue_element_t_ptr forward = NULL;
    if (bqueue)
    {
        if (sem_wait(&(bqueue->elements_in_queue)) >= 0)    //Wait until there is at least one element in the bqueue
        {   //Case1: Semaphore lock is acquired successfully. Sleeping thread is awoken
            pthread_mutex_lock(&(bqueue->lock));
            forward = bqueue->head->forward;
            data = bqueue->head->data;
            free(bqueue->head); //Free up the element itself
            bqueue->head = forward;
            sem_post(&(bqueue->elements_available));
            pthread_mutex_unlock(&(bqueue->lock));
        }   //Case2: Wait operation is aborted. Either a deadlock occurred (unlikely) or the bqueue was destroyed before another element was added
        else
            err = EBQSYNCERR;
    }
    else
        err = EBQNULLPTR;
    bqueue_set_errno(err);
    if (err)
        return NULL;
    else
        return data;
}
/*
 *  Function: bqueue_trypend
 *  Description: removes a single message from the bqueue and advances the head to the next element
 *  Returns: a void pointer to the data of the element that was removed if the remove operation was successful, null otherwise.
 *  NOTE1: unlike bqueue_fast_pend this function will not cause the thread to block. A return value of null may be indicative of either an error or
 *         an empty bqueue; bqueue_errno should be consulted to determine if an error occurred during execution.
 *  NOTE2: If the pend operation is successful, the message object will be freed but the message data will not. Freeing the element data
 *         is the responsibility of the implementer.
 */
void* bqueue_trypend(bqueue_t_ptr __restrict__ bqueue)
{
    int err = EBQNOERR;
    bqueue_element_t_ptr forward = NULL;
    void* data = NULL;
    if (bqueue)
    {
        if (sem_trywait(&(bqueue->elements_in_queue)) >= 0)//Attempt to lock the semaphore
        {   //Case1: Semaphore is locked successfully
            pthread_mutex_lock(&(bqueue->lock));
            forward = bqueue->head->forward;
            data = bqueue->head->data;
            free(bqueue->head); //Free up the element itself
            bqueue->head = forward;
            sem_post(&(bqueue->elements_available));
            pthread_mutex_unlock(&(bqueue->lock));
        }   //Case2: Semaphore cannot be locked
        else
            err = EWOULDBLOCK;
    }
    else
        err = EBQNULLPTR;
    bqueue_set_errno(err);
    return data;
}
/*
 *  Function: bqueue_peek
 *  Description: Attempts to return the value of the first element in the bqueue without removing it
 *  Returns: a void pointer to the data of the element that was removed if the remove operation was successful, null otherwise.
 *  NOTE1: This function will block the thread until at least one element is in the bqueue. A return value of NULL may be indicative of either an error or
 *         an empty bqueue. errno should be consulted to determine if an error occurred during processing.
  * NOTE2: If the pend operation is successful, the element will be freed but the element data will not. Freeing the element data
 *         is the responsibility of the implementer.
 */
void* bqueue_peek(bqueue_t_ptr __restrict__ bqueue)
{
    int err = EBQNOERR;
    void* data = NULL;
    if (bqueue)
    {
        if (sem_wait(&(bqueue->elements_in_queue)) >= 0)//Attempt to lock the semaphore
        {   //Case1: Semaphore is locked successfully
            pthread_mutex_lock(&(bqueue->lock));
            data = bqueue->head->data;
            pthread_mutex_unlock(&(bqueue->lock));
            sem_post(&(bqueue->elements_in_queue));//Post the semaphore as this is only a peek and thus makes no changes to the bqueue
        }   //Case2: Semaphore cannot be locked. Just leave data null
        else
            err = EBQSYNCERR;
    }
    else
        err = EBQNULLPTR;
    bqueue_set_errno(err);
    return data;
}
/*
 *  Function: bqueue_trypeek
 *  Description: Attempts to return the value of the first element in the bqueue without removing it
 *  Returns: a void pointer to the data of the element that was removed if the remove operation was successful, null otherwise.
 *  NOTE1: Unlike bqueue_peek this function will not block the thread. A return value of NULL may be indicative of either an error or
 *         an empty bqueue. errno should be consulted to determine if an error occurred during processing.
  * NOTE2: If the pend operation is successful, the element will be freed but the element data will not. Freeing the element data
 *         is the responsibility of the implementer.
 */
void* bqueue_trypeek(bqueue_t_ptr __restrict__ bqueue)
{
    int err = EBQNOERR;
    void* data = NULL;
    if (bqueue)
    {
        if (sem_trywait(&(bqueue->elements_in_queue)) >= 0)//Attempt to lock the semaphore
        {   //Case1: Semaphore is locked successfully
            pthread_mutex_lock(&(bqueue->lock));
            data = bqueue->head->data;
            pthread_mutex_unlock(&(bqueue->lock));
            sem_post(&(bqueue->elements_in_queue));//Post the semaphore as this is only a peek and thus makes no changes to the bqueue
        }   //Case2: Semaphore cannot be locked
        else
            err = EWOULDBLOCK;
    }
    else
        err = EBQNULLPTR;
    bqueue_set_errno(err);
    return data;
}
/*
 *  Function: bqueue_get_size
 *  Description: Determines the size of the bqueue
 *  Returns: an integer which is equal to the number of elements in the bqueue. This number is derived from a semaphore so it cannot be negative
 */
int bqueue_getsize(bqueue_t_ptr __restrict__ bqueue)
{
    int err = EBQNOERR;
    int val;
    if (bqueue)
    {
        pthread_mutex_lock(&(bqueue->lock));
        sem_getvalue(&(bqueue->elements_in_queue),&val);
        pthread_mutex_unlock(&(bqueue->lock));
        bqueue_set_errno(err);
    }
    else
        err = EBQNULLPTR;
    bqueue_set_errno(err);
    return val;
}
/*
 *  Function: bqueue_resize
 *  Description: resizes the maximum size of an existing bqueue
 *  Returns: RETURN_SUCCESS if the operation succeeded, RETURN_FAILURE otherwise
 */
int bqueue_resize(bqueue_t_ptr __restrict__ bqueue, int new_size)
{
    int err = EBQNOERR;
    int i = 0;
    int elements_in_queue = 0;
    if (bqueue)
    {
        pthread_mutex_lock(&(bqueue->lock));
        sem_getvalue(&(bqueue->elements_in_queue),&elements_in_queue);
        if (elements_in_queue < new_size)
        {
            if (new_size > bqueue->max_elements)
                for (i = 0; i< (new_size - bqueue->max_elements); i++)
                    sem_post(&(bqueue->elements_available));     //Increment or decrement the semaphore by the difference in the old size and the new size
            else
                for (i = 0; i< (bqueue->max_elements - new_size); i++)
                    sem_trywait(&(bqueue->elements_available));  //This is probably not very elegant
            bqueue->max_elements = new_size;
        }
        else
        {
            err = EBQWOULDLEAK;
        }
        pthread_mutex_unlock(&(bqueue->lock));
    }
    else
        err = EBQNULLPTR;
    bqueue_set_errno(err);
    if (err)
        return RETURN_FAILURE;
    else
        return RETURN_SUCCESS;
}
/*
 *  Function: bqueue_create
 *  Description: creates an empty blocking queue object
 *  Returns: pointer to the newly created queue object, or a null pointer if an error occurred
 */
bqueue_fast_t_ptr bqueue_fast_create(int max_elements)
{
    int err = EBQNOERR;
    int result = 0;
    bqueue_fast_t_ptr bqueue = NULL;
    if (max_elements > 0)
    {
        bqueue = malloc(sizeof(bqueue_fast_t)); //Create a base list structure
        if (!bqueue) //Make sure that the memory is allocated before attempting to operate on it
            err = EBQNOMEM; //An error occurred while allocating memory
        else
        {
            if (!err && (pthread_mutexattr_init(&(bqueue->lock_attr)) == RETURN_FAILURE));                           //Initialize the mutex attribute structure
                err = EBQSYNCERR;
            if (!err && (pthread_mutex_init(&(bqueue->lock), &(bqueue->lock_attr)) == RETURN_FAILURE));              //Initialize the mutex
                err = EBQSYNCERR;
            if (!err && (em_init(&(bqueue->elements_in_queue),0,0) == RETURN_FAILURE));                              //Initialize the semaphore used for pend synchronization. The queue is initially empty, so it defaults to 0
                err = EBQSYNCERR;
            if (!err && (em_init(&(bqueue->elements_available),0,max_elements) == RETURN_FAILURE));                  //Initialize the semaphore used for post synchronization. The queue is initially empty, so it defaults to max_elements
                err = EBQSYNCERR;
            if (!err)
            {
                bqueue->max_elements = max_elements;//Set the element size
                bqueue->queue_data = malloc(sizeof(void*) * max_elements);
                if (!(bqueue->queue_data))
                    err = EBQNOMEM;
            }
        }
    }
    else
        err = EBQINVALID;
    if (err == EBQNOMEM || err == EBQSYNCERR)
    {   //handle edge cases where parts of the queue may have been initialized
        pthread_mutex_destroy(&(bqueue->lock));
        pthread_mutexattr_destroy(&(bqueue->lock_attr));
        sem_destroy(&(bqueue->elements_in_queue));
        sem_destroy(&(bqueue->elements_available));
        free(bqueue);
    }
    bqueue_set_errno(err);
    if (err)
        return NULL;
    else
        return bqueue;
}
/*
 *  Function: bqueue_fast_destroy
 *  Description: destroys a a bqueue and all of the elements within
 *  Returns: RETURN_FAILURE if the operation fails, or RETURN_SUCCESS if the operation succeeds
 *  NB: This function frees up the memory allocated to storing queue elements but does not free up the memory allocated to storing
 *      messages. To prevent memory leaks this function will fail if there are any outstanding messages in the queue.
 */
int bqueue_fast_destroy(bqueue_fast_t_ptr __restrict__ bqueue)
{
    int err = EBQNOERR;
    int elements_in_queue = 0;
    if (bqueue)
    {
        sem_getvalue(&(bqueue->elements_in_queue),&elements_in_queue);
        if (elements_in_queue == 0)
        {
            pthread_mutex_lock(&(bqueue->lock));
            free(bqueue->queue_data);
            sem_destroy(&(bqueue->elements_in_queue));
            sem_destroy(&(bqueue->elements_available));
            pthread_mutex_unlock(&(bqueue->lock));
            pthread_mutex_destroy(&(bqueue->lock));
            pthread_mutexattr_destroy(&(bqueue->lock_attr));
            free(bqueue);
        }
        else
            err = EBQWOULDLEAK;
    }
    else
        err = EBQNULLPTR;
    bqueue_set_errno(err);
    if (err)
        return RETURN_FAILURE;
    else
        return RETURN_SUCCESS;
}
/*
 *  Function: bqueue_fast_post
 *  Description: inserts a new message to the end of the bqueue. Blocks if the queue is full. Will return once the message has been entered
 *  or after the queue is destroyed
 *  Returns: RETURN_FAILURE if an error occurred, or RETURN_SUCCESS if the operation succeeded.
 *  NOTE: it is up to the implementer to ensure that bqueue points to a valid queue object
 */
int bqueue_fast_post(bqueue_fast_t_ptr __restrict__ bqueue, void* data)
{
    int err = EBQNOERR;
    if (bqueue)
    {
        if (sem_wait(&(bqueue->elements_available)) >= 0)//Keep one element unused to ensure that start and end don't overlap unless the queue is empty
        {
            pthread_mutex_lock(&(bqueue->lock));
            bqueue->queue_data[bqueue->end] = data;
            if (bqueue->end == bqueue->max_elements - 1)
                bqueue->end = 0;
            else
                bqueue->end++;
            sem_post(&(bqueue->elements_in_queue));
            pthread_mutex_unlock(&(bqueue->lock));
        }
        else
            err = EBQSYNCERR;
    }
    else
        err = EBQNULLPTR;
    bqueue_set_errno(err);
    if (err)
        return RETURN_FAILURE;
    else
        return RETURN_SUCCESS;
}
/*
 *  Function: bqueue_fast_trypost
 *  Description: inserts a new message to the end of the bqueue. Fails if the queue is full, does not block.
 *  Returns: RETURN_FAILURE if an error occurred, or RETURN_SUCCESS if the operation succeeded.
 *  NOTE: it is up to the implementer to ensure that bqueue points to a valid queue object
 */
int bqueue_fast_trypost(bqueue_fast_t_ptr __restrict__ bqueue, void* data)
{
    int err = EBQNOERR;
    if (bqueue)
    {
        if (sem_trywait(&(bqueue->elements_available)) >= 0)//Keep one element unused to ensure that start and end don't overlap unless the queue is empty
        {
            pthread_mutex_lock(&(bqueue->lock));
            bqueue->queue_data[bqueue->end] = data;
            if (bqueue->end == bqueue->max_elements - 1)
                bqueue->end = 0;
            else
                bqueue->end++;
            sem_post(&(bqueue->elements_in_queue));
            pthread_mutex_unlock(&(bqueue->lock));
        }
        else
            err = EBQWOULDBLOCK;
    }
    else
        err = EBQNULLPTR;
    bqueue_set_errno(err);
    if (err)
        return RETURN_FAILURE;
    else
        return RETURN_SUCCESS;
}
/*
 *  Function: bqueue_fast_pend
 *  Description: removes a single message from the bqueue and advances the head to the next element
 *  Returns: a void pointer to the data of the element that was removed if the remove operation was successful, null otherwise.
 *  NOTE1: This function will block the thread if there are no messages in the queue. The function will return when a message has been entered. A
 *  return value of null indicates that an error occured; bequeue_errno should be consulted for the cause of the error.
 *  NOTE2: If the pend operation is successful, the message object will be freed but the message data will not. Freeing the element data
 *         is the responsibility of the implementer.
 */
void* bqueue_fast_pend(bqueue_fast_t_ptr __restrict__ bqueue)
{
    int err = EBQNOERR;
    void* data = NULL;
    if (bqueue)
    {
        if (sem_wait(&(bqueue->elements_in_queue)) >= 0)
        {
            pthread_mutex_lock(&(bqueue->lock));
            data = bqueue->queue_data[bqueue->start];
            if (bqueue->start == bqueue->max_elements - 1)
                bqueue->start = 0;
            else
                bqueue->start++;
            sem_post(&(bqueue->elements_available));
            pthread_mutex_unlock(&(bqueue->lock));
        }
        else
            err = EBQSYNCERR;
    }
    else
        err = EBQNULLPTR;
    bqueue_set_errno(err);
    if (err)
        return NULL;
    else
        return data;
}
/*
 *  Function: bqueue_fast_trypend
 *  Description: removes a single message from the bqueue and advances the head to the next element
 *  Returns: a void pointer to the data of the element that was removed if the remove operation was successful, null otherwise.
 *  NOTE1: unlike bqueue_fast_pend this function will not cause the thread to block. A return value of NULL may be indicative of either an error or
 *         an empty bqueue. bqueue_errno should be consulted to determine if an error occurred during execution.
 *  NOTE2: If the pend operation is successful, the message object will be freed but the message data will not. Freeing the element data
 *         is the responsibility of the implementer.
 */
void* bqueue_fast_trypend(bqueue_fast_t_ptr __restrict__ bqueue)
{
    int err = EBQNOERR;
    void* data = NULL;
    if (bqueue)
    {
        if (sem_trywait(&(bqueue->elements_in_queue)) >= 0)
        {
            pthread_mutex_lock(&(bqueue->lock));
            data = bqueue->queue_data[bqueue->start];
            if (bqueue->start == bqueue->max_elements - 1)
                bqueue->start = 0;
            else
                bqueue->start++;
            sem_post(&(bqueue->elements_available));
            pthread_mutex_unlock(&(bqueue->lock));
        }
        else
            err = EBQWOULDBLOCK;
    }
    else
        err = EBQNULLPTR;
    bqueue_set_errno(err);
    if (err)
        return NULL;
    else
        return data;
}
/*
 *  Function: bqueue_fast_peek
 *  Description: Attempts to return the value of the first element in the bqueue without removing it
 *  Returns: a void pointer to the data of the element that was peeked if the peek operation was successful, null otherwise.
 *  NOTE1: This function will block the thread if there are no messages in the queue. The function will return when a message has been entered. A
 *  return value of null indicates that an error occured. bequeue_errno should be consulted for the cause of the error.
 */
void* bqueue_fast_peek(bqueue_fast_t_ptr __restrict__ bqueue)
{
    int err = EBQNOERR;
    void* data = NULL;
    if (bqueue)
    {
        if (sem_wait(&(bqueue->elements_in_queue)) >= 0)
        {
            pthread_mutex_lock(&(bqueue->lock));
            data = bqueue->queue_data[bqueue->start];
            sem_post(&(bqueue->elements_in_queue));
            pthread_mutex_unlock(&(bqueue->lock));
        }
        else
            err = EBQSYNCERR;
    }
    else
        err = EBQNULLPTR;
    bqueue_set_errno(err);
    if (err)
        return NULL;
    else
        return data;
}
/*
 *  Function: bqueue_fast_trypeek
 *  Description: Attempts to return the value of the first element in the bqueue without removing it
 *  Returns: a void pointer to the data of the element that was removed if the remove operation was successful, null otherwise.
 *  NOTE1: Unlike bqueue_fast_peek this function will not block the thread. A return value of NULL may be indicative of either an error or
 *         an empty bqueue. errno should be consulted to determine if an error occurred during processing.
  * NOTE2: If the pend operation is successful, the element will be freed but the element data will not. Freeing the element data
 *         is the responsibility of the implementer.
 */
void* bqueue_fast_trypeek(bqueue_fast_t_ptr __restrict__ bqueue)
{
    int err = EBQNOERR;
    void* data = NULL;
    if (bqueue)
    {
        if (sem_trywait(&(bqueue->elements_in_queue)) >= 0)
        {
            pthread_mutex_lock(&(bqueue->lock));
            data = bqueue->queue_data[bqueue->start];
            sem_post(&(bqueue->elements_in_queue));
            pthread_mutex_unlock(&(bqueue->lock));
        }
        else
            err = EBQWOULDBLOCK;
    }
    else
        err = EBQNULLPTR;
    bqueue_set_errno(err);
    if (err)
        return NULL;
    else
        return data;
}
/*
 *  Function: bqueue_fast_getsize
 *  Description: Determines the size of the bqueue
 *  Returns: a positive integer which is equal to the number of elements in the bqueue, RETURN_FAILURE otherwise.
 */
int bqueue_fast_getsize(bqueue_fast_t_ptr __restrict__ bqueue)
{
    int err = 0;
    int size = 0;
    if (bqueue)
        size = bqueue_fast_size(bqueue);
    else
        err = EBQNULLPTR;
    bqueue_set_errno(err);
    if (err)
        return RETURN_FAILURE;
    else
        return size;
}
/*
 *  Function: bqueue_fast_resize
 *  Description: resizes the maximum size of an existing bqueue
 *  Returns: RETURN_SUCCESS if the operation succeeded, RETURN_FAILURE otherwise
 */
int bqueue_fast_resize(bqueue_fast_t_ptr __restrict__ bqueue, int new_size)
{
    int err = EBQNOERR;
    int i = 0;
    void* dest_data = NULL;;
    int elements_in_queue = 0;
    if (bqueue)
    {
        pthread_mutex_lock(&(bqueue->lock));
        elements_in_queue = bqueue_fast_size(bqueue);
        if (elements_in_queue < new_size)    //Case 1: existing queue size is smaller than the desired queue size, resize permitted
        {
            dest_data = malloc(new_size * sizeof(void*));
            if (dest_data)
            {
                if(bqueue->end > bqueue->start)     //Case 1: queue is not looped around the boundary
                {
                    memcpy(dest_data,(bqueue->queue_data + bqueue->start), elements_in_queue * sizeof(void*));   //Copy the ring buffer to the target and align it to the beginning

                }
                else                                //Case 2: queue is looped, two memory copies will need to be made
                {
                    memcpy(dest_data,(bqueue->queue_data + bqueue->start),(bqueue->max_elements - bqueue->start) * sizeof(void*));
                    memcpy(dest_data + (bqueue->max_elements - bqueue->start),(bqueue->queue_data),bqueue->end * sizeof(void*));
                }
                bqueue->start = 0;
                bqueue->end = elements_in_queue;
                free(bqueue->queue_data);           //Free the old buffer
                bqueue->queue_data = dest_data;     //Replace the old buffer with the new one
                if (new_size > bqueue->max_elements)    //Case 1: Queue size is increased
                    for (i = 0; i< (new_size - bqueue->max_elements);i++)
                        sem_post(&(bqueue->elements_available));     //Increment or decrement the semaphore by the difference in the old size and the new size
                else                                    //Case 2: Queue size is decreased
                    for (i = 0; i< (bqueue->max_elements - new_size);i++)
                        sem_trywait(&(bqueue->elements_available));  //This is probably not very elegant
                bqueue->max_elements = new_size;    //Adjust the size
            }
            else
                err = EBQNOMEM;
        }
        else
            err = EBQWOULDLEAK;
    }
    else
        err = EBQNULLPTR;
    pthread_mutex_unlock(&(bqueue->lock));
    bqueue_set_errno(err);
    if (err)
        return RETURN_FAILURE;
    else
        return RETURN_SUCCESS;

}
