#ifndef _KCAMERA_H
#define _KCAMERA_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <Python.h>
#include <inttypes.h>
#include "framelist.h"

#define KC_MODE_320X240X10                     "320x240x10"
#define KC_MODE_640X480X10                     "640x480x10"
#define KC_MODE_1280X960X10                    "1280x960x10"

typedef struct 
{
	unsigned int m_width;
	unsigned int m_height;
	unsigned int m_framerate;
	float m_duration;
	char m_mode[128];
	unsigned int m_brightness;
	unsigned int m_autoShutter;
	unsigned int m_awb;
	float m_awbRed;
	float m_awbBlue;
	float m_shutterSpeed;
	unsigned int m_saturation;
	unsigned int m_maxLatency;
	unsigned int m_memReserve;
	unsigned int m_hflip;
	unsigned int m_vflip;
	int m_startShift;

	// read-only
	float m_fps;
	unsigned int m_maxFps;
	unsigned int m_minFps;
} KcParams;	

extern KcParams *g_params;

void kcInit(KcParams *params);
void kcExit(void);
void kcStart(void);
void kcStop(void);
void kcStopped(void);
KcFrame *kcNextStreamFrame(void);
void kcWaitNextRecordFrame(FrameList *list);
void kcWaitLastRecordFrame(void);

KcFrame *kcCopyFrame(const KcFrame *frame);

int kcStartRecord(void);
void kcStopRecord(void);
FrameList *kcGetRecord(void);
unsigned kcRecordProgress(void);

void kcUpdateParams(void);

void kcSetMinMaxFramerate(void);

const char **kcGetModes(void);


void kcFrameData(uint16_t width, uint16_t height, FrameType type, uint64_t pts, uint8_t *data, unsigned int len);
void kcInitCallback(void);

void kcSetIRFilter(void);

unsigned kcSizeofFrameBuffer(unsigned width, unsigned height, FrameType type);
unsigned kcSizeofFrame(const KcFrame *frame);
unsigned kcMemReserveExceeded(void);

uint32_t kcGetTimer(uint32_t timer);
void kcSetTimer(uint32_t *timer);

int kcStartCameraLoop(void);
int kcStopCameraLoop(void);
void kcSetBrightness(void);
void kcSetAWBGains(void);
void kcSetFramerate(void);
void kcSetAWB(void);
void kcSetShutterSpeed(void);
void kcSetAutoShutter(void);

#ifdef __cplusplus
} // extern "C"
#endif
#endif
