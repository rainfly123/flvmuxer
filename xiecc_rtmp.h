#ifndef _XIECC_RTMP_H_
#define _XIECC_RTMP_H_
#include <stdint.h>

#ifdef __cplusplus
extern "C"{
#endif

#define RTMP_STREAM_PROPERTY_PUBLIC      0x00000001
#define RTMP_STREAM_PROPERTY_ALARM       0x00000002
#define RTMP_STREAM_PROPERTY_RECORD      0x00000004

// @brief alloc function
// @param [in] url     : RTMP URL, rtmp://127.0.0.1/live/xxx
// @return             : rtmp_sender handler
void *rtmp_sender_alloc(const char *url); //return handle

// @brief start publish
// @param [in] rtmp_sender handler
// @param [in] flag        stream falg
// @param [in] ts_us       timestamp in us
// @return             : 0: OK; others: FAILED
int rtmp_sender_start_publish(void *handle, uint32_t flag, int64_t ts_us);


// @brief set stream property 
// @param [in] rtmp_sender handler
// @param [in] flag        stream_property bits
// @param [in] ext         extern params in json
// @return             : 0: OK; others: FAILED
int rtmp_sender_set_stream_property(void *handle, uint32_t flag, const char *ext);

// @brief stop publish
// @param [in] rtmp_sender handler
// @return             : 0: OK; others: FAILED
int rtmp_sender_stop_publish(void *handle);

//@brief tell the server start record current stream
//@param handle rtmp_sender handle
int rtmp_sender_start_record(void *handle);

//@brief tell the server stop record current stream
//@param handle rtmp_sender handle
int rtmp_sender_stop_record(void *handle);
// @brief send audio frame
// @param [in] rtmp_sender handler
// @param [in] data       : AACAUDIODATA
// @param [in] size       : AACAUDIODATA size
// @param [in] dts_us     : decode timestamp of frame
int rtmp_sender_write_audio_frame(void *handle,
        uint8_t *data,
        int size,
        uint64_t dts_us,
        uint32_t abs_ts);

// @brief send video frame, now only H264 supported
// @param [in] rtmp_sender handler
// @param [in] data       : video data, (Full frames are required)
// @param [in] size       : video data size
// @param [in] dts_us     : decode timestamp of frame
// @param [in] key        : key frame indicate, [0: non key] [1: key]
int rtmp_sender_write_video_frame(void *handle,
        uint8_t *data,
        int size,
        uint64_t dts_us,
        int key,
        uint32_t abs_ts);

// @brief free rtmp_sender handler
// @param [in] rtmp_sender handler
void rtmp_sender_free(void *handle);

#ifdef __cplusplus
}
#endif
#endif
