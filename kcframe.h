#ifndef _KC_FRAME
#define _KC_FRAME

typedef enum
{
    FRAME_BGR
} FrameType;

typedef struct 
{
	uint16_t m_width;
	uint16_t m_height;
	uint64_t m_pts;
	FrameType m_type;
	uint8_t m_data[0]; // data is at a 20-byte offset, which is a 32-bit boundary, so we can read/write 32-bit values
} KcFrame;

#endif
