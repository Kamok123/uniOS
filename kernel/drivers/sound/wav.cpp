#include "wav.h"
#include "debug.h"
#include "unifs.h"

WavHeader* wav_open(const char* filename, uint8_t** data, uint32_t* data_size) {
    UniFSFile file;
    if (!unifs_open_into(filename, &file)) {
        DEBUG_ERROR("%s: unifs_open_into failed", filename);
        return nullptr;
    }

    if (file.size <= sizeof(WavHeader)) {
        DEBUG_ERROR("%s: invalid or corrupted wav file", filename);
        return nullptr;
    }

    WavHeader* wav = (WavHeader*)(uint64_t)file.data;

    if (wav->wave[0] != 'W' || wav->wave[1] != 'A' || wav->wave[2] != 'V' || wav->wave[3] != 'E') {
        DEBUG_ERROR("%s: invalid wav header", filename);
        return nullptr;
    }

    DEBUG_INFO("%s: format=%d sample_rate=%d bps=%d channels=%d data_size=%d", filename, wav->audio_format, wav->samples, wav->bits_per_sample, wav->channels, wav->data_size);
    if (wav->audio_format == 0 || wav->samples == 0 || wav->channels == 0 || wav->data_size == 0) {
        DEBUG_ERROR("%s: invalid wav data", filename);
        return nullptr;
    }

    // Only PCM format supported
    if (wav->audio_format != 1) {
        DEBUG_ERROR("%s: non-pcm format is not supported", filename);
        return nullptr;
    }

    // Only 16-bit stereo supported
    if (wav->channels != 2 || wav->bits_per_sample != 16) {
        DEBUG_ERROR("%s: only 16-bit stereo data is supported", filename);
        return nullptr;
    }

    *data = &wav->data_;
    *data_size = wav->data_size;
    return wav;
}
