#include <stdio.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>


typedef struct {
	AVFormatContext *fmt_ctx;
	AVStream *stream;
}MyFmtContext;




static int bit_stream_filter_deal(AVBSFContext *bsf_ctx, AVPacket *pkt, AVFormatContext *fmt_ctx)
{
	int ret = 0;

	if ((ret = av_bsf_send_packet(bsf_ctx, pkt)) < 0)
	{
		fprintf(stderr, "av_bsf_send_packet failed.\n");
		return -1;
	}

	while ((ret = av_bsf_receive_packet(bsf_ctx, pkt)) >= 0)
	{
		if (av_interleaved_write_frame(fmt_ctx, pkt) < 0)
		{
			fprintf(stderr, "av_interleaved_write_frame for video failed.\n");
			return -1;
		}
	}

	if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
	{
		fprintf(stderr, "av_bsf_receive_packet failed.\n");
		return -1;
	}

	return 0;
}

static int bit_stream_filter_init(const char *bit_stream_filter_name, AVBSFContext **bsf_ctx, AVStream *stream)
{
	int ret = 0;
	const AVBitStreamFilter *bs_filter = NULL;

	if ((bs_filter = av_bsf_get_by_name(bit_stream_filter_name)) == NULL)
	{
		fprintf(stderr, "av_bsf_get_by_name failed.\n");
		return -1;
	}

	if ((ret = av_bsf_alloc(bs_filter, bsf_ctx)) < 0)
	{
		fprintf(stderr, "av_bsf_alloc failed.\n");
		return -1;
	}

	if ((ret = avcodec_parameters_copy((*bsf_ctx)->par_in, stream->codecpar)) < 0)
	{
		fprintf(stderr, "avcodec_parameters_copy failed.\n");
		return -1;
	}
	(*bsf_ctx)->time_base_in = stream->time_base;

	if ((ret = av_bsf_init(*bsf_ctx)) < 0)
	{
		fprintf(stderr, "av_bsf_init failed.\n");
		return -1;
	}

	return 0;
}



static int create_output_fmt_ctx(MyFmtContext *mfmt_ctx, AVStream *stream_in, const char *filename)
{
	int ret = 0;
	AVStream *out_stream = NULL;
	AVCodecContext * cdc_ctx = NULL;

	if ((ret = avformat_alloc_output_context2(&mfmt_ctx->fmt_ctx, NULL, NULL, filename)) < 0)
	{
		fprintf(stderr, "avformat_alloc_output_context2 for %s failed.\n", filename);
		goto ret1;
	}

	if ((out_stream = avformat_new_stream(mfmt_ctx->fmt_ctx, NULL)) == NULL)
	{
		fprintf(stderr, "avformat_new_stream for %s \n", filename);
		goto ret2;
	}

	if ((cdc_ctx = avcodec_alloc_context3(NULL)) == NULL)
	{
		fprintf(stderr, "ready copy failed.\n");
		goto ret2;
	}

	if ((ret = avcodec_parameters_to_context(cdc_ctx, stream_in->codecpar)) < 0)
	{
		fprintf(stderr, "avcodec_parameters_to_context for %s failed.\n", filename);
		goto ret3;
	}
	cdc_ctx->codec_tag = 0;
	if (mfmt_ctx->fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
		cdc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	if ((ret = avcodec_parameters_from_context(out_stream->codecpar, cdc_ctx)) < 0)
	{
		fprintf(stderr, "avcodec_parameters_from_context for %s failed.\n", filename);
		goto ret3;
	}

	mfmt_ctx->stream = out_stream;

	avcodec_free_context(&cdc_ctx);
	return 0;
ret3:
	avcodec_free_context(&cdc_ctx);
ret2:
	avformat_free_context(mfmt_ctx->fmt_ctx);
ret1:
	return -1;
}



void demuxer_simple(const char *input_file, const char *output_video, const char *output_audio)
{
	int ret;
	AVFormatContext *fmt_ctx = NULL;
	MyFmtContext fmt_ctx_v = {0};
	MyFmtContext fmt_ctx_a = {0};
	AVPacket *pkt = NULL;
	int video_stream_index = -1, audio_stream_index = -1;
	AVStream *input_stream_v = NULL, *input_stream_a = NULL;
	AVBSFContext *bsf_ctx = NULL;

	/*打开输入文件(.mp4)，找到视频流和音频流*/
	if ((ret = avformat_open_input(&fmt_ctx, input_file, NULL, NULL)) < 0)
	{
		fprintf(stderr, "avformat_open_input failed.\n");
		goto ret1;
	}

	if ((ret = avformat_find_stream_info(fmt_ctx, NULL)) < 0)
	{
		fprintf(stderr, "avformat_find_stream_info failed.\n");
		goto ret2;
	}

	if ((ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0)) < 0)
	{
		fprintf(stderr, "avformat_find_best_stream for video failed.\n");
		goto ret2;
	}
	video_stream_index = ret;
	input_stream_v = fmt_ctx->streams[ret];
	if ((ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0)) < 0)
	{
		fprintf(stderr, "avformat_find_best_stream for audio failed.\n");
		goto ret2;
	}
	audio_stream_index = ret;
	input_stream_a = fmt_ctx->streams[ret];

	/*创建输出文件video(.h264)*/
	if ((ret = create_output_fmt_ctx(&fmt_ctx_v, input_stream_v, output_video)) < 0)
	{
		fprintf(stderr, "create_output_fmt_ctx for video failed.\n");
		goto ret2;
	}

	/*创建输出文件audio(.aac / .mp3)*/
	if ((ret = create_output_fmt_ctx(&fmt_ctx_a, input_stream_a, output_audio)) < 0)
	{
		fprintf(stderr, "create_output_fmt_ctx for audio failed.\n");
		goto ret2;
	}

	if ((pkt = av_packet_alloc()) == NULL)
	{
		fprintf(stderr, "av_packet_alloc failed.\n");
		goto ret2;
	}

	/*初始化bit stream filter，h264需要*/
	if ((ret = bit_stream_filter_init("h264_mp4toannexb", &bsf_ctx, input_stream_v)) < 0)
	{
		fprintf(stderr, "bit_stream_filter_init failed.\n");
		goto ret3;
	}

	if (!(fmt_ctx_v.fmt_ctx->oformat->flags & AVFMT_NOFILE))
	{
		if (avio_open(&fmt_ctx_v.fmt_ctx->pb, output_video, AVIO_FLAG_WRITE) < 0)
		{
			fprintf(stderr, "avio_open video failed.\n");
			goto ret3;
		}
	}

	if (!(fmt_ctx_a.fmt_ctx->oformat->flags & AVFMT_NOFILE))
	{
		if (avio_open(&fmt_ctx_a.fmt_ctx->pb, output_audio, AVIO_FLAG_WRITE) < 0)
		{
			fprintf(stderr, "avio_open audio failed.\n");
			goto ret4;
		}
	}

	/*写文件头*/
	if (avformat_write_header(fmt_ctx_v.fmt_ctx, NULL) < 0)
	{
		fprintf(stderr, "avformat_write_header for video failed.\n");
		goto ret5;
	}

	if (avformat_write_header(fmt_ctx_a.fmt_ctx, NULL) < 0)
	{
		fprintf(stderr, "avformat_write_header for audio failed.\n");
		goto ret5;
	}

	/*demuxer*/
	while (av_read_frame(fmt_ctx, pkt) >= 0)
	{
		if (pkt->size > 0)
		{
			if (pkt->stream_index == video_stream_index)
			{
				pkt->stream_index = fmt_ctx_v.stream->index;

				if (bit_stream_filter_deal(bsf_ctx, pkt, fmt_ctx_v.fmt_ctx) < 0)
					goto ret5;
			}
			else if (pkt->stream_index == audio_stream_index)
			{
				pkt->stream_index = fmt_ctx_a.stream->index;

				if ((ret = av_interleaved_write_frame(fmt_ctx_a.fmt_ctx, pkt)) < 0)
				{
					fprintf(stderr, "av_interleaved_write_frame for audio failed.\n");
					fprintf(stderr, "av_interleaved_write_frame for audio failed:%s.\n", av_err2str(ret));
					goto ret5;
				}
			}
		
			av_packet_unref(pkt);
		}
	}

	av_write_trailer(fmt_ctx_v.fmt_ctx);
	av_write_trailer(fmt_ctx_a.fmt_ctx);

	av_bsf_free(&bsf_ctx);
	av_packet_free(&pkt);
	if (!(fmt_ctx_a.fmt_ctx->oformat->flags & AVFMT_NOFILE))
		avio_close(fmt_ctx_a.fmt_ctx->pb);
	if (!(fmt_ctx_v.fmt_ctx->oformat->flags & AVFMT_NOFILE))
		avio_close(fmt_ctx_v.fmt_ctx->pb);
	avformat_free_context(fmt_ctx_v.fmt_ctx);
	avformat_free_context(fmt_ctx_a.fmt_ctx);
	avformat_close_input(&fmt_ctx);
	return;
ret5:
	if (!(fmt_ctx_a.fmt_ctx->oformat->flags & AVFMT_NOFILE))
		avio_close(fmt_ctx_a.fmt_ctx->pb);
ret4:
	if (!(fmt_ctx_v.fmt_ctx->oformat->flags & AVFMT_NOFILE))
		avio_close(fmt_ctx_v.fmt_ctx->pb);
ret3:
	av_packet_free(&pkt);
ret2:
	avformat_close_input(&fmt_ctx);
ret1:
	return;
}

int main(int argc, const char *argv[])
{
	if (argc < 4)
	{
		fprintf(stderr, "Usage: <input file> <output file video> <output file audio>\n");
		return -1;
	}

	demuxer_simple(argv[1], argv[2], argv[3]);
	
	return 0;
}
