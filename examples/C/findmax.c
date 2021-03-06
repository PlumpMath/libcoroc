// Copyright 2016 Amal Cao (amalcaowei@gmail.com). All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE.txt file.

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include "libcoroc.h"

#define MAX 16

int* array;

void gen_array_elem(int* array, int size) {
  int i;

  srand(array);

  for (i = 0; i < size; i++) {
    array[i] = rand();
  }
}

bool check_max_elem(int* array, int size, int max) {
  int i;
  for (i = 0; i < size; i++) {
    if (array[i] > max) return false;
  }

  return true;
}

/* -- the slave entry -- */
int find_max(coroc_chan_t chan) {
  coroc_coroutine_t self = coroc_coroutine_self();
  int size, start, max[2];

  // get the size firstly
  coroc_chan_recv(chan, &size);
  if (size <= 0) coroc_coroutine_exit(-1);
  // get my tasks' start index
  coroc_chan_recv(chan, &start);

  max[0] = array[start];
  max[1] = array[start + 1];
  if (size > 1) {
    if (size > 2) {

      coroc_coroutine_t slave[2];
      coroc_chan_t ch[2];
      int sz[2], st[2];

      sz[0] = size / 2;
      st[0] = start;
      sz[1] = size - sz[0];
      st[1] = start + sz[0];

      int i = 0;
      for (; i < 2; ++i) {
        ch[i] = coroc_chan_allocate(sizeof(int), 0);
        slave[i] = coroc_coroutine_allocate(find_max, ch[i], "",
                                          TSC_COROUTINE_NORMAL,
                                          TSC_DEFAULT_PRIO, 0);
        coroc_chan_send(ch[i], &sz[i]);
        coroc_chan_send(ch[i], &st[i]);
      }

      coroc_chan_recv(ch[0], &max[0]);
      coroc_chan_recv(ch[1], &max[1]);
    }
    if (max[0] < max[1]) max[0] = max[1];
  }

  coroc_chan_send(chan, &max[0]);
  coroc_coroutine_exit(0);
}

/* -- the user entry -- */
int main(int argc, char** argv) {
  int max = 0, size = MAX, start = 0;
  array = malloc(size * sizeof(int));
  coroc_coroutine_t slave = NULL;
  coroc_chan_t chan = NULL;

  gen_array_elem(array, size);
  chan = coroc_chan_allocate(sizeof(int), 0);
  slave = coroc_coroutine_allocate(find_max, chan, "s", 
                                 TSC_COROUTINE_NORMAL,
                                 TSC_DEFAULT_PRIO, 0);

  coroc_chan_send(chan, &size);   // send the size of array
  coroc_chan_send(chan, &start);  // send the start index of array

  coroc_chan_recv(chan, &max);  // recv the result

  printf("The MAX element is %d\n", max);
  if (!check_max_elem(array, size, max)) printf("The answer is wrong!\n");

  free(array);
  coroc_coroutine_exit(0);
}
