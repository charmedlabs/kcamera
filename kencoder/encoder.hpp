/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2020, Raspberry Pi (Trading) Ltd.
 *
 * encoder.hpp - Video encoder class.
 */

#pragma once

#include <functional>

#include "video_options.hpp"

typedef std::function<void(int)> InputDoneCallback;
typedef std::function<void(void *,size_t, int64_t, bool)> OutputReadyCallback;

struct OutputItem 
{
    void *mem;
    size_t bytes_used;
    size_t length;
    unsigned int index;
    bool keyframe;
    int64_t timestamp_us;
};

class Encoder
{
public:
	Encoder(VideoOptions const &options) : options_(options) {}
	virtual ~Encoder() {}
	// This is where the application sets the callback it gets whenever the encoder
	// has finished with an input buffer, so the application can re-use it.
	void SetInputDoneCallback(InputDoneCallback callback)
	{
		input_done_callback_ = callback;
	}

	virtual void GetOutput(OutputItem *item) = 0;
    virtual void OutputDone(const OutputItem &item) = 0;

	// Encode the given buffer. The buffer is specified both by an fd and size
	// describing a DMABUF, and by a mmapped userland pointer.
	virtual int EncodeBuffer(int fd, size_t size,
							 void *mem, int width, int height, int stride,
							 int64_t timestamp_us) = 0;

    virtual void SetBitrate(unsigned bitrate) = 0;

protected:
	InputDoneCallback input_done_callback_;
	VideoOptions options_;
};
