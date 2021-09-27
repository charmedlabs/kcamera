#ifndef _KENCODER_H
#define _KENCODER_H

#include <Python.h>
#include <inttypes.h>

typedef struct 
{
	unsigned int m_width;
	unsigned int m_height;
	unsigned int m_bitrate;
	char m_mode[128];
} KeParams;	


typedef struct 
{
    void *mem;
    size_t bytes_used;
    size_t length;
    unsigned int index;
    uint32_t keyframe;
    int64_t timestamp_us;
} KeOutput;

int keInit(KeParams *params);
void keExit(void);
const char **keGetModes(void);
int keEncodeIn(uint8_t *mem, uint32_t size, uint32_t width, uint32_t height, uint64_t timestamp_us);
void keEncodeOut(KeOutput *output);
void keEncodeOutDone(KeOutput *output);
void keUpdateParams(void);

#endif
