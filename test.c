#include "soooner_rtmp.h"
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#define AAC_ADTS_HEADER_SIZE 7
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

uint8_t * get_nal(uint32_t *len, uint8_t **offset, uint8_t *start, uint32_t total)
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
uint8_t *get_adts(uint32_t *len, uint8_t **offset, uint8_t *start, uint32_t total)
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



int main()
{

    void*p = rtmp_sender_alloc("rtmp://127.0.0.1:1935/live/ddd"); //return handle
    rtmp_sender_start_publish(p, 0, 0);
    int fd = open("cms.264", O_RDONLY);
    uint8_t * buf = malloc(3 *1024 * 1024);
    uint32_t total;
    total = read(fd, buf, (1024*1024 *3));
    close(fd);
    int aacfd = open("audiotest.aac", O_RDONLY); 
    uint8_t * audio_buf = malloc(1 *1024 * 1024);
    uint32_t audio_total;
    audio_total = read(aacfd, audio_buf, (1024*1024 *3));
    close(aacfd);
    uint8_t *buf_offset = buf;
    uint8_t *audio_buf_offset = audio_buf;
    uint32_t len;
    uint32_t audio_len;
    uint8_t *p_video ;
    uint8_t *pp;
    uint8_t *p_audio;
    uint32_t audio_ts = 0;
    uint32_t ts = 0;
    uint32_t len_1;
    uint32_t len_2;
    while (1) {
    p_audio = get_adts(&audio_len, &audio_buf_offset, audio_buf, audio_total);
    if (p_audio == NULL){
        audio_buf_offset = audio_buf;
        continue;
    }
    rtmp_sender_write_audio_frame(p, p_audio, audio_len, audio_ts);

    p_video = get_nal(&len, &buf_offset, buf, total);
    if (p_video == NULL) {
        buf_offset = buf;
        continue;
    }
    printf("%x %d\n", p_video[0], len);
    if (p_video[0] == 0x67) {
            pp = get_nal(&len_1, &buf_offset, buf, total);
            printf("%x %d\n", pp[0], len_1);
            pp = get_nal(&len_2, &buf_offset, buf, total);
            printf("%x %d\n", pp[0], len_2);
            uint8_t temp = len + len_1 + len_2 + 12;
            printf("temp %d\n", temp);
            rtmp_sender_write_video_frame(p, p_video - 4, temp, ts, 0);
    }
    else
       rtmp_sender_write_video_frame(p, p_video - 4, len + 4, ts, 0);
    ts += 50;
    audio_ts += 50;
    usleep(50 * 1000);
    }
}
