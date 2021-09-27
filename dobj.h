#ifndef _DOBJ_H
#define _DOBJ_H

#include <Python.h>

typedef struct 
{
    PyObject_HEAD

	void *memory; 
} DObj;

extern PyTypeObject dObjType;

#endif
