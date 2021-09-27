#include <chrono>
#include <mutex>
#include <libcamera/controls.h>
#include <libcamera/transform.h>
#include "libcamera_app.hpp"
#include "video_options.hpp"
#include "options.hpp"
#include "kcamera.h"


using namespace std::placeholders;
using namespace libcamera;


class LibcameraRaw: public LibcameraApp<VideoOptions>
{
public:

    FrameType frameType_;
};

static bool run; 
static LibcameraRaw *app = nullptr;
static ControlList _controls;
static std::mutex controls_mutex;

// The main even loop for the application.
static void event_loop(void)
{
    void *mem;
    int64_t timestamp_ns;

    app->frameType_ = FRAME_BGR;
    app->options.width = g_params->m_width;
    app->options.height = g_params->m_height;
    // Copy g_params into options and controls
    if (g_params->m_hflip)
        app->options.transform = Transform::HFlip * app->options.transform;
    if (g_params->m_vflip)
        app->options.transform = Transform::VFlip * app->options.transform;
    kcSetBrightness();
    kcSetAWB();
    kcSetFramerate();
    kcSetAutoShutter();
    //using namespace libcamera::controls::draft;
    //_controls.set(NoiseReductionMode, NoiseReductionModeOff); 
    //_controls.set(NoiseReductionMode, NoiseReductionModeMinimal);
    //_controls.set(NoiseReductionMode, NoiseReductionModeHighQuality);  
    app->OpenCamera();
    app->ConfigureVideo(LibcameraRaw::FLAG_VIDEO_RAW);
    app->StartCamera();
    for (unsigned int count = 0; run; count++)
    {
        {
            std::lock_guard<std::mutex> lock(controls_mutex);
            if (!_controls.empty())
                app->SetControls(_controls);
        }

        LibcameraRaw::Msg msg = app->Wait();

        if (msg.type != LibcameraRaw::MsgType::RequestComplete)
            throw std::runtime_error("unrecognised message!");
        CompletedRequest &completed_request = std::get<CompletedRequest>(msg.payload);
        libcamera::Stream *stream = app->VideoStream();
        libcamera::FrameBuffer *buffer = completed_request.buffers[stream];
        mem = app->Mmap(buffer)[0];
        if (!buffer || !mem)
            throw std::runtime_error("no buffer to encode");
        timestamp_ns = buffer->metadata().timestamp;
        kcFrameData(app->options.width, app->options.height, app->frameType_, timestamp_ns/1000, (uint8_t *)mem,  buffer->planes()[0].length);
        app->QueueRequest(completed_request);

    }
    app->StopCamera();
    kcStopped(); // indicate that we've stopped
}


extern "C" int kcStartCameraLoop(void)
{
    run = true;
    try
    {
        LibcameraRaw _app;
        app = &_app;
        event_loop();
        app = nullptr;
    }
    catch (std::exception const &e)
    {
        std::cerr << "ERROR: *** " << e.what() << " ***" << std::endl;
        return -1;
    }
    return 0;
}

extern "C" int kcStopCameraLoop(void)
{
    run = false;
    return 0;
}

#define BRIGHTNESS_KNEE           75
#define BRIGHTNESS_GAIN           1.0 // smaller is more gain
#define BRIGHTNESS_CONTRAST_RATIO 0.5

extern "C" void kcSetBrightness(void)
{
    std::lock_guard<std::mutex> lock(controls_mutex);
    _controls.set(controls::ExposureValue, (float)((int)g_params->m_brightness-50)/5.0);
    // For the last part of the brightness range, we add some digital gain by combining
    // brightness and contrast together -- a bit more contrast for the same brightness.
    if (g_params->m_brightness<=BRIGHTNESS_KNEE)
    {
        _controls.set(controls::Brightness, 0.0);
        _controls.set(controls::Contrast, 1.0);
    }
    else
    {
        _controls.set(controls::Brightness, (float)((int)g_params->m_brightness-BRIGHTNESS_KNEE)/((100-BRIGHTNESS_KNEE)*BRIGHTNESS_GAIN));
        _controls.set(controls::Contrast, (float)((int)g_params->m_brightness-BRIGHTNESS_KNEE)/((100-BRIGHTNESS_KNEE)*BRIGHTNESS_GAIN*BRIGHTNESS_CONTRAST_RATIO)+1.0);
    }
}


extern "C" void kcSetAWBGains(void)
{
    std::lock_guard<std::mutex> lock(controls_mutex);
    float r=g_params->m_awbRed*2.0, b=g_params->m_awbBlue*2.0;
    _controls.set(controls::ColourGains, {r, b});
}


extern "C" void kcSetAWB(void)
{
    std::lock_guard<std::mutex> lock(controls_mutex);
    if (g_params->m_awb)
    {
        float r=0.0, b=0.0;
        _controls.set(controls::ColourGains, {r, b});
    }
    else
    {
        float r=g_params->m_awbRed*2.0, b=g_params->m_awbBlue*2.0;
        _controls.set(controls::ColourGains, {r, b});
    }
}

extern "C" void kcSetFramerate(void)
{
    std::lock_guard<std::mutex> lock(controls_mutex);
    int64_t shutterSpeed = (uint64_t)(g_params->m_shutterSpeed*1000000.0);
    int64_t frame_time = 1000000/g_params->m_framerate; // in us
    _controls.set(controls::FrameDurations, {frame_time, frame_time});
    // If shutter speed exceeds frame period, we need to adjust the shutter speed
    if (shutterSpeed>frame_time)
    {
        g_params->m_shutterSpeed = frame_time/1000000.0;
        _controls.set(controls::ExposureTime, (uint32_t)frame_time);
    }
       
}

extern "C" void kcSetShutterSpeed(void)
{
    std::lock_guard<std::mutex> lock(controls_mutex);
    _controls.set(controls::ExposureTime, (uint32_t)(g_params->m_shutterSpeed*1000000.0));
}

extern "C" void kcSetAutoShutter(void)
{
    std::lock_guard<std::mutex> lock(controls_mutex);
    if (g_params->m_autoShutter)
        _controls.set(controls::ExposureTime, 0);
    else
        _controls.set(controls::ExposureTime, (uint32_t)(g_params->m_shutterSpeed*1000000.0));
}


