#ifndef _STREAMER_H
#define _STREAMER_H
#include <Python.h>

#include "framelist.h"

typedef struct {
    PyObject_HEAD
    /* Type-specific fields go here. */
    FrameList *m_record;
    unsigned m_index;
  	int m_startShift;
  	unsigned m_duration;
} Streamer;

void streamerInit(void);
void stSetCallback(void (*callback)(Streamer *));

extern PyTypeObject streamerType;

#endif