#include <Python.h>
#include <stdlib.h>
#include "framelist.h"

void flistInit(FrameList *list, int startShift, unsigned duration)
{
	pthread_mutex_init(&list->m_mutex, NULL);
	// if we're negative-shifting, set m_recording to -1
	list->m_recording = startShift==0 ? 1 : -1;
	list->m_front = NULL;
	list->m_back = NULL;
	list->m_readNode = &list->m_front;
	list->m_len = 0;
	list->m_readIndex = 0;
	list->m_t0 = -1;
	list->m_startShift = startShift;
	list->m_duration = duration;
}

void freeNode(FrameNode *node)
{
	if (node->m_frame)
		free(node->m_frame); // no base object, no references, free-up regardless
	free(node);	
}

void flistClear(FrameList *list)
{
	FrameNode *node, *next=list->m_front;
	while(next)
	{
		node = next;
		next = node->m_next;
		freeNode(node);
	}
	flistInit(list, list->m_startShift, list->m_duration);
}

int flistAppend(FrameList *list, KcFrame *frame)
{
	FrameNode *node;
	int64_t t;

	// ignore everything if we're not recording, unless m_startShift is <0
	if (list->m_recording==0)
	{
		free(frame);
		return 0; // toss frame
	}

	// Set t0 if it hasn't been intialized.  This is the first frame we see since we started recording.
	if (list->m_t0<0)
		list->m_t0 = frame->m_pts;

	// handle positive startShift (ignore all frames until the start shift has expired)
	if (list->m_startShift>0 && list->m_recording==-1)
	{
		if (frame->m_pts-list->m_t0<list->m_startShift)
		{
			// keep one frame for streaming
			if (list->m_len==1)
			{
				freeNode(list->m_front);
				list->m_len = 0;
			}
			else if (list->m_len>1)
				printf("Error: more than 1 frame!\n");
		}
		else // transistion to m_recording = 1
			list->m_recording = 1;
	}

	node = (FrameNode *)malloc(sizeof(FrameNode));
	if (node==NULL)	
		return -2;

	node->m_frame = frame;
	node->m_next = NULL;
	if (list->m_len==0)
		list->m_front = list->m_back = node;
	else
	{
	  	list->m_back->m_next = node;
	  	list->m_back = node;
	}
	list->m_len++;

	// check to see if we're starting in the past
	if (list->m_startShift<0 && list->m_recording==-1)
	{
		t = frame->m_pts + list->m_startShift;
		while(list->m_front && list->m_front->m_frame && (int64_t)list->m_front->m_frame->m_pts<t)
		{
			if (list->m_front->m_next==NULL) // we've reached the back
			{
				printf("*** back reached\n");
				break;
			}
			node = list->m_front->m_next;
			freeNode(list->m_front);
			list->m_front = node;
			list->m_len--;
		}
	}
	// check duration
	if (list->m_duration)
	{
		t = flistTime(list);
		if (t>=list->m_duration)
		{
			list->m_recording = 0;
			return -1;
		}
	}
	return 0;
}

int flistSeek(FrameList *list, unsigned n)
{
	if (n>=list->m_len)
		return -1; // doesn't exist

	for (list->m_readIndex=0, list->m_readNode=&list->m_front; 
		 *list->m_readNode && list->m_readIndex!=n; 
		 list->m_readNode=&(*list->m_readNode)->m_next, list->m_readIndex++);

 	return 0; // success
}

KcFrame *flistNext(FrameList *list)
{
	void *result = NULL;
	FrameNode *node = *list->m_readNode;

	if (node)
	{
		result = node->m_frame;
		list->m_readNode = &node->m_next;
		list->m_readIndex++;
	}
	return result;
}

unsigned flistEnd(FrameList *list)
{
	return *list->m_readNode==NULL;
}

void flistVerify(FrameList *list)
{
	unsigned index = list->m_readIndex;
	KcFrame *frame;
	printf("verify:\n");
	flistSeek(list, 0);
	for (frame=flistNext(list); frame; frame=flistNext(list))
	{
		if (frame->m_width!=640 || frame->m_height!=480)
			printf("*** error %d %d %d\n", list->m_readIndex-1, frame->m_width, frame->m_height);
	}
	flistSeek(list, index);
}

unsigned flistTime(FrameList *list)
{
	return list->m_back->m_frame->m_pts - list->m_front->m_frame->m_pts;
}
