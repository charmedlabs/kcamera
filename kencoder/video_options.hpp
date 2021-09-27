/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2020, Raspberry Pi (Trading) Ltd.
 *
 * still_video.hpp - video capture program options
 */

#pragma once

#include <cstdio>

#include <string>

#include "options.hpp"

struct VideoOptions : public Options
{
	VideoOptions() : Options()
	{
		bitrate = 0;
		profile = "";
		level = ""; 
		intra = 0;
		inline_headers = false;
		codec = "h264";
		save_pts = "";
		quality = 50;
		listen = false;
		keypress = false;
		signal = false;
		initial = "record";
		pause = false;
		split = false;
		segment = 0;
		circular = false;
	}

	uint32_t bitrate;
	std::string profile;
	std::string level;
	unsigned int intra;
	bool inline_headers;
	std::string codec;
	std::string save_pts;
	int quality;
	bool listen;
	bool keypress;
	bool signal;
	std::string initial;
	bool pause;
	bool split;
	uint32_t segment;
	bool circular;
};
