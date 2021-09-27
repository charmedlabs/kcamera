#include <stdio.h>
#include "streamer.h"
#include "dobj.h"
#include "kcamera.h"
#include "structmember.h"
#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION
#include <numpy/ndarrayobject.h>

#define MAGIC 0xc1ab511c

void (*g_deallocCallback)(Streamer *) = NULL;

void stSetCallback(void (*callback)(Streamer *))
{
    g_deallocCallback = callback;
}

static void streamer_dealloc(Streamer *self)
{
    if (self->m_record)
    {
        if (self->m_record==kcGetRecord())
            kcStopRecord(); // see note below
        flistClear(self->m_record);
        free(self->m_record);
    }
    else
    {
        kcStop(); // stop streaming
        if (g_deallocCallback) // inform anyone who wants to know
            (*g_deallocCallback)(self);
    }
    Py_TYPE(self)->tp_free((PyObject *)self);
}

// Note:  We need to stop the recording if we lose the record object, otherwise the call
// to flistAppend() will segfault.  But if we do something like this:
// cam = kcamera.Camera()
// s = cam.record()
// s.stop()
// s = cam.record()
// The last line will both start and deallocate the old s.  This creates a race condition.  
// Python wont deallocate s first then start the record -- garbage collection runs as a
// separate thread, so it will both start the record and delete s simultaneously.  To 
// solve this we compare the record objec with the one in kcamera.  

static int streamer_load(FrameList *record, const char *filename)
{
    FILE *file;
    KcFrame *frame;
    unsigned val, res;

    printf("load %s\n", filename);
    file = fopen(filename, "r");
    if (file==NULL)
    {
        PyErr_SetString(PyExc_Exception, "unable to open file");
        return -1;  
    }
    while(1)
    {

        if (kcMemReserveExceeded())
        {
            PyErr_SetString(PyExc_Exception, "memory reserve has been exceeded");
            fclose(file);
            return -1;                       
        }
        res = fread((void *)&val, 1, 4, file);
        if (res!=4 || val!=MAGIC)
        {
            if (feof(file))
            {
                printf("done %d %x\n", res, val);
                fclose(file);
                return 0;
            }
            else
            {
                fclose(file);
                PyErr_SetString(PyExc_Exception, "error parsing file, magic number is missing");
                return -1;
            }           
        }

        res = fread((void *)&val, 1, 4, file);
        if (res!=4)
        {
            PyErr_SetString(PyExc_Exception, "error parsing file");
            fclose(file);
            return -1;           
        }

        frame = (KcFrame *)malloc(val);
        if (frame==NULL)
        {
            PyErr_SetString(PyExc_Exception, "out of memory");
            fclose(file);
            return -1;           
        }
        printf("loading frame %d %d\n", record->m_len, val);
        res = fread((void *)frame, 1, val, file);
        if (res!=val)
        {
            PyErr_SetString(PyExc_Exception, "error parsing frame");
            fclose(file);
            return -1;           
        }
        pthread_mutex_lock(&record->m_mutex);            
        flistAppend(record, frame);
        pthread_mutex_unlock(&record->m_mutex);            
    }
}

static int streamer_init(Streamer *self, PyObject *args, PyObject *kwds)
{
    const char *filename;

    if (PyArg_ParseTuple(args, "s", &filename))
    {
        self->m_startShift = 0;
        self->m_duration = 0;
        if (strlen(filename)==0)
        {
            self->m_record = kcGetRecord();
            if (self->m_record)
            {
                self->m_startShift = self->m_record->m_startShift; // copy startShift over
                self->m_duration = self->m_record->m_duration; // copy duration over
            }
            return 0;
        }
        else
        {
            self->m_record = (FrameList *)malloc(sizeof(FrameList));
            flistInit(self->m_record, 0, 0);
            return streamer_load(self->m_record, filename);
        }
    }
    else
        return -1;
}


static PyObject *streamer_frame(Streamer* self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"type", NULL};
    KcFrame *frame=NULL;
    PyObject *object, *array, *tuple;
    int dims[3];
    char *type="";
    pthread_mutex_t *mutex=NULL; 
    FrameList *record;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|s", kwlist, &type))
    {
        PyErr_BadArgument();
        return NULL;
    }
    
    if (self->m_record)
    {
        self->m_index = self->m_record->m_readIndex;
        kcWaitNextRecordFrame(self->m_record);
        mutex = &self->m_record->m_mutex;
        pthread_mutex_lock(mutex);
        frame = flistNext(self->m_record);
        if (frame)
            frame = kcCopyFrame(frame);
    }
    else
    {
        // if we're recording, send the most recently recorded frame
        record = kcGetRecord();
        if (record)
        {
            kcWaitLastRecordFrame();
            mutex = &record->m_mutex;
            pthread_mutex_lock(mutex);
            if (record->m_back)
                frame = record->m_back->m_frame;      
            frame = kcCopyFrame(frame);
        }

        if (frame==NULL)
            frame = kcNextStreamFrame();
    }

    if (frame==NULL)
    {
        if (mutex)
            pthread_mutex_unlock(mutex);            
        Py_RETURN_NONE;
    }

    // create new deallocation object
    object = (PyObject *)PyObject_New(DObj, &dObjType);
    // copy frame pointer into deallocation object
    ((DObj *)object)->memory = frame;
    if (!strcmp(type, "bytes"))
        array = PyBytes_FromStringAndSize((char *)frame->m_data, frame->m_width*frame->m_height*3);
    else
    {
        dims[0] = frame->m_height;
        dims[1] = frame->m_width;
        dims[2] = 3;
        array = PyArray_SimpleNewFromData(3, dims, NPY_UINT8, frame->m_data); 
    }

    // attach deallocation object to array object so memory gets deallocated when array gets deallocated
    PyArray_SetBaseObject((PyArrayObject *)array, object);

    tuple = PyTuple_New(3);
    PyTuple_SetItem(tuple, 0, array);
    PyTuple_SetItem(tuple, 1, PyLong_FromLongLong(frame->m_pts));
    PyTuple_SetItem(tuple, 2, PyLong_FromLong(self->m_index));
    if (self->m_record==NULL)
        self->m_index++;

    if (mutex)
        pthread_mutex_unlock(mutex);            

    return tuple;
}

static PyObject *streamer_seek(Streamer* self, PyObject *args)
{
    if (self->m_record)
    {
        unsigned index;
        if (!PyArg_ParseTuple(args, "I", &index))
        {
            PyErr_SetString(PyExc_Exception, "seek needs an integer argument\n");
            return NULL;
        }
    
        pthread_mutex_lock(&self->m_record->m_mutex);            
        flistSeek(self->m_record, index);
        pthread_mutex_unlock(&self->m_record->m_mutex);            
    }
    else
    {
        PyErr_SetString(PyExc_Exception, "cannot seek within this type of stream");
        return NULL;
    }        
    Py_RETURN_NONE;
}


static PyObject *streamer_stop(Streamer* self)
{
    if (self->m_record)
        kcStopRecord();
    else 
        kcStop();

    return PyLong_FromLong(0);  
}

static PyObject *streamer_start(Streamer* self)
{
    if (self->m_record)
        self->m_record->m_recording = 1; // set it to true to start recording

    return PyLong_FromLong(0);  
}


static PyObject *streamer_recording(Streamer* self)
{
    if (self->m_record)
        return PyLong_FromLong(self->m_record->m_recording);
    else
        return PyLong_FromLong(0);
}

static PyObject *streamer_len(Streamer* self)
{
    if (self->m_record) 
        return PyLong_FromLong(self->m_record->m_len);
    else
        return PyLong_FromLong(-1);
}

static PyObject *streamer_index(Streamer* self)
{
    if (self->m_record) 
        return PyLong_FromLong(self->m_record->m_readIndex);
    else
        return PyLong_FromLong(self->m_index);           
}

static PyObject *streamer_progress(Streamer* self)
{
    unsigned res;

    if (self->m_record && self->m_record!=kcGetRecord()) // if we're a record and we're not recording
    {
        // play progress
        pthread_mutex_lock(&self->m_record->m_mutex);
        if (self->m_record->m_len!=0)
            res = 100*self->m_record->m_readIndex/self->m_record->m_len;
        else
            res = 0;  
        pthread_mutex_unlock(&self->m_record->m_mutex);
    }
    else
        // recording progress
        res = kcRecordProgress(); // this routine locks the mutex

    return PyLong_FromLong(res);
}

static PyObject *streamer_time(Streamer* self)
{
    double time = 0.0;

    if (self->m_record) 
    {
        pthread_mutex_lock(&self->m_record->m_mutex);            
        if (self->m_record->m_front && self->m_record->m_back)
        {
            if (self->m_record!=kcGetRecord() && self->m_record->m_readNode) // if we're a record and we're not recording
            {
                FrameNode *node = *self->m_record->m_readNode;
                if (node)
                    time = (node->m_frame->m_pts - self->m_record->m_front->m_frame->m_pts)/1000000.0;
            }
            else
            {
                if (self->m_record->m_startShift>0 && self->m_record->m_recording==-1)
                    time = ((int64_t)self->m_record->m_front->m_frame->m_pts - self->m_record->m_t0 - self->m_record->m_startShift)/1000000.0;
                else
                    time = (self->m_record->m_back->m_frame->m_pts - self->m_record->m_front->m_frame->m_pts)/1000000.0;
            }
        }
        else
            time = -self->m_record->m_startShift/1000000.0;            
        pthread_mutex_unlock(&self->m_record->m_mutex);            
    }
  
    return PyFloat_FromDouble(time);
}


static PyObject *save(FrameList *record, const char *filename)
{
    FILE *file;
    KcFrame *frame;
    unsigned len = 0;
    unsigned size;
    unsigned magic = MAGIC;

    //printf("save!! %x %x\n", PyEval_ThreadsInitialized(), PyGILState_Check());
    file = fopen(filename,  "w");
    if (file==NULL)
    {
        PyErr_SetString(PyExc_Exception, "unable to open file");
        return NULL;
    }

    pthread_mutex_lock(&record->m_mutex);            
    flistSeek(record, 0);
    pthread_mutex_unlock(&record->m_mutex);            
    while(1)
    {
        pthread_mutex_lock(&record->m_mutex);            
        frame = flistNext(record);
        if (frame==NULL)
        {
            pthread_mutex_unlock(&record->m_mutex);            
            break;
        }

        printf("saving frame %d\n", record->m_readIndex-1);
        size = kcSizeofFrame(frame);
        //Py_BEGIN_ALLOW_THREADS // this is supposed to allow other python threads to run, but it doesn't work for some reason, see https://docs.python.org/3/c-api/init.html
        len += fwrite((void *)&magic, 1, 4, file);
        len += fwrite((void *)&size, 1, 4, file);
        len += fwrite((void *)frame, 1, size, file);
        pthread_mutex_unlock(&record->m_mutex);            
        //Py_END_ALLOW_THREADS 
        if (ferror(file)!=0)
        {
            PyErr_SetString(PyExc_Exception, "error while writing file");
            fclose(file);
            return NULL;
        }
    }
    fclose(file);
    printf("done %d\n", len);

    return PyLong_FromLong(len);  
}


static PyObject *streamer_save(Streamer* self, PyObject *args)
{
    if (self->m_record)
    {
        const char *filename;
        if (!PyArg_ParseTuple(args, "s", &filename))
        {
            PyErr_SetString(PyExc_Exception, "save needs a filename (string)");
            return NULL;
        }
        return save(self->m_record, filename);
    }
    else
    {
        PyErr_SetString(PyExc_Exception, "cannot save this type of stream");
        return NULL;
    }       
}

static int streamer_setAttr(Streamer *self, PyObject *attr_name, PyObject *v)
{
    int res;
    PyObject *str = PyUnicode_AsEncodedString(attr_name, "utf-8", "~E~");
    const char *cstr = PyBytes_AS_STRING(str);

    if (v==NULL)
        return  PyObject_GenericSetAttr((PyObject *)self, attr_name, v);    

    if (strcmp(cstr, "start_shift")==0)
    {
        long val;
        val = PyLong_AsLong(v);
        self->m_startShift = val;
        if (self->m_record)
        {
            pthread_mutex_lock(&self->m_record->m_mutex);
            self->m_record->m_startShift = val;
            pthread_mutex_unlock(&self->m_record->m_mutex);
        }
    }
    else if (strcmp(cstr, "duration")==0)
    {
        unsigned long val;
        // limit brightness value between 0 and 100
        val = PyLong_AsUnsignedLong(v);
        self->m_duration = val;
        if (self->m_record)
        {
            pthread_mutex_lock(&self->m_record->m_mutex);
            self->m_record->m_duration = val;
            pthread_mutex_unlock(&self->m_record->m_mutex);
        }
    }

    // update param value
    res = PyObject_GenericSetAttr((PyObject *)self, attr_name, v);

    return res; 
}

static PyMethodDef streamer_methods[] = {
    {"frame",  (PyCFunction)streamer_frame, METH_VARARGS|METH_KEYWORDS, "get next frame"},
    {"seek",  (PyCFunction)streamer_seek, METH_VARARGS, "seek within stream"},
    {"stop",  (PyCFunction)streamer_stop, METH_NOARGS, "stop recording"},
    {"start",  (PyCFunction)streamer_start, METH_NOARGS, "start recording"},
    {"recording",  (PyCFunction)streamer_recording, METH_NOARGS, "returns True if stream is recording"},
    {"len",  (PyCFunction)streamer_len, METH_NOARGS, "number of frames in stream"},
    {"index",  (PyCFunction)streamer_index, METH_NOARGS, "frame index"},
    {"progress",  (PyCFunction)streamer_progress, METH_NOARGS, "recording or play progress, 0 to 100"},
    {"time",  (PyCFunction)streamer_time, METH_NOARGS, "recording or play time in seconds"},
    {"save",  (PyCFunction)streamer_save, METH_VARARGS, "save stream to file"},
    {NULL}  // Sentinel 
};


static PyMemberDef streamer_members[] = 
{

    {"start_shift", T_INT, offsetof(Streamer, m_startShift), 0, "start recording shifted in microseconds"},
    {"duration", T_UINT, offsetof(Streamer, m_duration), 0, "duration of record length in microseconds"},
    {NULL}  // Sentinel 
};


PyTypeObject streamerType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "kcamera.streamer",            // tp_name 
    sizeof(Streamer),              // tp_basicsize 
    0,                             // tp_itemsize 
    (destructor)streamer_dealloc,  // tp_dealloc 
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
    streamer_setAttr,               // tp_setattro 
    0,                             // tp_as_buffer 
    Py_TPFLAGS_DEFAULT,            // tp_flags 
    "streamer objects",            // tp_doc 
    0,                             // tp_traverse 
    0,                             // tp_clear 
    0,                             // tp_richcompare 
    0,                             // tp_weaklistoffset 
    0,                             // tp_iter 
    0,                             // tp_iternext 
    streamer_methods,              // tp_methods 
    streamer_members,              // tp_members 
    0,                             // tp_getset 
    0,                             // tp_base 
    0,                             // tp_dict 
    0,                             // tp_descr_get 
    0,                             // tp_descr_set 
    0,                             // tp_dictoffset 
    (initproc)streamer_init,       // tp_init
    0, 
    PyType_GenericNew 
};

void streamerInit(void)
{
    if (PyType_Ready(&dObjType)<0)
        return; 
    if (PyType_Ready(&streamerType)<0)
        return;
    import_array();
}
