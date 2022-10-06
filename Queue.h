#ifndef QUEUE_H_   /* Include guard */
#define QUEUE_H_

int foo(int x);  /* An example function declaration */

#define QUEUESIZE 50
#include <pthread.h>
#include <sys/time.h>

typedef struct workFunction {
  void * (*work)(void *,double);
  void * arg;
  double timer;
  //struct timeval startwtime, endwtime;
} workFunc;

typedef struct {
  workFunc buf[QUEUESIZE];
  long head, tail;
  int full, empty;
  pthread_mutex_t *mut;
  pthread_cond_t *notFull, *notEmpty;
} queue;

void queueDelete (queue *q);

void queueAdd (queue *q, workFunc in);

void queueDel (queue *q, workFunc *out);

queue *queueInit (void);

#endif