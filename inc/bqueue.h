/*  bbqueue.h - A blocking bqueue for multithreaded message passing
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
 #if !defined (_BQUEUE_H)
    #define _BQUEUE_H
    #include <pthread.h>
    #include <semaphore.h>
    /*==========================================
     *  Defaults
     *==========================================*/
     #define EBQNOERR       0                       //No Error occured
     #define EBQSYNCERR     1                       //An error occured when initializing a mutex or semaphore
     #define EBQNOMEM       2                       //Memory could not be allocated for the bqueue element
     #define EBQNULLPTR     3                       //Null Pointer where null not permitted
     #define EBQWOULDBLOCK  4                       //Queue size has reached capacity
     #define EBQWOULDLEAK   5                       //There are more elements in the queue than the new size permits. Resizing would result in messages getting lost and memory leaking
     #define EBQINVALID     6                       //Parameter is invalid (eg, zero)
     #define EBQRESIZE      7                       //The operation failed because the queue was resized while the thread was waiting
     #define RETURN_SUCCESS 0
     #define RETURN_FAILURE -1
    /*==========================================
     *  Externals
     *==========================================*/
     #if defined (BQUEUE_WITH_ERRNO)
     extern __thread int bqueue_errno;
     #endif
    /*==========================================
     *  Macros
     *==========================================*/
     #define bqueue_fast_size(x) ((x->end >= x->start) ? (x->end - x->start):(x->start + x->end))
    /*==========================================
     *  Types
     *==========================================*/
    #if defined (__cplusplus)                   //C language linkage
        extern "C" {
    #endif
    typedef struct bqueue_element_t
    {
        void*                   data;                           //Pointer to the element data
        struct                  bqueue_element_t* forward;      //Pointer to the next element in the bqueue
        struct                  bqueue_element_t* backward;     //Pointer to the previous element in the bqueue
    }bqueue_element_t, *bqueue_element_t_ptr;

    typedef struct bqueue_t
    {
        pthread_mutex_t         lock;                           //Pthread mutex allows safe concurrent operation on the queue
        pthread_mutexattr_t     lock_attr;                      //Pthread mutex attributes
        sem_t                   elements_in_queue;              //Counts the number of elements in the queue
        sem_t                   elements_available;             //Counts the number of free spaces in the queue
        int                     max_elements;                   //Maximum number of elements in the queue
        bqueue_element_t_ptr head;                              //Pointer to the first element in the bqueue
        bqueue_element_t_ptr tail;                              //Pointer to the last element in the bqueue
    }bqueue_t, *bqueue_t_ptr;

    typedef struct bqueue_fast_t
    {
        pthread_mutex_t         lock;                           //Pthread mutex allows safe concurrent operation on the queue
        pthread_mutexattr_t     lock_attr;                      //Pthread mutex attributes
        sem_t                   elements_in_queue;              //Counts the number of elements in the queue
        sem_t                   elements_available;             //Counts the number of free spaces in the queue
        int                     start;                          //Index of the first element in the queue
        int                     end;                            //Index of the last element in the queue 
        int                     max_elements;                   //Maximum number of elements in the queue
        void**                  queue_data;                     //Pointer to queue data
    }bqueue_fast_t, *bqueue_fast_t_ptr;
    /*==========================================
     *  Prototypes
     *==========================================*/
    /* The basic implementation of bqueue uses a linked list to store message elements.
    It is spatially efficient but incurs additional overhead due to repeated calls to malloc */
    bqueue_t_ptr    bqueue_create(int max_elements);
    int             bqueue_destroy(bqueue_t_ptr __restrict__ bqueue);
    int             bqueue_post(bqueue_t_ptr __restrict__ bqueue, void* data);
    int             bqueue_trypost(bqueue_t_ptr __restrict__ bqueue, void* data);
    void*           bqueue_pend(bqueue_t_ptr __restrict__ bqueue);
    void*           bqueue_trypend(bqueue_t_ptr __restrict__ bqueue);
    void*           bqueue_peek(bqueue_t_ptr __restrict__ bqueue);
    void*           bqueue_trypeek(bqueue_t_ptr __restrict__ bqueue);
    int             bqueue_getsize(bqueue_t_ptr __restrict__ bqueue);
    int             bqueue_resize(bqueue_t_ptr __restrict__ bqueue, int new_size);
    /* The fast implementation of bqueue uses a ring buffer to store message elements.
    It is less spatially efficient than the basic implementation but incurs less overhead due to all buffer
    memory being allocated at once. As a consequence, the size of the list must be known ahead of time */
    
    bqueue_fast_t_ptr    bqueue_fast_create(int max_elements);
    int                  bqueue_fast_destroy(bqueue_fast_t_ptr __restrict__ bqueue);
    int                  bqueue_fast_post(bqueue_fast_t_ptr __restrict__ bqueue, void* data);
    int                  bqueue_fast_trypost(bqueue_fast_t_ptr __restrict__ bqueue, void* data);
    void*                bqueue_fast_pend(bqueue_fast_t_ptr __restrict__ bqueue);
    void*                bqueue_fast_trypend(bqueue_fast_t_ptr __restrict__ bqueue);
    void*                bqueue_fast_peek(bqueue_fast_t_ptr __restrict__ bqueue);
    void*                bqueue_fast_trypeek(bqueue_fast_t_ptr __restrict__ bqueue);
    int                  bqueue_fast_getsize(bqueue_fast_t_ptr __restrict__ bqueue);
    int                  bqueue_fast_resize(bqueue_fast_t_ptr __restrict__ bqueue, int new_size);

    #if defined (__cplusplus)
        }
    #endif

#endif /* _BQUEUE_H */
