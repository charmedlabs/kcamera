#include <Python.h>
#include <structmember.h>
#include <pthread.h>
#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION
#include <numpy/ndarrayobject.h>
#include "kencoder.h"

#define FRAME_IN_TABLE_SIZE   32 // way more than needed


typedef struct 
{
    PyObject_HEAD
    PyObject *m_resObject;  
    PyObject *m_modeObject;
    // parameters
    KeParams m_params;
    uint64_t m_count;
} Encoder;

Encoder *g_encoder = NULL;

static int parseArgs(Encoder *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"resolution", "bitrate", "mode", NULL};
    PyObject *resObject = NULL, *modeObject = NULL;
    
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|OIO", kwlist,
                                    &resObject, &self->m_params.m_bitrate, 
                                    &modeObject))
        return -1;
    if (resObject)
    {
        if (!PyArg_ParseTuple(resObject, "II", &self->m_params.m_width, &self->m_params.m_height))
        {
            PyErr_SetString(PyExc_Exception, "resolution is (width, height) tuple\n");
            return -1;
        }
        else
        {
            // decrement old object
            Py_XDECREF(self->m_resObject);
            // reuse the returned object
            Py_XINCREF(resObject);
            self->m_resObject = resObject;
        }
    }
    if (modeObject)
    {
        char *mode;
        if (!PyArg_Parse(modeObject, "s", &mode))
        {
            PyErr_SetString(PyExc_Exception, "unable to set mode\n");
            return -1;
        }
        else
        {
            strcpy(self->m_params.m_mode, mode);
            // decrement old object
            Py_XDECREF(self->m_modeObject);
            // reuse the returned object
            Py_XINCREF(modeObject);
            self->m_modeObject = modeObject;
        }       
    }
        
    return 0;   
}


static PyObject *encoder_encode(Encoder *self, PyObject *args)
{
    PyObject *frame, *eframe;
    KeOutput output;    
    uint32_t index, width, height, size;
    PyArray_Descr *desc;
    uint8_t *mem;
    int *dims;
    PyThreadState *save; 

    if (!PyArg_ParseTuple(args, "O", &frame)) 
        return NULL;
    desc = PyArray_DTYPE(frame);
    if (desc->type_num!=NPY_UINT8)
    {
        PyErr_SetString(PyExc_Exception, "only arrays of int8 are allowed\n");
        return NULL;
    }    
    dims = PyArray_DIMS(frame);
    mem = PyArray_DATA(frame);
    width = dims[1];
    height = dims[0];
    size = width*height*dims[3];
    index = keEncodeIn(mem, size, width,  height, g_encoder->m_count);
    if (index>=FRAME_IN_TABLE_SIZE)
    {
        PyErr_SetString(PyExc_Exception, "index value is too large\n");
        return NULL;
    }    
    save = PyEval_SaveThread(); // release GIL
    keEncodeOut(&output);
    PyEval_RestoreThread(save); // reacquire GIL
    if (!output.bytes_used)
    {
        PyErr_SetString(PyExc_Exception, "encoder returned null packet\n");
        return NULL;        
    }
    if (output.timestamp_us!=g_encoder->m_count)
    {
        PyErr_SetString(PyExc_Exception, "encoder returned stale frame\n");
        return NULL;        
    }
    g_encoder->m_count++;
    // create output object
    eframe = PyBytes_FromStringAndSize((char *)output.mem, output.bytes_used);
    // tell encoder that we're done with the buffer memory
    keEncodeOutDone(&output);
    return eframe;
}


static PyObject *encoder_getModes(Encoder *self, PyObject *args)
{
    unsigned i, n;
    PyObject *tuple, *mode;
    const char **modes;
    modes = keGetModes();

    // count the number of modes
    for (n=0; modes[n]; n++);

    tuple = PyTuple_New(n);

    for (i=0; i<n; i++)
    {
        mode = PyUnicode_FromString(modes[i]);
        PyTuple_SetItem(tuple, i, mode);
    }
    
    return tuple;
}


static void encoder_dealloc(Encoder *self)
{
    keExit();

    Py_XDECREF(self->m_modeObject);
    Py_XDECREF(self->m_resObject);
    Py_TYPE(self)->tp_free((PyObject *)self);
    g_encoder = NULL;
}

static PyObject *encoder_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    Encoder *self;

    if (g_encoder) // only one object allowed
    {
        PyErr_SetString(PyExc_Exception, "only one Encoder object allowed");
        return NULL;
    }

    self = (Encoder *)type->tp_alloc(type, 0);
    if (!self)
    {
        PyErr_SetString(PyExc_Exception, "memory allocation");
        return NULL;
    }

    g_encoder = self;
    return (PyObject *)self;
}

static int encoder_init(Encoder *self, PyObject *args, PyObject *kwds)
{
    int res;

    self->m_params.m_width = 640;
    self->m_params.m_height = 480;
    self->m_params.m_bitrate = 3000000;
    strcpy(self->m_params.m_mode, "default");
    res = parseArgs(self, args, kwds);
    if (res<0)
        return res;
    return keInit(&self->m_params);
}


static int encoder_setAttr(Encoder *self, PyObject *attr_name, PyObject *v)
{
    int res;
    PyObject *str = PyUnicode_AsEncodedString(attr_name, "utf-8", "~E~");
    const char *cstr = PyBytes_AS_STRING(str);

    if (v==NULL)
        return  PyObject_GenericSetAttr((PyObject *)self, attr_name, v);    

    if (strcmp(cstr, "mode")==0)
    {
        int i;
        char *mode = (char *)PyUnicode_1BYTE_DATA(v);
        const char **modes;
        modes = keGetModes();

        for (i=0; modes[i]; i++)
            if (strcmp(mode, modes[i])==0)
                break;
        if (modes[i]==NULL)
        {
            PyErr_SetString(PyExc_AttributeError, "invalid mode");
            return -1;
        }
        strcpy(self->m_params.m_mode, modes[i]);
    }
    else if (strcmp(cstr, "resolution")==0)
    {
        if (!PyArg_ParseTuple(v, "II", &self->m_params.m_width, &self->m_params.m_height))
            return -1;
    }
    // update param value
    res = PyObject_GenericSetAttr((PyObject *)self, attr_name, v);

    // handle side-effects from parameter change
    keUpdateParams();
    return res; 
}

static PyMemberDef encoder_members[] = 
{
    {"resolution", T_OBJECT, offsetof(Encoder, m_resObject), 0, "frame resolution (width, height)"},
    {"bitrate", T_UINT, offsetof(Encoder, m_params) + offsetof(KeParams, m_bitrate), 0, "framerate (frames/second)"},
    {"mode", T_OBJECT, offsetof(Encoder, m_modeObject), 0, "encoding mode, use getmodes to get possible modes"},
    {NULL}  // Sentinel 
};

static PyMethodDef encoder_methods[] = {
    {"encode", (PyCFunction)encoder_encode, METH_VARARGS, "encode frame"},
    {"getmodes", (PyCFunction)encoder_getModes, METH_NOARGS, "get encoding modes"}, 
    {NULL}  // Sentinel 
};

static PyTypeObject encoderType = 
{ 
    PyVarObject_HEAD_INIT(NULL, 0)
    "kencoder.Encoder",              // tp_name 
    sizeof(Encoder),               // tp_basicsize 
    0,                             // tp_itemsize 
    (destructor)encoder_dealloc,   // tp_dealloc 
    0,                             // tp_print 
    0,                             // tp_getattr 
    0,                             // tp_setattr 
    0,                             // tp_reserved 
    0,                             // tp_repr 
    0,                             // tp_as_number 
    0,                             // tp_as_sequence 
    0,                             // tp_as_mapping 
    0,                             // tp_hash  
    0,                             // tp_call 
    0,                             // tp_str 
    0,                             // tp_getattro 
    encoder_setAttr,               // tp_setattro 
    0,                             // tp_as_buffer 
    Py_TPFLAGS_DEFAULT,            // tp_flags 
    "kencoder objects",             // tp_doc 
    0,                             // tp_traverse 
    0,                             // tp_clear 
    0,                             // tp_richcompare 
    0,                             // tp_weaklistoffset 
    0,                             // tp_iter 
    0,                             // tp_iternext 
    encoder_methods,               // tp_methods 
    encoder_members,               // tp_members 
    0,                             // tp_getset 
    0,                             // tp_base 
    0,                             // tp_dict 
    0,                             // tp_descr_get 
    0,                             // tp_descr_set 
    0,                             // tp_dictoffset 
    (initproc)encoder_init,        // tp_init 
    0,                             // tp_alloc 
    encoder_new                    // tp_new 
};


static struct PyModuleDef kencoderModule = {
   PyModuleDef_HEAD_INIT,
   "kencoder",   // name of module 
   "kencoder module", 
   -1,       // size of per-interpreter state of the module,
             // or -1 if the module keeps state in global variables. 
    NULL, NULL, NULL, NULL, NULL
};

PyMODINIT_FUNC PyInit_kencoder(void)
{
    PyObject *m;
        
    if (PyType_Ready(&encoderType)<0)
        return NULL;

    m = PyModule_Create(&kencoderModule);
    if (m == NULL)
        return NULL;

    Py_INCREF(&encoderType);
    PyModule_AddObject(m, "Encoder", (PyObject *)&encoderType);

    return m;
}



