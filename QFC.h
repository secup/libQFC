#ifndef __QFC__
#define __QFC__


#if defined _WIN32 || defined __CYGWIN__ || defined __MINGW32__

  // Include Windows specific headers
  #include <windows.h>

  #ifdef BUILDING_DLL
    #ifdef __GNUC__
      #define DLL_PUBLIC __attribute__ ((dllexport))
    #else
      #define DLL_PUBLIC __declspec(dllexport) // Note: actually gcc seems to also supports this syntax.
    #endif
  #else
    #ifdef __GNUC__
      #define DLL_PUBLIC __attribute__ ((dllimport))
    #else
      #define DLL_PUBLIC __declspec(dllimport) // Note: actually gcc seems to also supports this syntax.
    #endif
  #endif
  #define DLL_LOCAL
#else

  #include <pthread.h>
  #include<unistd.h>

  #if __GNUC__ >= 4
    #define DLL_PUBLIC __attribute__ ((visibility ("default")))
    #define DLL_LOCAL  __attribute__ ((visibility ("hidden")))

    



  #else
    #define DLL_PUBLIC
    #define DLL_LOCAL
  #endif
#endif

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>

// Maximum number of cams that we can process internally.
#define MAX_CAMS 9

// Maximum number of buffered frames
#define MAX_BUFFERED_FRAMES 30


// PUBLIC API

int8_t DLL_PUBLIC startStreaming(uint8_t camId, const char* rtspAddress, uint32_t width, uint32_t height);
void* ffmpegStartStreaming(void *arg); // thread function


uint8_t DLL_PUBLIC isCapturing(uint8_t camId);

// End of PUBLIC API


void DLL_LOCAL launchThread(void *args);


struct cam_st {
    uint8_t camId;
    char rtspAddress[100];
    uint8_t isConnected;
    uint8_t isCapturing;
    uint8_t isEOS;
    uint32_t requestedWidth;
    uint32_t requestedHeight;

    // YUV420p array


};
typedef struct cam_st cam_t;

cam_t cams[MAX_CAMS];

#endif