CFLAGS := -I /usr/local/ffmpeg/include/
CFLAGS += -I /usr/local/x264/include/
CFLAGS += -I /usr/local/mp3lame/include/
LDFLAGS := -L /usr/local/ffmpeg/lib/ -lavcodec -lavutil -lavformat -lswresample
LDFLAGS += -L /usr/local/x264/lib/ -lx264
LDFLAGS += -L /usr/local/mp3lame/lib -lmp3lame

demuxer:demuxer.c
	gcc $< -o $@ $(CFLAGS) $(LDFLAGS)
