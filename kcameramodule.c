#include "kcamera.h"
#include "streamer.h"
#include <Python.h>
#include <structmember.h>

typedef struct 
{
    PyObject_HEAD
	// parameters
	KcParams m_params;
	PyObject *m_resObject;	
	PyObject *m_modeObject;
	PyObject *m_streamerObject;

	// list of frames
} Camera;

Camera *g_camera = NULL;

void updateResObject(Camera *self)
{
    Py_XDECREF(self->m_resObject);
    self->m_resObject = Py_BuildValue("(II)", self->m_params.m_width, self->m_params.m_height);
}

static int parseArgs(Camera *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"resolution", "framerate", "duration", "mode", "brightness", "autoshutter", "awb", "awb_red", "awb_blue", "shutter_speed", "saturation", "max_latency", "mem_reserve", 
		"hflip", "vflip", "start_shift", NULL};
	PyObject *resObject = NULL, *modeObject = NULL;
	
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "|OIfOIppfffIIIppI", kwlist,
                                      &resObject, &self->m_params.m_framerate, &self->m_params.m_duration, &modeObject, &self->m_params.m_brightness, 
                                      &self->m_params.m_autoShutter, &self->m_params.m_awb, &self->m_params.m_awbRed, &self->m_params.m_awbBlue, 
									  &self->m_params.m_shutterSpeed, &self->m_params.m_saturation, &self->m_params.m_maxLatency, &self->m_params.m_memReserve, 
									  &self->m_params.m_hflip, &self->m_params.m_vflip, &self->m_params.m_startShift))
		return -1;
	if (resObject)
	{
		if (!PyArg_ParseTuple(resObject, "II", &self->m_params.m_width, &self->m_params.m_height))
			return -1;
		else
		{
			// decrement old object
			Py_XDECREF(self->m_resObject);
			// reuse object
            Py_XINCREF(resObject);
			self->m_resObject = resObject;
		}
	}
    else
        updateResObject(self);

	if (modeObject)
	{
        char *mode;
		if (!PyArg_Parse(modeObject, "s", &mode))
			return -1;
		else
		{
            strcpy(self->m_params.m_mode, mode);
			// decrement old object
			Py_XDECREF(self->m_modeObject);
			// reuse object
            Py_XINCREF(modeObject);

			self->m_modeObject = modeObject;
		}		
	}
    else
    {
        Py_XDECREF(self->m_modeObject);
        self->m_modeObject = Py_BuildValue("s", self->m_params.m_mode);
    }
		
    return 0;	
}

static PyObject *createStream(const char *filename)
{
	PyObject *args, *res;
	args = Py_BuildValue("(s)", filename);
	res = PyObject_CallObject((PyObject *)&streamerType, args);
	Py_XDECREF(args); // no longer needed

	return res;
}


static PyObject *camera_stream(Camera *self, PyObject *args, PyObject *kwds)
{
	if (parseArgs(self, args, kwds)<0)
	{
		PyErr_BadArgument();
		return NULL;
	}

	// create streamer object
	if (self->m_streamerObject==NULL)
		self->m_streamerObject = createStream("");
	else
		Py_INCREF(self->m_streamerObject);

	return self->m_streamerObject;
}

static PyObject *camera_record(Camera *self, PyObject *args, PyObject *kwds)
{
	PyObject *streamer;

	if (parseArgs(self, args, kwds)<0)
	{
		PyErr_BadArgument();
		return NULL;
	}

	// if there's already a recording, close it down and start a new one (can't have more than 1)
	if (kcGetRecord())
		kcStopRecord();

	// create record object
	if (kcStartRecord()<0)
	{
		PyErr_SetString(PyExc_AttributeError, "recorder is already recording");
		return NULL;
	}

	// start
	kcStart();

	// create streamer object
	streamer = createStream("");

	return streamer;
}

static PyObject *camera_getModes(Camera *self, PyObject *args)
{
	unsigned i, n;
	PyObject *tuple, *mode;
	const char **modes;
	modes = kcGetModes();

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

static PyObject *camera_load(Camera *self, PyObject *args)
{
    const char *filename;
	PyObject *streamer;

    if (!PyArg_ParseTuple(args, "s", &filename))
    {
        PyErr_SetString(PyExc_Exception, "load needs a filename (string)\n");
        return NULL;
    }

	streamer = createStream(filename);

	return streamer;
}


static void camera_dealloc(Camera *self)
{
	kcExit();
	Py_XDECREF(self->m_resObject);
	Py_XDECREF(self->m_modeObject);
    Py_TYPE(self)->tp_free((PyObject *)self);
    g_camera = NULL;
}

static PyObject *camera_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    Camera *self;

    if (g_camera) // only one object allowed
    {
    	PyErr_SetString(PyExc_Exception, "only one Camera object allowed");
    	return NULL;
    }

    self = (Camera *)type->tp_alloc(type, 0);
    if (!self)
    {
        PyErr_SetString(PyExc_Exception, "memory allocation");
        return NULL;
    }

	g_camera = self;
	return (PyObject *)self;
}

void streamCallback(Streamer *streamer)
{
	if (g_camera && streamer==(Streamer *)g_camera->m_streamerObject)
		g_camera->m_streamerObject = NULL;
}

static int camera_init(Camera *self, PyObject *args, PyObject *kwds)
{
    int res;
    unsigned width, height;
	self->m_streamerObject = NULL;
	stSetCallback(streamCallback);

    kcInit(&self->m_params);
    width = self->m_params.m_width;
    height = self->m_params.m_height;
    res = parseArgs(self, args, kwds);
    kcUpdateParams();
    // If the width or height was changed, we need to update the resolution object
    if (width!=self->m_params.m_width || height!=self->m_params.m_height)
        updateResObject(self);
    return res;
}


static int camera_setAttr(Camera *self, PyObject *attr_name, PyObject *v)
{
	unsigned long val;
    unsigned width=self->m_params.m_width, height=self->m_params.m_height;
	int res;
	PyObject *str = PyUnicode_AsEncodedString(attr_name, "utf-8", "~E~");
	const char *cstr = PyBytes_AS_STRING(str);

	if (v==NULL)
		return 	PyObject_GenericSetAttr((PyObject *)self, attr_name, v);	

	if (strcmp(cstr, "brightness")==0)
	{
		// limit brightness value between 0 and 100
		val = PyLong_AsUnsignedLong(v);
		if (val>100)
		{
			Py_XDECREF(v);
			v = PyLong_FromUnsignedLong(100);
		}
	}
	else if (strcmp(cstr, "mode")==0)
	{
		int i;
		char *mode = (char *)PyUnicode_1BYTE_DATA(v);
		const char **modes;
		modes = kcGetModes();

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
	else if (strcmp(cstr, "framerate")==0)
	{
		// limit framerate value
		val = PyLong_AsUnsignedLong(v);
		if (val < self->m_params.m_minFps)
		{
			Py_XDECREF(v);
			v = PyLong_FromUnsignedLong(self->m_params.m_minFps);
		}
		else if (val > self->m_params.m_maxFps)
		{
			Py_XDECREF(v);
			v = PyLong_FromUnsignedLong(self->m_params.m_maxFps);
		}
	}
	// update param value
	res = PyObject_GenericSetAttr((PyObject *)self, attr_name, v);

	// handle side-effects from parameter change
	kcUpdateParams();
    // If resolution has changed, update m_resObject
    if (width!=self->m_params.m_width || height!=self->m_params.m_height)
        updateResObject(self);
	return res;	
}

static PyMemberDef camera_members[] = 
{
	{"resolution", T_OBJECT, offsetof(Camera, m_resObject), READONLY, "frame resolution (width, height)"},
	{"framerate", T_UINT, offsetof(Camera, m_params) + offsetof(KcParams, m_framerate), 0, "framerate (frames/second)"},
	{"duration", T_FLOAT, offsetof(Camera, m_params) + offsetof(KcParams, m_duration), 0, "record duration (milliseconds)"},
	{"mode", T_OBJECT, offsetof(Camera,m_modeObject), 0, "video mode, use getmodes to get possible modes"},
	{"brightness", T_UINT, offsetof(Camera, m_params) + offsetof(KcParams, m_brightness), 0, "brightness setting (0 to 100)"},
	{"autoshutter", T_BOOL, offsetof(Camera, m_params) + offsetof(KcParams, m_autoShutter), 0, "auto shutter enable"},
	{"awb", T_BOOL, offsetof(Camera, m_params) + offsetof(KcParams, m_awb), 0, "auto white balance enable"},
	{"awb_red", T_FLOAT, offsetof(Camera, m_params) + offsetof(KcParams, m_awbRed), 0, "auto white balance red gain (awb must be False)"},
	{"awb_blue", T_FLOAT, offsetof(Camera, m_params) + offsetof(KcParams, m_awbBlue), 0, "auto white balance blue gain (awb must be False)"},
	{"shutter_speed", T_FLOAT, offsetof(Camera, m_params) + offsetof(KcParams, m_shutterSpeed), 0, "shutter speed in microseconds"},
	{"saturation", T_UINT, offsetof(Camera, m_params) + offsetof(KcParams, m_saturation), 0, "saturation (0 to 255)"},
	{"max_latency", T_UINT, offsetof(Camera, m_params) + offsetof(KcParams, m_maxLatency), 0, "maximum latency allowed in grab mode"},
	{"mem_reserve", T_UINT, offsetof(Camera, m_params) + offsetof(KcParams, m_memReserve), 0, "amount of memory to reserve when grabbing frames (megabytes)"},
	{"hflip", T_BOOL, offsetof(Camera, m_params) + offsetof(KcParams, m_hflip), 0, "flip horizontal orientation"},
	{"vflip", T_BOOL, offsetof(Camera, m_params) + offsetof(KcParams, m_vflip), 0, "flip vertical orientation"},
	{"start_shift", T_UINT, offsetof(Camera, m_params) + offsetof(KcParams, m_startShift), 0, "start recording shifted in microseconds"},
	{"measured_framerate", T_FLOAT, offsetof(Camera, m_params) + offsetof(KcParams, m_fps), READONLY, "current frames per second"},
	{"max_framerate", T_UINT, offsetof(Camera, m_params) + offsetof(KcParams, m_maxFps), READONLY, "maximum allowed frames per second"},
	{"min_framerate", T_UINT, offsetof(Camera, m_params) + offsetof(KcParams, m_minFps), READONLY, "minimum allowed frames per second"},
    {NULL}  // Sentinel 
};

static PyMethodDef camera_methods[] = {
	{"stream", (PyCFunction)camera_stream, METH_VARARGS|METH_KEYWORDS, "get live streamer object"},
	{"record", (PyCFunction)camera_record, METH_VARARGS|METH_KEYWORDS, "record frames"},
	{"getmodes", (PyCFunction)camera_getModes, METH_NOARGS, "get video modes"},	
    {"load",  (PyCFunction)camera_load, METH_VARARGS, "load stream from file"},
    {NULL}  // Sentinel 
};

static PyTypeObject cameraType = 
{
    PyVarObject_HEAD_INIT(NULL, 0)
    "kcamera.Camera",              // tp_name 
    sizeof(Camera),               // tp_basicsize 
    0,                             // tp_itemsize 
    (destructor)camera_dealloc,   // tp_dealloc 
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
    camera_setAttr,               // tp_setattro 
    0,                             // tp_as_buffer 
    Py_TPFLAGS_DEFAULT,            // tp_flags 
    "kcamera objects",             // tp_doc 
    0,                             // tp_traverse 
    0,                             // tp_clear 
    0,                             // tp_richcompare 
    0,                             // tp_weaklistoffset 
    0,                             // tp_iter 
    0,                             // tp_iternext 
    camera_methods,               // tp_methods 
    camera_members,               // tp_members 
    0,                             // tp_getset 
    0,                             // tp_base 
    0,                             // tp_dict 
    0,                             // tp_descr_get 
    0,                             // tp_descr_set 
    0,                             // tp_dictoffset 
    (initproc)camera_init,        // tp_init 
    0,                             // tp_alloc 
    camera_new                    // tp_new 
};


static struct PyModuleDef kcameraModule = {
   PyModuleDef_HEAD_INIT,
   "kcamera",   // name of module 
   "kcamera module", 
   -1,       // size of per-interpreter state of the module,
             // or -1 if the module keeps state in global variables. 
    NULL, NULL, NULL, NULL, NULL
};

PyMODINIT_FUNC PyInit_kcamera(void)
{
	PyObject *m;
		
	if (PyType_Ready(&cameraType)<0)
	    return NULL;
	streamerInit();

	m = PyModule_Create(&kcameraModule);
	if (m == NULL)
	    return NULL;

	Py_INCREF(&cameraType);
	PyModule_AddObject(m, "Camera", (PyObject *)&cameraType);

	return m;
}



