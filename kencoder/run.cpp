#include <exception>
#include "run.h"
#include "video_options.hpp"
#include "h264_encoder.hpp"

static VideoOptions g_options;
static Encoder *g_encoder = nullptr;
static KeParams *g_params;
static KeParams g_currParams;

extern "C" int keInit(KeParams *params)
{
    g_params = params;
    g_currParams = *g_params;

    try
    { 
        g_options.width = g_params->m_width;
        g_options.height = g_params->m_height;        
        // transfer params into options, etc.
        g_encoder = new H264Encoder(g_options);
    }
    catch (std::exception& e)
    {
        printf("Standard exception: %s\n", e.what());
        return -1;
    }

    return 0;
}

extern "C" void keExit(void)
{
    printf("keExit\n");
    if (g_encoder)
    {
        delete g_encoder;
        g_encoder = nullptr;
    }
}

extern "C" const char **keGetModes(void)
{
    static const char *modes[] = {
        "default", 
        NULL
    };
    return modes;
}

extern "C" int keEncodeIn(uint8_t *mem, uint32_t size, uint32_t width, uint32_t height, uint64_t timestamp_us)
{
    return g_encoder->EncodeBuffer(0, size, mem, width, height, width, timestamp_us);
}

extern "C" void keEncodeOut(KeOutput *output)
{
    g_encoder->GetOutput((OutputItem *)output);
}

extern "C" void keEncodeOutDone(KeOutput *output)
{
    g_encoder->OutputDone((OutputItem &)*output);
}

extern "C" void keUpdateParams(void)
{
    unsigned restart = 0;

    if (g_params->m_bitrate!=g_currParams.m_bitrate)
        g_encoder->SetBitrate(g_params->m_bitrate);
    
    if (g_params->m_width!=g_currParams.m_width ||
        g_params->m_height!=g_currParams.m_height)
        restart = 1;

    if (restart)
    {
        keExit();
        keInit(g_params);
    }
    else
        g_currParams = *g_params;
}
