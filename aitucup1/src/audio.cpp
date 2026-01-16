#include "audio.h"
#include "config.h"
#include "driver/i2s.h"

static bool micInstalled = false;
static bool spkInstalled = false;

static void stopIfInstalled(i2s_port_t port, bool& flag) {
  if (!flag) return;
  i2s_stop(port);
  i2s_driver_uninstall(port);
  flag = false;
}

void audioInitMic() {
  stopIfInstalled(I2S_NUM_0, micInstalled);
  stopIfInstalled(I2S_NUM_1, spkInstalled);

  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  cfg.sample_rate = MIC_SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_I2S;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;

  i2s_pin_config_t pins = {};
  pins.bck_io_num = PIN_BCLK;
  pins.ws_io_num  = PIN_WS;
  pins.data_in_num = PIN_DIN;
  pins.data_out_num = I2S_PIN_NO_CHANGE;

  i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pins);
  i2s_zero_dma_buffer(I2S_NUM_0);

  micInstalled = true;
}

void audioInitSpeaker(int sampleRate) {
  stopIfInstalled(I2S_NUM_0, micInstalled);
  stopIfInstalled(I2S_NUM_1, spkInstalled);

  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = sampleRate;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT; // стерео в i2s_write
  cfg.communication_format = I2S_COMM_FORMAT_I2S;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = 512;
  cfg.use_apll = false;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;

  i2s_pin_config_t pins = {};
  pins.bck_io_num = PIN_BCLK;
  pins.ws_io_num  = PIN_WS;
  pins.data_out_num = PIN_DOUT;
  pins.data_in_num = I2S_PIN_NO_CHANGE;

  i2s_driver_install(I2S_NUM_1, &cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_1, &pins);

  spkInstalled = true;
}

bool micRecordPcm16(int16_t* out, size_t samples) {
  size_t need = samples * sizeof(int16_t);
  size_t got = 0;
  uint8_t* p = (uint8_t*)out;

  while (got < need) {
    size_t r = 0;
    size_t chunk = min((size_t)1024, need - got);
    if (i2s_read(I2S_NUM_0, p + got, chunk, &r, portMAX_DELAY) != ESP_OK) return false;
    got += r;
  }
  return true;
}

static uint32_t rd32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t rd16(const uint8_t* p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

bool speakerPlayWav(const uint8_t* data, size_t len) {
  if (len < 44) return false;
  if (rd32(data + 0) != 0x46464952) return false; // "RIFF"
  if (rd32(data + 8) != 0x45564157) return false; // "WAVE"

  // ищем chunk "fmt " и "data"
  uint16_t audioFormat = 0, numCh = 0, bits = 0;
  uint32_t sampleRate = 0;
  size_t pos = 12;
  size_t dataPos = 0, dataLen = 0;

  while (pos + 8 <= len) {
    uint32_t id = rd32(data + pos);
    uint32_t sz = rd32(data + pos + 4);
    pos += 8;
    if (pos + sz > len) break;

    if (id == 0x20746D66) { // "fmt "
      audioFormat = rd16(data + pos + 0);
      numCh      = rd16(data + pos + 2);
      sampleRate = rd32(data + pos + 4);
      bits       = rd16(data + pos + 14);
    } else if (id == 0x61746164) { // "data"
      dataPos = pos;
      dataLen = sz;
      break;
    }
    pos += sz;
    if (sz & 1) pos++; // padding
  }

  if (!dataPos || !dataLen) return false;
  if (audioFormat != 1 || bits != 16) return false; // только PCM16

  audioInitSpeaker((int)sampleRate);
  delay(30);

  const uint8_t* pcm = data + dataPos;
  size_t pcmLen = min(dataLen, len - dataPos);

  // MAX98357A любит стерео. Если WAV mono — продублируем в стерео.
  if (numCh == 2) {
    size_t written = 0;
    while (written < pcmLen) {
      size_t w = 0;
      size_t chunk = min((size_t)2048, pcmLen - written);
      i2s_write(I2S_NUM_1, pcm + written, chunk, &w, portMAX_DELAY);
      written += w;
    }
  } else {
    const int16_t* mono = (const int16_t*)pcm;
    size_t samples = pcmLen / 2;
    static int16_t stereoBuf[512 * 2];

    size_t i = 0;
    while (i < samples) {
      size_t n = min((size_t)512, samples - i);
      for (size_t k = 0; k < n; k++) {
        int16_t v = mono[i + k];
        stereoBuf[2*k+0] = v;
        stereoBuf[2*k+1] = v;
      }
      size_t w = 0;
      i2s_write(I2S_NUM_1, stereoBuf, n * 4, &w, portMAX_DELAY);
      i += n;
    }
  }

  delay(80);
  return true;
}
