/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2020, Raspberry Pi (Trading) Ltd.
 *
 * options.hpp - common program options
 */

#pragma once

#include <iostream>

#include <libcamera/control_ids.h>
#include <libcamera/transform.h>

struct Options
{
	Options() 
	{
		help = false;
		verbose = true;
		timeout = 5000;
		width = 640;
		height = 480;
		rawfull = false;
		transform = libcamera::Transform::Identity;
		roi_x = 0;
		roi_y = 0;
		roi_width = 0.0;
		roi_height = 0.0;
		shutter = 0;
		gain = 0;
		metering = "center";
		metering_index = 0;
		exposure = "normal";
		exposure_index = 0;
		ev = 0;
		awb = "auto";
		awb_index = 0;
		awb_gain_r = 0.0;
		awb_gain_b = 0.0;
		brightness = 0.0;
		contrast = 1.0;
		saturation = 1.0;
		sharpness = 1.0;
		framerate = 30.0;
		denoise = "auto";
	}

	bool help;
	bool verbose;
	uint64_t timeout; // in ms
	std::string output;
	unsigned int width;
	unsigned int height;
	bool rawfull;
	libcamera::Transform transform;
	float roi_x, roi_y, roi_width, roi_height;
	float shutter;
	float gain;
	std::string metering;
	int metering_index;
	std::string exposure;
	int exposure_index;
	float ev;
	std::string awb;
	int awb_index;
	float awb_gain_r;
	float awb_gain_b;
	float brightness;
	float contrast;
	float saturation;
	float sharpness;
	float framerate;
	std::string denoise;

private:
	bool hflip_;
	bool vflip_;
	int rotation_;
};
