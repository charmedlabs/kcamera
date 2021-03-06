/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2020, Raspberry Pi (Trading) Ltd.
 *
 * h264_encoder.hpp - h264 video encoder.
 */

#pragma once

#include <mutex>
#include <queue>
#include <thread>
#include <condition_variable>

#include "encoder.hpp"

class H264Encoder : public Encoder
{
public:
	H264Encoder(VideoOptions const &options);
	~H264Encoder();

	void GetOutput(OutputItem *item) override;
    void OutputDone(const OutputItem &item) override;

	// Encode the given DMABUF.
	int EncodeBuffer(int fd, size_t size,
					 void *mem, int width, int height, int stride,
					 int64_t timestamp_us) override;

    void SetBitrate(unsigned bitrate) override;

private:
	// We want at least as many output buffers as there are in the camera queue
	// (we always want to be able to queue them when they arrive). Make loads
	// of capture buffers, as this is our buffering mechanism in case of delays
	// dealing with the output bitstream.
	static constexpr int NUM_OUTPUT_BUFFERS  = 6;
	static constexpr int NUM_CAPTURE_BUFFERS = 12;

	// This thread just sits waiting for the encoder to finish stuff. It will either:
	// * receive "output" buffers (codec inputs), which we must return to the caller
	// * receive encoded buffers, which we pass to the application.
	void pollThread();

	bool abort_;
	int fd_;
	struct BufferDescription
	{
		void *mem;
		size_t size;
	};
	BufferDescription buffers_[NUM_CAPTURE_BUFFERS];
	BufferDescription input_buffers_[NUM_OUTPUT_BUFFERS];
	std::thread poll_thread_;
	std::mutex input_buffers_available_mutex_;
	std::queue<int> input_buffers_available_;
	std::queue<OutputItem> output_queue_;
	std::mutex output_mutex_;
	std::condition_variable output_cond_var_;
};
