#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rtmp.h"
#include "log.h"
#include "xiecc_rtmp.h"

#define AAC_ADTS_HEADER_SIZE 7
#define FLV_TAG_HEAD_LEN 11
#define FLV_PRE_TAG_LEN 4

typedef struct {
    uint8_t audio_object_type;
    uint8_t sample_frequency_index;
    uint8_t channel_configuration;
}AudioSpecificConfig;

typedef struct 
{
    RTMP *rtmp;
    AudioSpecificConfig config;
    uint32_t audio_config_ok;
}RTMP_XIECC;


static AudioSpecificConfig gen_config(uint8_t *frame)
{
    AudioSpecificConfig config = {0, 0, 0};

    if (frame == NULL) {
        return config;
    }
    config.audio_object_type = (frame[2] & 0xc0) >> 6;
    config.sample_frequency_index =  (frame[2] & 0x3c) >> 2;
    config.channel_configuration = (frame[3] & 0xc0) >> 6;
    return config;
}

static uint8_t gen_audio_tag_header(AudioSpecificConfig config)
{
     uint8_t soundType = config.channel_configuration - 1; //0 mono, 1 stero
     uint8_t soundRate = 0;
     uint8_t val = 0;


     switch (config.sample_frequency_index) {
         case 10: { //11.025k
             soundRate = 1;
             break;
         }
         case 7: { //22k
             soundRate = 2;
             break;
         }
         case 4: { //44k
             soundRate = 3;
             break;
         }
         default:
         { 
             return val;
         }
    }
    val = 0xA0 | (soundRate << 2) | 0x02 | soundType;
    return val;
}
static uint8_t *get_adts(uint32_t *len, uint8_t **offset, uint8_t *start, uint32_t total)
{
    uint8_t *p  =  *offset;
    uint32_t frame_len_1;
    uint32_t frame_len_2;
    uint32_t frame_len_3;
    uint32_t frame_length;
   
    if (total < AAC_ADTS_HEADER_SIZE) {
        return NULL;
    }
    if ((p - start) >= total) {
        return NULL;
    }
    
    if (p[0] != 0xff) {
        return NULL;
    }
    if ((p[1] & 0xf0) != 0xf0) {
        return NULL;
    }
    frame_len_1 = p[3] & 0x03;
    frame_len_2 = p[4];
    frame_len_3 = (p[5] & 0xe0) >> 5;
    frame_length = (frame_len_1 << 11) | (frame_len_2 << 3) | frame_len_3;
    *offset = p + frame_length;
    *len = frame_length;
    return p;
}



// @brief alloc function
// @param [in] url     : RTMP URL, rtmp://127.0.0.1/live/xxx
// @return             : rtmp_sender handler
void *rtmp_sender_alloc(const char *url) //return handle
{
    RTMP_XIECC *rtmp_xiecc; 
    RTMP *rtmp; 

    if (url == NULL) {
        return NULL;
    }
    RTMP_LogSetLevel(RTMP_LOGDEBUG);
    rtmp = RTMP_Alloc();
    RTMP_Init(rtmp);
    rtmp->Link.timeout = 10; //10seconds
    rtmp->Link.lFlags |= RTMP_LF_LIVE;

    if (!RTMP_SetupURL(rtmp, (char *)url)) {
        RTMP_Log(RTMP_LOGWARNING, "Couldn't set the specified url (%s)!", url);
        RTMP_Free(rtmp);
        return NULL;
    }

    RTMP_EnableWrite(rtmp);
    rtmp_xiecc = calloc(1, sizeof(RTMP_XIECC));
    rtmp_xiecc->rtmp = rtmp;
    return (void *)rtmp_xiecc;
}

// @brief start publish
// @param [in] rtmp_sender handler
// @param [in] flag        stream falg
// @param [in] ts_us       timestamp in us
// @return             : 0: OK; others: FAILED
int rtmp_sender_start_publish(void *handle, uint32_t flag, int64_t ts_us)
{
    RTMP_XIECC *rtmp_xiecc = (RTMP_XIECC *)handle; 
    RTMP *rtmp; 

    if (rtmp_xiecc == NULL) {
        return 1;
    }
    rtmp = rtmp_xiecc->rtmp; 
    if (!RTMP_Connect(rtmp, NULL) || !RTMP_ConnectStream(rtmp, 0))  {
        return 1;
    }
    return 0;
}

// @brief stop publish
// @param [in] rtmp_sender handler
// @return             : 0: OK; others: FAILED
int rtmp_sender_stop_publish(void *handle)
{
    RTMP_XIECC *rtmp_xiecc = (RTMP_XIECC *)handle; 
    RTMP *rtmp ;

    if (rtmp_xiecc == NULL) {
        return 1;
    }

    rtmp = rtmp_xiecc->rtmp; 
    RTMP_Close(rtmp);
    return 0;
}

// @brief send audio frame
// @param [in] rtmp_sender handler
// @param [in] data       : AACAUDIODATA
// @param [in] size       : AACAUDIODATA size
// @param [in] dts_us     : decode timestamp of frame
int rtmp_sender_write_audio_frame(void *handle,
        uint8_t *data,
        int size,
        uint64_t dts_us)
{
    RTMP_XIECC *rtmp_xiecc = (RTMP_XIECC *)handle; 
    RTMP *rtmp ;
    uint32_t audio_ts = (uint32_t)dts_us;
    uint8_t * audio_buf = data; 
    uint32_t audio_total = size;
    uint8_t *audio_buf_offset = audio_buf;
    uint8_t *audio_frame;
    uint32_t adts_len;
    uint32_t offset;
    uint32_t body_len;
    uint32_t output_len;
    char *output ; 

    if ((data == NULL) || (rtmp_xiecc == NULL)) {
        return 1;
    }
    rtmp = rtmp_xiecc->rtmp; 
    while (1) {
    //Audio OUTPUT
    offset = 0;
    audio_frame = get_adts(&adts_len, &audio_buf_offset, audio_buf, audio_total);
    if (audio_frame == NULL) break;
    if (rtmp_xiecc->audio_config_ok == 0) {
        rtmp_xiecc->config = gen_config(audio_frame);
        body_len = 2 + 2; //AudioTagHeader + AudioSpecificConfig
        output_len = body_len + FLV_TAG_HEAD_LEN + FLV_PRE_TAG_LEN;
        output = malloc(output_len);
        // flv tag header
        output[offset++] = 0x08; //tagtype video
        output[offset++] = (uint8_t)(body_len >> 16); //data len
        output[offset++] = (uint8_t)(body_len >> 8); //data len
        output[offset++] = (uint8_t)(body_len); //data len
        output[offset++] = (uint8_t)(audio_ts >> 16); //time stamp
        output[offset++] = (uint8_t)(audio_ts >> 8); //time stamp
        output[offset++] = (uint8_t)(audio_ts); //time stamp
        output[offset++] = (uint8_t)(audio_ts >> 24); //time stamp
        output[offset++] = 0x00; //stream id 0
        output[offset++] = 0x00; //stream id 0
        output[offset++] = 0x00; //stream id 0

        //flv AudioTagHeader
        output[offset++] = gen_audio_tag_header(rtmp_xiecc->config); // sound format aac
        output[offset++] = 0x00; //aac sequence header

        //flv VideoTagBody --AudioSpecificConfig
        uint8_t audio_object_type = rtmp_xiecc->config.audio_object_type + 1;
        output[offset++] = (audio_object_type << 3)|(rtmp_xiecc->config.sample_frequency_index >> 1); 
        output[offset++] = ((rtmp_xiecc->config.sample_frequency_index & 0x01) << 7) \
                           | (rtmp_xiecc->config.channel_configuration << 3) ;
        //no need to set pre_tag_size
        /*
        uint32_t fff = body_len + FLV_TAG_HEAD_LEN;
        output[offset++] = (uint8_t)(fff >> 24); //data len
        output[offset++] = (uint8_t)(fff >> 16); //data len
        output[offset++] = (uint8_t)(fff >> 8); //data len
        output[offset++] = (uint8_t)(fff); //data len
        */
        RTMP_Write(rtmp, output, output_len);
        free(output);
        rtmp_xiecc->audio_config_ok = 1;
    }else
    {
        body_len = 2 + adts_len - AAC_ADTS_HEADER_SIZE; // remove adts header + AudioTagHeader
        output_len = body_len + FLV_TAG_HEAD_LEN + FLV_PRE_TAG_LEN;
        output = malloc(output_len);
        // flv tag header
        output[offset++] = 0x08; //tagtype video
        output[offset++] = (uint8_t)(body_len >> 16); //data len
        output[offset++] = (uint8_t)(body_len >> 8); //data len
        output[offset++] = (uint8_t)(body_len); //data len
        output[offset++] = (uint8_t)(audio_ts >> 16); //time stamp
        output[offset++] = (uint8_t)(audio_ts >> 8); //time stamp
        output[offset++] = (uint8_t)(audio_ts); //time stamp
        output[offset++] = (uint8_t)(audio_ts >> 24); //time stamp
        output[offset++] = 0x00; //stream id 0
        output[offset++] = 0x00; //stream id 0
        output[offset++] = 0x00; //stream id 0

        //flv AudioTagHeader
        output[offset++] = gen_audio_tag_header(rtmp_xiecc->config); // sound format aac
        output[offset++] = 0x01; //aac raw data 

        //flv VideoTagBody --raw aac data
        memcpy(output + offset, audio_frame + AAC_ADTS_HEADER_SIZE,\
                 (adts_len - AAC_ADTS_HEADER_SIZE)); //H264 sequence parameter set
        /*
        //previous tag size 
        uint32_t fff = body_len + FLV_TAG_HEAD_LEN;
        offset += (adts_len - AAC_ADTS_HEADER_SIZE);
        output[offset++] = (uint8_t)(fff >> 24); //data len
        output[offset++] = (uint8_t)(fff >> 16); //data len
        output[offset++] = (uint8_t)(fff >> 8); //data len
        output[offset++] = (uint8_t)(fff); //data len
        */
        RTMP_Write(rtmp, output, output_len);
        free(output);
     }
    } //end while 1
    return 0;
}

static uint32_t find_start_code(uint8_t *buf, uint32_t zeros_in_startcode)   
{   
  uint32_t info;   
  uint32_t i;   
   
  info = 1;   
  if ((info = (buf[zeros_in_startcode] != 1)? 0: 1) == 0)   
      return 0;   
       
  for (i = 0; i < zeros_in_startcode; i++)   
    if (buf[i] != 0)   
    { 
        info = 0;
        break;
    };   
     
  return info;   
}   

static uint8_t * get_nal(uint32_t *len, uint8_t **offset, uint8_t *start, uint32_t total)
{
    uint32_t info;
    uint8_t *q ;
    uint8_t *p  =  *offset;
    *len = 0;

    while(1) {
        info =  find_start_code(p, 3);
        if (info == 1)
            break;
        p++;
        if ((p - start) >= total)
            return NULL;
    }
    q = p + 4;
    p = q;
    while(1) {
        info =  find_start_code(p, 3);
        if (info == 1)
            break;
        p++;
        if ((p - start) >= total)
            return NULL;
    }
    
    *len = (p - q);
    *offset = p;
    return q;
}

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
        int key)
{
    uint8_t * buf; 
    uint8_t * buf_offset;
    int total;
    uint32_t ts;
    uint32_t nal_len;
    uint32_t nal_len_n;
    uint8_t *nal; 
    uint8_t *nal_n;
    char *output ; 
    uint32_t offset = 0;
    uint32_t body_len;
    uint32_t output_len;
    RTMP_XIECC *rtmp_xiecc;
    RTMP *rtmp;


    buf = data;
    buf_offset = data;
    total = size;
    ts = (uint32_t)dts_us;
    rtmp_xiecc = (RTMP_XIECC *)handle; 
    if ((data == NULL) || (rtmp_xiecc == NULL)) {
        return 1;
    }
    rtmp = rtmp_xiecc->rtmp; 

    while (1) {
    //ts = RTMP_GetTime() - start_time;
    offset = 0;
    nal = get_nal(&nal_len, &buf_offset, buf, total);
    if (nal == NULL) break;
    if (nal[0] == 0x67)  {
        nal_n  = get_nal(&nal_len_n, &buf_offset, buf, total); //get pps
        if (nal == NULL) {
            RTMP_Log(RTMP_LOGERROR, "No Nal after SPS");
            break;
        }
        body_len = nal_len + nal_len_n + 16;
        output_len = body_len + FLV_TAG_HEAD_LEN + FLV_PRE_TAG_LEN;
        output = malloc(output_len);
        // flv tag header
        output[offset++] = 0x09; //tagtype video
        output[offset++] = (uint8_t)(body_len >> 16); //data len
        output[offset++] = (uint8_t)(body_len >> 8); //data len
        output[offset++] = (uint8_t)(body_len); //data len
        output[offset++] = (uint8_t)(ts >> 16); //time stamp
        output[offset++] = (uint8_t)(ts >> 8); //time stamp
        output[offset++] = (uint8_t)(ts); //time stamp
        output[offset++] = (uint8_t)(ts >> 24); //time stamp
        output[offset++] = 0x00; //stream id 0
        output[offset++] = 0x00; //stream id 0
        output[offset++] = 0x00; //stream id 0

        //flv VideoTagHeader
        output[offset++] = 0x17; //key frame, AVC
        output[offset++] = 0x00; //avc sequence header
        output[offset++] = 0x00; //composit time ??????????
        output[offset++] = 0x00; // composit time
        output[offset++] = 0x00; //composit time

        //flv VideoTagBody --AVCDecoderCOnfigurationRecord
        output[offset++] = 0x01; //configurationversion
        output[offset++] = nal[1]; //avcprofileindication
        output[offset++] = nal[2]; //profilecompatibilty
        output[offset++] = nal[3]; //avclevelindication
        output[offset++] = 0xff; //reserved + lengthsizeminusone
        output[offset++] = 0xe1; //numofsequenceset
        output[offset++] = (uint8_t)(nal_len >> 8); //sequence parameter set length high 8 bits
        output[offset++] = (uint8_t)(nal_len); //sequence parameter set  length low 8 bits
        memcpy(output + offset, nal, nal_len); //H264 sequence parameter set
        offset += nal_len;
        output[offset++] = 0x01; //numofpictureset
        output[offset++] = (uint8_t)(nal_len_n >> 8); //picture parameter set length high 8 bits
        output[offset++] = (uint8_t)(nal_len_n); //picture parameter set length low 8 bits
        memcpy(output + offset, nal_n, nal_len_n); //H264 picture parameter set

        //no need set pre_tag_size ,RTMP NO NEED
        // flv test 
        /*
        offset += nal_len_n;
        uint32_t fff = body_len + FLV_TAG_HEAD_LEN;
        output[offset++] = (uint8_t)(fff >> 24); //data len
        output[offset++] = (uint8_t)(fff >> 16); //data len
        output[offset++] = (uint8_t)(fff >> 8); //data len
        output[offset++] = (uint8_t)(fff); //data len
        */
        RTMP_Write(rtmp, output, output_len);
        //RTMP Send out
        free(output);
        continue;
    }

    if (nal[0] == 0x65)
    {
        body_len = nal_len + 5 + 4; //flv VideoTagHeader +  NALU length
        output_len = body_len + FLV_TAG_HEAD_LEN + FLV_PRE_TAG_LEN;
        output = malloc(output_len);
        // flv tag header
        output[offset++] = 0x09; //tagtype video
        output[offset++] = (uint8_t)(body_len >> 16); //data len
        output[offset++] = (uint8_t)(body_len >> 8); //data len
        output[offset++] = (uint8_t)(body_len); //data len
        output[offset++] = (uint8_t)(ts >> 16); //time stamp
        output[offset++] = (uint8_t)(ts >> 8); //time stamp
        output[offset++] = (uint8_t)(ts); //time stamp
        output[offset++] = (uint8_t)(ts >> 24); //time stamp
        output[offset++] = 0x00; //stream id 0
        output[offset++] = 0x00; //stream id 0
        output[offset++] = 0x00; //stream id 0

        //flv VideoTagHeader
        output[offset++] = 0x17; //key frame, AVC
        output[offset++] = 0x01; //avc NALU unit
        output[offset++] = 0x00; //composit time ??????????
        output[offset++] = 0x00; // composit time
        output[offset++] = 0x00; //composit time

        output[offset++] = (uint8_t)(nal_len >> 24); //nal length 
        output[offset++] = (uint8_t)(nal_len >> 16); //nal length 
        output[offset++] = (uint8_t)(nal_len >> 8); //nal length 
        output[offset++] = (uint8_t)(nal_len); //nal length 
        memcpy(output + offset, nal, nal_len);

        //no need set pre_tag_size ,RTMP NO NEED
        /*
        offset += nal_len;
        uint32_t fff = body_len + FLV_TAG_HEAD_LEN;
        output[offset++] = (uint8_t)(fff >> 24); //data len
        output[offset++] = (uint8_t)(fff >> 16); //data len
        output[offset++] = (uint8_t)(fff >> 8); //data len
        output[offset++] = (uint8_t)(fff); //data len
        */
        RTMP_Write(rtmp, output, output_len);
        //RTMP Send out
        free(output);
        continue;
     }

    if ((nal[0] & 0x1f) == 0x01)
    {
        body_len = nal_len + 5 + 4; //flv VideoTagHeader +  NALU length
        output_len = body_len + FLV_TAG_HEAD_LEN + FLV_PRE_TAG_LEN;
        output = malloc(output_len);
        // flv tag header
        output[offset++] = 0x09; //tagtype video
        output[offset++] = (uint8_t)(body_len >> 16); //data len
        output[offset++] = (uint8_t)(body_len >> 8); //data len
        output[offset++] = (uint8_t)(body_len); //data len
        output[offset++] = (uint8_t)(ts >> 16); //time stamp
        output[offset++] = (uint8_t)(ts >> 8); //time stamp
        output[offset++] = (uint8_t)(ts); //time stamp
        output[offset++] = (uint8_t)(ts >> 24); //time stamp
        output[offset++] = 0x00; //stream id 0
        output[offset++] = 0x00; //stream id 0
        output[offset++] = 0x00; //stream id 0

        //flv VideoTagHeader
        output[offset++] = 0x27; //not key frame, AVC
        output[offset++] = 0x01; //avc NALU unit
        output[offset++] = 0x00; //composit time ??????????
        output[offset++] = 0x00; // composit time
        output[offset++] = 0x00; //composit time

        output[offset++] = (uint8_t)(nal_len >> 24); //nal length 
        output[offset++] = (uint8_t)(nal_len >> 16); //nal length 
        output[offset++] = (uint8_t)(nal_len >> 8); //nal length 
        output[offset++] = (uint8_t)(nal_len); //nal length 
        memcpy(output + offset, nal, nal_len);

        //no need set pre_tag_size ,RTMP NO NEED
        /*
        offset += nal_len;
        uint32_t fff = body_len + FLV_TAG_HEAD_LEN;
        output[offset++] = (uint8_t)(fff >> 24); //data len
        output[offset++] = (uint8_t)(fff >> 16); //data len
        output[offset++] = (uint8_t)(fff >> 8); //data len
        output[offset++] = (uint8_t)(fff); //data len
        */
        RTMP_Write(rtmp, output, output_len);

       //RTMP Send out
        free(output);
        continue;
     }
   }
   return 0;
}

// @brief free rtmp_sender handler
// @param [in] rtmp_sender handler
void rtmp_sender_free(void *handle)
{
    RTMP_XIECC *rtmp_xiecc;
    RTMP *rtmp;

    if (handle == NULL) {
        return;
    }

    rtmp_xiecc = (RTMP_XIECC *)handle; 
    rtmp = rtmp_xiecc->rtmp; 
    if (rtmp != NULL) {
        RTMP_Free(rtmp);
    }
    free(rtmp_xiecc);
}

