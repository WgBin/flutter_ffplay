#include "exception"
#include "ffi.h"

#include <mmdeviceapi.h>
#include <audiopolicy.h>

const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
const IID IID_IAudioClient = __uuidof(IAudioClient);
const IID IID_IAudioRenderClient = __uuidof(IAudioRenderClient);

class AudioClientImpl : public PlaybackClient
{
  IMMDeviceEnumerator *pEnumerator = nullptr;
  IMMDevice *pDevice = nullptr;
  IAudioClient *pAudioClient = nullptr;
  WAVEFORMATEX *pwfx = nullptr;
  IAudioRenderClient *pRenderClient = nullptr;

  static AVSampleFormat getSampleFormat(WAVEFORMATEX *wave_format)
  {
    switch (wave_format->wFormatTag)
    {
    case WAVE_FORMAT_PCM:
      if (16 == wave_format->wBitsPerSample)
      {
        return AV_SAMPLE_FMT_S16;
      }
      if (32 == wave_format->wBitsPerSample)
      {
        return AV_SAMPLE_FMT_S32;
      }
      break;
    case WAVE_FORMAT_IEEE_FLOAT:
      return AV_SAMPLE_FMT_FLT;
    case WAVE_FORMAT_ALAW:
    case WAVE_FORMAT_MULAW:
      return AV_SAMPLE_FMT_U8;
    case WAVE_FORMAT_EXTENSIBLE:
    {
      const WAVEFORMATEXTENSIBLE *wfe = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(wave_format);
      if (KSDATAFORMAT_SUBTYPE_IEEE_FLOAT == wfe->SubFormat)
      {
        return AV_SAMPLE_FMT_FLT;
      }
      if (KSDATAFORMAT_SUBTYPE_PCM == wfe->SubFormat)
      {
        if (16 == wave_format->wBitsPerSample)
        {
          return AV_SAMPLE_FMT_S16;
        }
        if (32 == wave_format->wBitsPerSample)
        {
          return AV_SAMPLE_FMT_S32;
        }
      }
      break;
    }
    default:
      break;
    }
    return AV_SAMPLE_FMT_NONE;
  }

public:
  int64_t getSampleRate()
  {
    return pwfx->nSamplesPerSec;
  }
  int64_t getChannels()
  {
    return pwfx->nChannels;
  }
  AVSampleFormat getFormat()
  {
    return format;
  }
  int64_t getBufferFrameCount()
  {
    return bufferFrameCount;
  }

  uint32_t getCurrentPadding()
  {
    uint32_t numFramesPadding = 0;
    if (pAudioClient)
      pAudioClient->GetCurrentPadding(&numFramesPadding);
    return numFramesPadding;
  }
  int writeBuffer(uint8_t *data, int64_t length)
  {
    if (!pRenderClient)
      return -1;
    int requestBuffer = min(bufferFrameCount - getCurrentPadding(), length);
    uint8_t *buffer;
    pRenderClient->GetBuffer(requestBuffer, &buffer);
    if (!buffer)
      return -1;
    int count = requestBuffer * av_get_bytes_per_sample(format) * channels;
    memcpy_s(buffer, count, data, count);
    if (pRenderClient->ReleaseBuffer(requestBuffer, 0) < 0)
      return -1;
    return requestBuffer;
  }
  void start()
  {
    if (!pAudioClient)
      return;
    pAudioClient->Start();
  }
  void stop()
  {
    if (!pAudioClient)
      return;
    pAudioClient->Stop();
  }
  void close()
  {
    if (pwfx)
      CoTaskMemFree(pwfx);
    pwfx = nullptr;
    if (pRenderClient)
      pRenderClient->Release();
    pRenderClient = nullptr;
    if (pAudioClient)
      pAudioClient->Release();
    pAudioClient = nullptr;
    if (pDevice)
      pDevice->Release();
    pDevice = nullptr;
    if (pEnumerator)
      pEnumerator->Release();
    pEnumerator = nullptr;
  }

  AudioClientImpl()
  {
    try
    {
      CoInitialize(NULL);
      CoCreateInstance(
          CLSID_MMDeviceEnumerator, NULL,
          CLSCTX_ALL, IID_IMMDeviceEnumerator,
          (void **)&pEnumerator);
      if (!pEnumerator)
        throw std::exception("Create IMMDeviceEnumerator failed");
      pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
      if (!pDevice)
        throw std::exception("IMMDeviceEnumerator GetDefaultAudioEndpoint failed");
      pDevice->Activate(
          IID_IAudioClient, CLSCTX_ALL,
          NULL, (void **)&pAudioClient);
      if (!pAudioClient)
        throw std::exception("IMMDevice Activate failed");
      pAudioClient->GetMixFormat(&pwfx);
      if (!pwfx)
        throw std::exception("IAudioClient GetMixFormat failed");
      sampleRate = pwfx->nSamplesPerSec;
      channels = pwfx->nChannels;
      format = getSampleFormat(pwfx);
      if (pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 10000000, 0, pwfx, NULL) < 0)
        throw std::exception("IAudioClient Initialize failed");
      pAudioClient->GetBufferSize(&bufferFrameCount);
      if (bufferFrameCount <= 0)
        throw std::exception("IAudioClient GetBufferSize failed");
      pAudioClient->GetService(
          IID_IAudioRenderClient,
          (void **)&pRenderClient);
      if (!pAudioClient)
        throw std::exception("IAudioClient GetService failed");
    }
    catch (std::exception &e)
    {
      close();
      throw e;
    }
  }
};
