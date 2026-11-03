/* Multimedia types referenced by the Miles header and game audio code. The
 * N64 port plays audio through libdragon (src/n64/audio_n64.c), so these are type
 * declarations only. */
#pragma once
#include <windows.h>

typedef UINT MMRESULT;
DECLARE_HANDLE(HWAVEOUT);
DECLARE_HANDLE(HWAVEIN);
DECLARE_HANDLE(HMIDIOUT);
typedef HWAVEOUT *LPHWAVEOUT;
typedef HMIDIOUT *LPHMIDIOUT;
#define MMSYSERR_NOERROR 0
#define WAVE_FORMAT_PCM 1
#define WAVE_FORMAT_IMA_ADPCM 0x0011

typedef struct waveformat_tag { WORD wFormatTag, nChannels; DWORD nSamplesPerSec, nAvgBytesPerSec; WORD nBlockAlign; } WAVEFORMAT, *LPWAVEFORMAT;
typedef struct pcmwaveformat_tag { WAVEFORMAT wf; WORD wBitsPerSample; } PCMWAVEFORMAT, *LPPCMWAVEFORMAT;
typedef struct tWAVEFORMATEX { WORD wFormatTag, nChannels; DWORD nSamplesPerSec, nAvgBytesPerSec; WORD nBlockAlign, wBitsPerSample, cbSize; } WAVEFORMATEX, *LPWAVEFORMATEX;
typedef struct wavehdr_tag { LPSTR lpData; DWORD dwBufferLength, dwBytesRecorded; DWORD_PTR dwUser; DWORD dwFlags, dwLoops; struct wavehdr_tag *lpNext; DWORD_PTR reserved; } WAVEHDR, *LPWAVEHDR;
typedef struct midihdr_tag { LPSTR lpData; DWORD dwBufferLength, dwBytesRecorded; DWORD_PTR dwUser; DWORD dwFlags; struct midihdr_tag *lpNext; DWORD_PTR reserved; DWORD dwOffset; DWORD_PTR dwReserved[8]; } MIDIHDR, *LPMIDIHDR;

#ifdef __cplusplus
extern "C" {
#endif
MMRESULT waveOutReset(HWAVEOUT);
#ifdef __cplusplus
}
#endif
