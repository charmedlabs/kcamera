/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2020, Google Inc.
 *
 * post_processor_jpeg.cpp - JPEG Post Processor
 */

#include "post_processor_jpeg.h"

#include <chrono>

#include "../camera_device.h"
#include "../camera_metadata.h"
#include "encoder_libjpeg.h"
#include "exif.h"

#include <libcamera/formats.h>

#include "libcamera/internal/log.h"

using namespace libcamera;
using namespace std::chrono_literals;

LOG_DEFINE_CATEGORY(JPEG)

PostProcessorJpeg::PostProcessorJpeg(CameraDevice *const device)
	: cameraDevice_(device)
{
}

int PostProcessorJpeg::configure(const StreamConfiguration &inCfg,
				 const StreamConfiguration &outCfg)
{
	if (inCfg.size != outCfg.size) {
		LOG(JPEG, Error) << "Mismatch of input and output stream sizes";
		return -EINVAL;
	}

	if (outCfg.pixelFormat != formats::MJPEG) {
		LOG(JPEG, Error) << "Output stream pixel format is not JPEG";
		return -EINVAL;
	}

	streamSize_ = outCfg.size;

	thumbnailer_.configure(inCfg.size, inCfg.pixelFormat);

	encoder_ = std::make_unique<EncoderLibJpeg>();

	return encoder_->configure(inCfg);
}

void PostProcessorJpeg::generateThumbnail(const FrameBuffer &source,
					  const Size &targetSize,
					  unsigned int quality,
					  std::vector<unsigned char> *thumbnail)
{
	/* Stores the raw scaled-down thumbnail bytes. */
	std::vector<unsigned char> rawThumbnail;

	thumbnailer_.createThumbnail(source, targetSize, &rawThumbnail);

	StreamConfiguration thCfg;
	thCfg.size = targetSize;
	thCfg.pixelFormat = thumbnailer_.pixelFormat();
	int ret = thumbnailEncoder_.configure(thCfg);

	if (!rawThumbnail.empty() && !ret) {
		/*
		 * \todo Avoid value-initialization of all elements of the
		 * vector.
		 */
		thumbnail->resize(rawThumbnail.size());

		int jpeg_size = thumbnailEncoder_.encode(rawThumbnail,
							 *thumbnail, {}, quality);
		thumbnail->resize(jpeg_size);

		LOG(JPEG, Debug)
			<< "Thumbnail compress returned "
			<< jpeg_size << " bytes";
	}
}

int PostProcessorJpeg::process(const FrameBuffer &source,
			       CameraBuffer *destination,
			       const CameraMetadata &requestMetadata,
			       CameraMetadata *resultMetadata)
{
	if (!encoder_)
		return 0;

	ASSERT(destination->numPlanes() == 1);

	camera_metadata_ro_entry_t entry;
	int ret;

	/* Set EXIF metadata for various tags. */
	Exif exif;
	exif.setMake(cameraDevice_->maker());
	exif.setModel(cameraDevice_->model());

	ret = requestMetadata.getEntry(ANDROID_JPEG_ORIENTATION, &entry);

	const uint32_t jpegOrientation = ret ? *entry.data.i32 : 0;
	resultMetadata->addEntry(ANDROID_JPEG_ORIENTATION, &jpegOrientation, 1);
	exif.setOrientation(jpegOrientation);

	exif.setSize(streamSize_);
	/*
	 * We set the frame's EXIF timestamp as the time of encode.
	 * Since the precision we need for EXIF timestamp is only one
	 * second, it is good enough.
	 */
	exif.setTimestamp(std::time(nullptr), 0ms);

	ret = resultMetadata->getEntry(ANDROID_SENSOR_EXPOSURE_TIME, &entry);
	exif.setExposureTime(ret ? *entry.data.i64 : 0);
	ret = requestMetadata.getEntry(ANDROID_LENS_APERTURE, &entry);
	if (ret)
		exif.setAperture(*entry.data.f);
	exif.setISO(100);
	exif.setFlash(Exif::Flash::FlashNotPresent);
	exif.setWhiteBalance(Exif::WhiteBalance::Auto);

	exif.setFocalLength(1.0);

	ret = requestMetadata.getEntry(ANDROID_JPEG_GPS_TIMESTAMP, &entry);
	if (ret) {
		exif.setGPSDateTimestamp(*entry.data.i64);
		resultMetadata->addEntry(ANDROID_JPEG_GPS_TIMESTAMP,
					 entry.data.i64, 1);
	}

	ret = requestMetadata.getEntry(ANDROID_JPEG_THUMBNAIL_SIZE, &entry);
	if (ret) {
		const int32_t *data = entry.data.i32;
		Size thumbnailSize = { static_cast<uint32_t>(data[0]),
				       static_cast<uint32_t>(data[1]) };

		ret = requestMetadata.getEntry(ANDROID_JPEG_THUMBNAIL_QUALITY, &entry);
		uint8_t quality = ret ? *entry.data.u8 : 95;
		resultMetadata->addEntry(ANDROID_JPEG_THUMBNAIL_QUALITY, &quality, 1);

		if (thumbnailSize != Size(0, 0)) {
			std::vector<unsigned char> thumbnail;
			generateThumbnail(source, thumbnailSize, quality, &thumbnail);
			if (!thumbnail.empty())
				exif.setThumbnail(thumbnail, Exif::Compression::JPEG);
		}

		resultMetadata->addEntry(ANDROID_JPEG_THUMBNAIL_SIZE, data, 2);
	}

	ret = requestMetadata.getEntry(ANDROID_JPEG_GPS_COORDINATES, &entry);
	if (ret) {
		exif.setGPSLocation(entry.data.d);
		resultMetadata->addEntry(ANDROID_JPEG_GPS_COORDINATES,
					 entry.data.d, 3);
	}

	ret = requestMetadata.getEntry(ANDROID_JPEG_GPS_PROCESSING_METHOD, &entry);
	if (ret) {
		std::string method(entry.data.u8, entry.data.u8 + entry.count);
		exif.setGPSMethod(method);
		resultMetadata->addEntry(ANDROID_JPEG_GPS_PROCESSING_METHOD,
					 entry.data.u8, entry.count);
	}

	if (exif.generate() != 0)
		LOG(JPEG, Error) << "Failed to generate valid EXIF data";

	ret = requestMetadata.getEntry(ANDROID_JPEG_QUALITY, &entry);
	const uint8_t quality = ret ? *entry.data.u8 : 95;
	resultMetadata->addEntry(ANDROID_JPEG_QUALITY, &quality, 1);

	int jpeg_size = encoder_->encode(source, destination->plane(0),
					 exif.data(), quality);
	if (jpeg_size < 0) {
		LOG(JPEG, Error) << "Failed to encode stream image";
		return jpeg_size;
	}

	/* Fill in the JPEG blob header. */
	uint8_t *resultPtr = destination->plane(0).data()
			   + destination->jpegBufferSize(cameraDevice_->maxJpegBufferSize())
			   - sizeof(struct camera3_jpeg_blob);
	auto *blob = reinterpret_cast<struct camera3_jpeg_blob *>(resultPtr);
	blob->jpeg_blob_id = CAMERA3_JPEG_BLOB_ID;
	blob->jpeg_size = jpeg_size;

	/* Update the JPEG result Metadata. */
	resultMetadata->addEntry(ANDROID_JPEG_SIZE, &jpeg_size, 1);

	return 0;
}
