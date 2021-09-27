#ifndef _FRAME_LIST
#define _FRAME_LIST
#include <pthread.h>
#include "kcframe.h"

struct FrameNode
{
  KcFrame *m_frame;
  struct FrameNode *m_next;
};

typedef struct FrameNode FrameNode;

typedef struct
{
  pthread_mutex_t m_mutex; 
  int m_recording;
  FrameNode *m_front; 
  FrameNode *m_back;
  FrameNode **m_readNode;
  unsigned m_len;
  unsigned m_readIndex;
  int m_t0;
  int m_startShift;
  unsigned m_duration;
}
FrameList;

void flistInit(FrameList *list, int startShift, unsigned duration);
void flistClear(FrameList *list);
int flistAppend(FrameList *list, KcFrame *frame);
int flistSeek(FrameList *list, unsigned n);
KcFrame *flistNext(FrameList *list);
unsigned flistEnd(FrameList *list);
void flistVerify(FrameList *list);
unsigned flistTime(FrameList *list);

#endif