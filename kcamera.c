#include <stdint.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <asm/resource.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <errno.h>
#include "kcamera.h"
#define KC_DEFAULT_MAX_LATENCY      100000 // microseconds
#define KC_FPS_FILTER               0.2    // 
#define KC_MAX_FRAME_TIMEOUT        5000000 // microseconds


void kcSetMode(void);

typedef struct 
{	
	unsigned m_run;
	unsigned m_stopping;
	uint64_t m_lastPts;
	pthread_t m_thread;	
	pthread_cond_t m_cond;
	pthread_mutex_t m_frameMutex;
	pthread_mutex_t m_paramsMutex;
	KcFrame *m_frame;
	KcParams m_currParams;
	int64_t m_ptsOffset;
	uint64_t m_pts;
	uint32_t m_frameTimer;
	FrameList *m_record;
} KcState;	


KcParams *g_params = NULL;
static KcState g_state; 

int memfree(void)
{
#if 0 // sysinfo doesn't provide a complete or reliable picture of memory usage.... 
	struct sysinfo info;
	sysinfo(&info);

	printf("%d %d %d %d %d %d\n", info.freeram, info.bufferram, info.mem_unit, info.totalram, info.totalhigh, info.freehigh);
	return (info.freeram + info.bufferram)*info.mem_unit;
#else
    unsigned mfree, mtotal;
    FILE *meminfo = fopen("/proc/meminfo", "r");
    if(meminfo == NULL)
        printf("error\n");

    char line[256];
    while(fgets(line, sizeof(line), meminfo))
    {
        if(sscanf(line, "MemTotal: %d kB", &mtotal)==1)
            continue;
        if(sscanf(line, "MemAvailable: %d kB", &mfree)==1)
        {
            fclose(meminfo);
            return mfree*100/mtotal;
        }
    }

    fclose(meminfo);
    return -1;

#endif
}

static float maxShutterSpeed(unsigned fps) // shutter speed in microseconds
{
	return 1.0/fps;
}

	
static void *vidThread(void *arg)
{	
	kcStartCameraLoop();
	return NULL;
}

void kcInit(KcParams *params)
{
	pthread_mutex_init(&g_state.m_frameMutex, NULL);
	pthread_mutex_init(&g_state.m_paramsMutex, NULL);
	pthread_cond_init(&g_state.m_cond, NULL);
	pthread_mutex_lock(&g_state.m_paramsMutex);

	g_params = params;

	g_params->m_width = 640;
	g_params->m_height = 480;
	g_params->m_framerate = 30;
	g_params->m_duration = 0;
	strcpy(g_params->m_mode, KC_MODE_640X480X10);
	g_params->m_brightness = 50;
	g_params->m_autoShutter = 1;
	g_params->m_awb = 1;
	g_params->m_awbRed = 1.0;
	g_params->m_awbBlue = 1.0;
	g_params->m_shutterSpeed = maxShutterSpeed(g_params->m_framerate);
	g_params->m_saturation = 200;
	g_params->m_maxLatency = KC_DEFAULT_MAX_LATENCY;
	g_params->m_memReserve = 10;
	g_params->m_hflip = 0;
	g_params->m_vflip = 0;
	g_params->m_startShift = 0;

	g_params->m_fps = 0.0;
	kcSetMinMaxFramerate();

	g_state.m_record = NULL;

	// copy over new parameter values
	g_state.m_currParams = *g_params;

	pthread_mutex_unlock(&g_state.m_paramsMutex);
}

void kcExit(void)
{
	printf("kcExit\n");
	kcStop();
}


void kcStart(void)
{
	int ret;
	pthread_attr_t attr;
	struct sched_param param;
	pthread_mutex_lock(&g_state.m_paramsMutex);

	// if we're stopping we need to stop first before starting again (lazy method)	
	while(g_state.m_stopping) 
		sched_yield();

	// KcStart might be called while we have active streams, e.g. when we change
	// resolution, so we lock here.
	pthread_mutex_lock(&g_state.m_frameMutex);
	g_state.m_frame = NULL;		
	g_state.m_run = 1;
	g_state.m_ptsOffset = -1;
	g_state.m_pts = 0;
	g_state.m_lastPts = 0;
	g_state.m_frameTimer = 0;
	g_state.m_stopping = 0;
	pthread_mutex_unlock(&g_state.m_frameMutex);

	// limit framerate if new mode requires it
	kcSetMinMaxFramerate();
	if (g_params->m_framerate > g_params->m_maxFps)
		g_params->m_framerate = g_params->m_maxFps;
	else if (g_params->m_framerate < g_params->m_minFps)
		g_params->m_framerate = g_params->m_minFps;

	// copy over new parameter values
	g_state.m_currParams = *g_params;
	
	// grab thread needs highest priority
	pthread_attr_init (&attr);
	pthread_attr_getschedparam (&attr, &param);
	param.sched_priority = sched_get_priority_max(SCHED_RR); 

	pthread_create(&g_state.m_thread, &attr, vidThread, NULL);	
	ret = pthread_setschedparam(g_state.m_thread, SCHED_RR, &param);
	if (ret!=0)
		printf("error: unable to set thread priority\n");
	pthread_mutex_unlock(&g_state.m_paramsMutex);
}

void kcStopInternal(unsigned join, unsigned signal)
{
	if (g_state.m_stopping)
		return;

	pthread_mutex_lock(&g_state.m_paramsMutex);

	// stop thread somehow
	kcStopCameraLoop();

	pthread_mutex_lock(&g_state.m_frameMutex);
	g_state.m_run = 0;
	if (signal)
	{
		pthread_cond_signal(&g_state.m_cond); 
		if (g_state.m_frame)
		{
			free(g_state.m_frame);
			g_state.m_frame = NULL;
		}
		g_state.m_record = NULL;
	}
	pthread_mutex_unlock(&g_state.m_frameMutex);

 	// wait for thread to exit (join), or indicate that we're stopping
	if (join)
		pthread_join(g_state.m_thread, NULL);
	else
		g_state.m_stopping = 1; 

	g_params->m_fps = 0.0;
	
	pthread_mutex_unlock(&g_state.m_paramsMutex);
}

void kcStop(void)
{
	kcStopInternal(1, 1);
}

void kcStopped(void)
{
	g_state.m_stopping = 0;
}

KcFrame *kcCopyFrame(const KcFrame *frame)
{
    int size = kcSizeofFrameBuffer(frame->m_width, frame->m_height, frame->m_type);
    KcFrame *newFrame = (KcFrame *)malloc(size);
    memcpy(newFrame, frame, size);
   	return newFrame;
}



void kcWaitNextRecordFrame(FrameList *list)
{
	pthread_mutex_lock(&g_state.m_frameMutex);
	while(flistEnd(list) && g_state.m_run && list==g_state.m_record)
		// unlock flist mutex to avoid deadlock with kcFrameData (which grabs mutex)
		pthread_cond_wait(&g_state.m_cond, &g_state.m_frameMutex);

	pthread_mutex_unlock(&g_state.m_frameMutex);
}

void kcWaitLastRecordFrame(void)
{
	pthread_mutex_lock(&g_state.m_frameMutex);
	if (g_state.m_record)
	{
		while(g_state.m_record->m_back==NULL && g_state.m_run)
			// unlock flist mutex to avoid deadlock with kcFrameData (which grabs mutex)
			pthread_cond_wait(&g_state.m_cond, &g_state.m_frameMutex);
	}
	pthread_mutex_unlock(&g_state.m_frameMutex);
}

KcFrame *kcNextStreamFrame(void)
{
	unsigned wait;
	PyThreadState *save; 
	KcFrame *frame;

	// Check to see if we're running
	if (!g_state.m_run)
		kcStart();

	pthread_mutex_lock(&g_state.m_frameMutex);

	// reset timer
	kcSetTimer(&g_state.m_frameTimer);

	wait = g_state.m_frame==NULL && g_state.m_run;
	// if we're going to wait for the next frame, release the GIL
	if (wait)
		save = PyEval_SaveThread();
	// grab latest frame or wait for new frame
	while(g_state.m_frame==NULL && g_state.m_run)
	{
		//printf("wait\n");
		pthread_cond_wait(&g_state.m_cond, &g_state.m_frameMutex);
	}
	frame = g_state.m_frame;
	// free-up frame
	g_state.m_frame = NULL;			

	pthread_mutex_unlock(&g_state.m_frameMutex);
	// reacquire GIL -- note, this may block and so we release m_frameMutex 
	// before we call PyEval_RestoreThread, which may seem odd.   
	if (wait)
		PyEval_RestoreThread(save);		

	return frame;
}


uint32_t getTime(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	uint32_t t = 1000000*tv.tv_sec + tv.tv_usec;
	return t;	
}


uint32_t kcGetTimer(uint32_t timer)
{
	return getTime() - timer;
}


void kcSetTimer(uint32_t *timer)
{
	*timer = getTime();
}


void kcFrameData(uint16_t width, uint16_t height, FrameType type, uint64_t pts, uint8_t *data,  unsigned int len)
{	
	float fps, mfps;
	KcFrame *frame;
	unsigned stop = 0;
	unsigned stopRecord = 0;
	int res;

	pthread_mutex_lock(&g_state.m_frameMutex);

	if (!g_state.m_record && g_state.m_frameTimer && kcGetTimer(g_state.m_frameTimer)>KC_MAX_FRAME_TIMEOUT)
	{
		stop = 1;
		goto end;
	}

	// deal with pts offset
    if (g_state.m_ptsOffset<0)
    	g_state.m_ptsOffset = pts;
    // subtract out offset
    pts -= g_state.m_ptsOffset;
    g_state.m_pts = pts;

	if (pts>g_state.m_lastPts && pts!=g_state.m_lastPts)
	{
		fps = 1000000.0/(pts-g_state.m_lastPts);
		mfps = g_params->m_fps;
		mfps = (1.0-KC_FPS_FILTER)*mfps + KC_FPS_FILTER*fps;
		g_params->m_fps = mfps;
	}
	// is the frame expired?
	if (g_state.m_record==NULL && g_state.m_frame && pts-g_state.m_frame->m_pts>g_params->m_maxLatency)
	{
		free(g_state.m_frame);
		g_state.m_frame = NULL;
	}

	// copy and add frame as long as we have free space in table
	// Check for m_run because of condition where we stop the recording and 
	// we end up with a stranded frame in g_state.m_frame because we've set 
	// g_state.m_record to NULL and the video thread is still sending frames.  
	if ((g_state.m_frame==NULL || g_state.m_record) && g_state.m_run)
	{
		//printf("copy frame %lld\n", pts);
		// allocate and copy new frame
		frame = (KcFrame *)malloc(kcSizeofFrameBuffer(width, height, type));
		if (frame==NULL)
		{
			stop = stopRecord = 1;
			goto end;
		}
		frame->m_width = width;
		frame->m_height = height;
		frame->m_type = type;
		frame->m_pts = pts;
		memcpy(frame->m_data, data, len);
		
		if (g_state.m_record)
		{
        	pthread_mutex_lock(&g_state.m_record->m_mutex);            
			res = flistAppend(g_state.m_record, frame);
        	pthread_mutex_unlock(&g_state.m_record->m_mutex);            
			if (res<0)
			{
				//printf("*** stop recording %d\n", res);
				stopRecord = 1;
			}
		}
		else
			g_state.m_frame = frame;
		// signal other thread
		pthread_cond_signal(&g_state.m_cond); 
	}

	g_state.m_lastPts = pts;

	// check the memory reserve, stop if it's exceeded
	if (g_state.m_record && kcMemReserveExceeded())
	{
		printf("Stopping to maintain free memory reserve.\n");
		stopRecord = 1;
	}

	end:
	pthread_mutex_unlock(&g_state.m_frameMutex);
	if (stopRecord)
		kcStopRecord();
	if (stop)
		// We don't want to wait for thread to end -- this will cause deadlock
		kcStopInternal(0, 1);
}



void kcUpdateParams(void)
{
	unsigned restart = 0;
	pthread_mutex_lock(&g_state.m_paramsMutex);

	if (g_params->m_width!=g_state.m_currParams.m_width || g_params->m_height!=g_state.m_currParams.m_height)
		restart = 1;

	if (g_params->m_framerate!=g_state.m_currParams.m_framerate)
		kcSetFramerate();

	if (strcmp(g_params->m_mode, g_state.m_currParams.m_mode))
	{
		kcSetMode();
		restart = 1;
	}

	if (g_params->m_brightness!=g_state.m_currParams.m_brightness)
		kcSetBrightness();

	if (g_params->m_autoShutter!=g_state.m_currParams.m_autoShutter)
		kcSetAutoShutter();

	if (g_params->m_awb!=g_state.m_currParams.m_awb)
		kcSetAWB();

	if (g_params->m_awbRed!=g_state.m_currParams.m_awbRed || g_params->m_awbBlue!=g_state.m_currParams.m_awbBlue)
		kcSetAWBGains();

	if (g_params->m_shutterSpeed!=g_state.m_currParams.m_shutterSpeed)
		kcSetShutterSpeed();

	// TODO: m_saturation
	pthread_mutex_unlock(&g_state.m_paramsMutex);

	if (restart && g_state.m_run) // if we're supposed to restart and we're running
	{
		printf("restarting\n");
		// We don't want to signal otherwise we'll get a null frame in the stream
		kcStopInternal(1, 0);

		// we should update the shutter speed to the max shutter speed to prevent 
		// the case where we decrease the framerate and the shutter speed stays low (and crappy-looking)
		g_params->m_shutterSpeed = maxShutterSpeed(g_params->m_framerate);

		kcStart(); 
	}
	else // set the current params otherwise
	{
		pthread_mutex_lock(&g_state.m_paramsMutex);
		g_state.m_currParams = *g_params;
		pthread_mutex_unlock(&g_state.m_paramsMutex);
	}
}


const char **kcGetModes(void)
{
	static const char *modes[] =
	{
		KC_MODE_320X240X10,
		KC_MODE_640X480X10,
		KC_MODE_1280X960X10,
		NULL // indicate end of list
	};

	return modes;
}


void kcSetMinMaxFramerate(void)
{
	if (!strcmp(g_params->m_mode, KC_MODE_320X240X10) ||
		!strcmp(g_params->m_mode, KC_MODE_640X480X10) ||
		!strcmp(g_params->m_mode, KC_MODE_1280X960X10))
	{
		g_params->m_maxFps = 90;
		g_params->m_minFps = 4;
	}	
}
 

void kcSetMode(void)
{
	if (!strcmp(g_params->m_mode, KC_MODE_320X240X10))
	{
		g_params->m_width = 320;
		g_params->m_height = 240;
	}
	else if (!strcmp(g_params->m_mode, KC_MODE_640X480X10))
	{
		g_params->m_width = 640;
		g_params->m_height = 480;
	}
	else if (!strcmp(g_params->m_mode, KC_MODE_1280X960X10))
	{
		g_params->m_width = 1280;
		g_params->m_height = 960;
	}
	// else, unknown mode, don't change anything
}


int kcStartRecord(void)
{

	if (g_state.m_record)
		return -1; // already recording

	pthread_mutex_lock(&g_state.m_frameMutex);
	g_state.m_record = (FrameList *)malloc(sizeof(FrameList));
	flistInit(g_state.m_record, g_params->m_startShift, g_params->m_duration);
	pthread_mutex_unlock(&g_state.m_frameMutex);
	
	return 0;
}

FrameList *kcGetRecord(void)
{
	return g_state.m_record;
}

void kcStopRecord(void)
{
	pthread_mutex_lock(&g_state.m_frameMutex);
	if (g_state.m_record!=NULL)
	{
		g_state.m_record->m_recording = 0;  // reflect that no longer recording
		g_state.m_record = NULL;
	}
	pthread_mutex_unlock(&g_state.m_frameMutex);
}

unsigned kcRecordProgress(void)
{
	unsigned reserve, p0=0, p1=0;

	if (g_state.m_run && g_state.m_record)
	{
		// calc based on free memory
		reserve = g_state.m_currParams.m_memReserve;
		p0 = 100-(memfree()-reserve)*100/reserve;
		if (p0<0)
			p0 = 0;
		if (p0>100)
			p0 = 100;
		// calc based on duration
		pthread_mutex_lock(&g_state.m_record->m_mutex);
		if (g_state.m_record->m_duration!=0)
		{
			p1 = flistTime(g_state.m_record)*100/g_state.m_record->m_duration;
			if (p1>100)
				p1 = 100;
		}
		pthread_mutex_unlock(&g_state.m_record->m_mutex);
		// return whichever is greater (because the greater one will end the recording)
		if (p0>p1)
			return p0;
		else
			return p1;
	}
	else
		return 100;
}

unsigned kcSizeofFrameBuffer(unsigned width, unsigned height, FrameType type)
{
	unsigned area = width*height;
	return area*3 + sizeof(KcFrame) + 32; // BGR
}

unsigned kcSizeofFrame(const KcFrame *frame)
{
	return kcSizeofFrameBuffer(frame->m_width, frame->m_height, frame->m_type);
}

unsigned kcMemReserveExceeded(void)
{
	return memfree()<g_state.m_currParams.m_memReserve;
}
