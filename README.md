# flvmuxer
a mux tool for mux H.264 NAL Video Data with ADTS-AAC RAW Audio Data write to a flv file or publish RTMP Server with librtmp    
# How to install ?     
1. you must install rtmpdump firstly if you want to publish stream to RTMP Server     
2. then `gcc -o  name xiecc_rtmp.c librtmp.a`             

If you just want to mux to flv file, There's no need to install rtmpdump.         
just run `gcc -o name flv.c`     

If it can help you a little,  I'll be glad .        
     
