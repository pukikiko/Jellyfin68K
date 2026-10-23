/* Streaming PCM playback for the Jellyfin 68K client.
 *
 * Jellyfin transcodes to raw 16-bit big-endian PCM.  The Settings menu picks
 * the sample rate and channel count (8 kHz mono through 44.1 kHz stereo).
 * Packets are appended to a small ring buffer as they arrive; there is no
 * decoding on the Mac, so playback stays ahead of the 68LC040 even without
 * an FPU. */
#ifndef JF_AUDIO_H
#define JF_AUDIO_H

/* Set up the Sound Manager stream.  Nothing is played until enough PCM has
 * been written.  Returns 0 on success. */
int AudioPcmBegin(long rate, int channels, int bits);

/* Append PCM bytes from the HTTP body.  Returns 0 if accepted, 1 if the
 * ring is full (retry later), or -1 on a fatal error. */
int AudioPcmWrite(const unsigned char *data, long length);

/* The HTTP body is complete.  Starts playback if the track was shorter than
 * the priming threshold. */
void AudioPcmEnd(void);

/* Seconds of playback so far, or -1 when not playing. */
long AudioPlaySeconds(void);

void AudioStop(void);
int AudioIsPlaying(void);

#endif
