#include <stdlib.h>
#include <stdio.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
#include <libavformat/avformat.h>

#include <SDL2/SDL.h>

#include <smx-adapter/smx-adapter.h>

//#define NOSCALE
#define FILLGAPS
#define PRECACHE_LEN 0	// queue 30 frames in advance

int bothpads = 0;
int padoutput = 0;

int smxPanelPosX[18] = {
	0, 1, 2,
	0, 1, 2,
	0, 1, 2,
	3, 4, 5,
	3, 4, 5,
	3, 4, 5
};

int smxPanelPosY[18] = {
	0, 0, 0,
	1, 1, 1,
	2, 2, 2,
	0, 0, 0,
	1, 1, 1,
	2, 2, 2
};

int smxLedPosX[25] = {
	0, 2, 4, 6,
	0, 2, 4, 6,
	0, 2, 4, 6,
	0, 2, 4, 6,
	1, 3, 5,
	1, 3, 5,
	1, 3, 5
};

int smxLedPosY[25] = {
	0, 0, 0, 0,
	2, 2, 2, 2,
	4, 4, 4, 4,
	6, 6, 6, 6,
	1, 1, 1,
	3, 3, 3,
	5, 5, 5
};

struct frameQueueNode {
	struct frameQueue *next;

	AVPacket *frame;
};

struct frameQueue {
	SDL_mutex *mutex;
	struct frameQueueNode *head;
	struct frameQueueNode *tail;

	uint64_t len;
};

struct frameQueue *frameQueue_alloc() {
	struct frameQueue *result = malloc(sizeof(struct frameQueue));

	result->head = NULL;
	result->tail = NULL;
	result->mutex = SDL_CreateMutex();
	result->len = 0;

	return result;
}

void frameQueue_enqueue(struct frameQueue *queue, AVPacket *frame) {
	struct frameQueueNode *node = malloc(sizeof(struct frameQueueNode));

	node->frame = av_packet_clone(frame);
	node->next = NULL;

	SDL_LockMutex(queue->mutex);

	if (queue->tail != NULL) {
		queue->tail->next = node;
	} else {
		queue->head = node;
	}

	queue->tail = node;

	queue->len++;

	SDL_UnlockMutex(queue->mutex);
}

AVPacket *frameQueue_dequeue(struct frameQueue *queue) {
	SDL_LockMutex(queue->mutex);

	struct frameQueueNode *node = queue->head;
	queue->head = queue->head->next;

	if (queue->tail == node) {
		queue->tail = NULL;
	}

	queue->len--;

	SDL_UnlockMutex(queue->mutex);

	AVPacket *result = node->frame;
	free(node);

	return result;
}

uint8_t frameQueue_populated(struct frameQueue *queue) {
	SDL_LockMutex(queue->mutex);

	uint8_t result = queue->head != NULL;

	SDL_UnlockMutex(queue->mutex);

	return result;
}

void safeWait(uint64_t endTime) {
	uint64_t timerFreq = SDL_GetPerformanceFrequency();
	uint64_t safetyThreshold = timerFreq / 1000 * 2;

	uint64_t currentTime = SDL_GetPerformanceCounter();

	//printf("%f, %d, %f\n", (double)(nextTime - currentTime) / timerFreq, inFrame->best_effort_timestamp, timebase);

	while (currentTime < endTime) {
		currentTime = SDL_GetPerformanceCounter();

		//printf("%f\n", timerAccumulator);

		if (endTime - currentTime > safetyThreshold) {
			SDL_Delay(1);
		}
	}
}

struct decoderThreadData {
	struct frameQueue *queue;
	uint8_t streaming;

	AVFormatContext *formatContext;
	AVCodecContext *codecContext;

	int targetStream;

	SDL_sem *readySemaphore;
	SDL_sem *playbackSemaphore;
};

void videoThread(void *data) {
	struct decoderThreadData *decoderData = (struct decoderThreadData *)data;
	//struct videoDecoderData *videoDecoderData = (struct videoDecoderData *)decoderData->data;

	int outW, outH;

#ifdef NOSCALE
	outW = decoderData->codecContext->width;
	outH = decoderData->codecContext->height;
#else

#ifdef FILLGAPS
	int dimensionx = 56;
	int dimensiony = 56;
#else 
	int dimensionx = 30;
	int dimensiony = 30;
#endif

	if (bothpads) {
		printf("OUTPUTTING TO BOTH PADS\n");
		#ifdef FILLGAPS
			dimensionx = 125;
		#else 
			dimensionx = 60;
		#endif
	}

	float padAspectRatio, vidAspectRatio;

	if (padoutput) {
		printf("PADDING VIDEO OUTPUT\n");
		padAspectRatio = (double)dimensionx / (double)dimensiony;
		vidAspectRatio = (double)decoderData->codecContext->width / (double)decoderData->codecContext->height;
	} else {
		vidAspectRatio = (double)dimensionx / (double)dimensiony;
		padAspectRatio = (double)decoderData->codecContext->width / (double)decoderData->codecContext->height;
	}
	
	if (padAspectRatio > vidAspectRatio) {
		// pads wider than video
		outW = (double)((double)dimensiony / (double)decoderData->codecContext->height) * decoderData->codecContext->width;
		outH = dimensiony;
	} else if (padAspectRatio < vidAspectRatio) {
		// pads narrower than video
		outH = (double)((double)dimensionx / (double)decoderData->codecContext->width) * decoderData->codecContext->height;
		outW = dimensionx;
	} else {
		outW = dimensionx;
		outH = dimensiony;
	}
	#endif

	struct Swscontext* swsContext = NULL;
	swsContext = sws_getContext(
		decoderData->codecContext->width,
		decoderData->codecContext->height,
		decoderData->codecContext->pix_fmt,
		outW,
		outH,
		AV_PIX_FMT_RGB24,
		SWS_BILINEAR,
		NULL,
		NULL,
		NULL
	);

	int bufferSize = av_image_get_buffer_size(
		AV_PIX_FMT_RGB24,
		decoderData->codecContext->width, 
		decoderData->codecContext->height,
		32
	);

	uint8_t *buffer = NULL;

	buffer = malloc(bufferSize);

	AVFrame *outFrame = av_frame_alloc();

	av_image_fill_arrays(
		outFrame->data,
		outFrame->linesize,
		buffer,
		AV_PIX_FMT_RGB24,
		outW,
		outH,
		32
	);

	AVFrame *inFrame = av_frame_alloc();

	// video output setup

	//double framerate = av_q2d(formatContext->streams[videoStream]->r_frame_rate);
	//double frameInterval = 1.0 / framerate;

	SDL_Window *window;
	window = SDL_CreateWindow("Video Output", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, outW, outH, 0);

	SDL_Surface *surf = SDL_CreateRGBSurfaceFrom(outFrame->data[0], outW, outH, 24, outFrame->linesize[0], 0x0000FF, 0x00FF00, 0xFF0000, 0);

	//int framesToDraw = 0;
	/*
	uint64_t timerAccumulator = 0;
	uint64_t startTime = SDL_GetPerformanceCounter();
	uint64_t timerFreq = SDL_GetPerformanceFrequency();

	uint64_t frameInterval = (1.0 / framerate) * timerFreq;
	uint64_t safetyThreshold = timerFreq / 1000 * 2;
	double timebase = av_q2d(formatContext->streams[videoStream]->time_base);
	*/

	uint64_t timerFreq = SDL_GetPerformanceFrequency();
	double timebase = av_q2d(decoderData->formatContext->streams[decoderData->targetStream]->time_base);

	// smx setup

	startSMX();

	uint8_t *smxBuffer = calloc(1350, 1);

	// signal ready and wait for playback to start
	SDL_SemPost(decoderData->readySemaphore);
	SDL_SemWait(decoderData->playbackSemaphore);

	uint64_t startTime = SDL_GetPerformanceCounter();
	uint64_t currentTime = startTime;

	SDL_Event e;

	while(decoderData->streaming || frameQueue_populated(decoderData->queue)) {
		while(SDL_PollEvent(&e) != 0) {
			switch(e.type) {
			case SDL_QUIT: 
				exit(0);
				return 0;	// TODO: cancel and await threads
			}
		}

		if (frameQueue_populated(decoderData->queue)) {

			AVPacket *packet = frameQueue_dequeue(decoderData->queue);

			avcodec_send_packet(decoderData->codecContext, packet);

			int result = avcodec_receive_frame(decoderData->codecContext, inFrame);

			if (result == AVERROR(EAGAIN)) {
				continue;
			}

			av_packet_unref(packet);

			sws_scale(
				swsContext,
				inFrame->data,
				inFrame->linesize,
				0,
				decoderData->codecContext->height,
				outFrame->data,
				outFrame->linesize
			);

			//uint64_t nextTime = startTime + (((double)(inFrame->pts - decoderData->formatContext->start_time) * timebase * timerFreq)); 
			uint64_t nextTime = currentTime + (((double)inFrame->duration * timebase * timerFreq));

			if (inFrame->duration <= 0) {
				//printf("BAD DURATION! %lld\n", inFrame->duration);
				nextTime = startTime + (((double)(inFrame->pts - decoderData->formatContext->start_time) * timebase * timerFreq)); 
			}

			int offsetX = (outW - dimensionx) / 2;
			int offsetY = (outH - dimensiony) / 2;

			for (int side = 0; side < 2; side++) {
				for (int panel = 0; panel < 9; panel++) {
					for (int light = 0; light < 25; light++) {
						int posX, posY;

						if (bothpads) {
							posX = offsetX + (smxPanelPosX[(side * 9) + panel] * 23) + (smxLedPosX[light]);
							posY = offsetY + (smxPanelPosY[(side * 9) + panel] * 23) + (smxLedPosY[light]);
						} else {
							posX = offsetX + (smxPanelPosX[panel] * 23) + (smxLedPosX[light]);
							posY = offsetY + (smxPanelPosY[panel] * 23) + (smxLedPosY[light]);
						}

						if (posX >= 0 && posX < outW && posY >= 0 && posY < outH) { 
							for (int color = 0; color < 3; color++) {
								int idx = (side * 675) + (panel * 75) + (light * 3) + color; 
								int bufIdx = (outFrame->linesize[0] * posY) + (posX * 3) + color;
							
								//smxBuffer[idx] = outFrame->data[0][bufIdx];

								smxBuffer[idx] = (float)(outFrame->data[0][bufIdx] * outFrame->data[0][bufIdx])/255.0;

								//outFrame->data[0][bufIdx] = smxBuffer[idx];
							}
						}
					}
				}
			}

			//uint64_t currentTime = SDL_GetPerformanceCounter();
			//printf("NextTime = %llu, %llu\n", nextTime, nextTime - currentTime);

			safeWait(nextTime);
			currentTime = nextTime;

			setSMXLights(smxBuffer, 1350);

			SDL_Rect rect = {0, 0, outW, outH};

			SDL_BlitSurface(surf, &rect, SDL_GetWindowSurface(window), &rect);

			SDL_UpdateWindowSurface(window);
		}
	}

	printf("DONE PLAYBACK\n");

	stopSMX();

	SDL_FreeSurface(surf);
	SDL_DestroyWindow(window);

	exit(0);
}

void audioThread(void *data) {
	struct decoderThreadData *decoderData = (struct decoderThreadData *)data;

	SDL_AudioSpec desiredSpec;
	SDL_AudioSpec spec;

	desiredSpec.freq = decoderData->codecContext->sample_rate;
	desiredSpec.format = AUDIO_F32;
	desiredSpec.channels = decoderData->codecContext->ch_layout.nb_channels;
	desiredSpec.silence = 0;
	desiredSpec.samples = 1024;
	desiredSpec.callback = NULL;
	desiredSpec.userdata = NULL;

	SDL_AudioDeviceID audioDeviceID;

	audioDeviceID = SDL_OpenAudioDevice(NULL, 0, &desiredSpec, &spec, 0);

	struct SwrContext* swrContext = NULL;
	swr_alloc_set_opts2(
		&swrContext, 
		&(AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO, 
		AV_SAMPLE_FMT_FLT, 
		spec.freq, 
		&decoderData->codecContext->ch_layout, 
		decoderData->codecContext->sample_fmt, 
		decoderData->codecContext->sample_rate, 
		0, 
		NULL
	);

	swr_init(swrContext);

	int bufferSize = av_samples_get_buffer_size(
		NULL, 
		decoderData->codecContext->ch_layout.nb_channels, 
		spec.samples, 
		AV_SAMPLE_FMT_FLT, 
		1
	);

	uint8_t *buffer = NULL;

	buffer = malloc(bufferSize);

	AVFrame *outFrame = av_frame_alloc();

	av_samples_fill_arrays(
		outFrame->data,
		outFrame->linesize,
		buffer,
		decoderData->codecContext->ch_layout.nb_channels,
		spec.samples,
		AV_SAMPLE_FMT_FLT,
		1
	);

	AVFrame *inFrame = av_frame_alloc();

	uint64_t timerFreq = SDL_GetPerformanceFrequency();
	double timebase = av_q2d(decoderData->formatContext->streams[decoderData->targetStream]->time_base);

	// signal ready and wait for playback to start
	SDL_SemPost(decoderData->readySemaphore);
	SDL_SemWait(decoderData->playbackSemaphore);

	SDL_PauseAudioDevice(audioDeviceID, 0);	// let 'er rip

	uint64_t startTime = SDL_GetPerformanceCounter();

	while(decoderData->streaming || frameQueue_populated(decoderData->queue)) {
		if (frameQueue_populated(decoderData->queue)) {
			AVPacket *packet = frameQueue_dequeue(decoderData->queue);

			avcodec_send_packet(decoderData->codecContext, packet);

			uint64_t nextTime = startTime + (((double)((packet->pts - packet->duration) - decoderData->formatContext->start_time) * timebase * timerFreq));

			int result = avcodec_receive_frame(decoderData->codecContext, inFrame);

			if (result == AVERROR(EAGAIN)) {
				continue;
			}

			av_packet_unref(packet);

			swr_convert(swrContext, outFrame->data, inFrame->nb_samples, inFrame->data, inFrame->nb_samples);

			uint32_t sz = av_samples_get_buffer_size(NULL, inFrame->ch_layout.nb_channels, inFrame->nb_samples, AV_SAMPLE_FMT_FLT, 1);
			SDL_QueueAudio(audioDeviceID, outFrame->data[0], sz);
		}
	}

	while (SDL_GetQueuedAudioSize(audioDeviceID)) {
		SDL_Delay(1);
	}

	SDL_CloseAudioDevice(audioDeviceID);
}

AVCodecContext *getCodecContext(AVCodecParameters *param) {
	AVCodec *codec = NULL;
	codec = avcodec_find_decoder(param->codec_id);

	AVCodecContext *result = NULL;
	result = avcodec_alloc_context3(codec);
	avcodec_parameters_to_context(result, param);

	avcodec_open2(result, codec, NULL);

	return result;
}

// TODO: don't use semaphores, use cond

int main(int argc, char** argv) {
	SDL_Init(SDL_INIT_EVERYTHING);

	for (int i = 1; i < argc - 1; i++) {
		if (strcmp(argv[i], "-w") == 0) {
			bothpads = 1;
		}

		if (strcmp(argv[i], "-p") == 0) {
			padoutput = 1;
		}
	}

	// open file

	AVOutputFormat* fmt;

	AVDictionary *d = NULL;
	av_dict_set(&d, "protocol_whitelist", "file,crypto,data,http,https,tcp,tls", 0);

	AVFormatContext *formatContext = NULL;
	avformat_open_input(&formatContext, argv[argc - 1], NULL, &d);

	avformat_find_stream_info(formatContext, NULL);

	av_dump_format(formatContext, 0, argv[argc - 1], 0);

	int videoStream = -1;
	int audioStream = -1;
	for (int i = 0; i < formatContext->nb_streams; i++) {
		if (videoStream == -1 && formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			videoStream = i;
		} else if (audioStream == -1 && formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			audioStream = i;
		}

		if (audioStream != -1 && videoStream != -1)
			break;
	}

	// setup video

	AVCodecContext *videoCodecContext = getCodecContext(formatContext->streams[videoStream]->codecpar);

	//struct videoDecoderData *videoDecoderData = initVideoData(formatContext, codecContext, videoStream);

	struct frameQueue *videoQueue = frameQueue_alloc();

	struct decoderThreadData *videoThreadData = malloc(sizeof(struct decoderThreadData));

	//videoThreadData->data = videoDecoderData;
	videoThreadData->formatContext = formatContext;
	videoThreadData->codecContext = videoCodecContext;
	videoThreadData->queue = videoQueue;
	videoThreadData->streaming = 1;
	videoThreadData->targetStream = videoStream;
	videoThreadData->readySemaphore = SDL_CreateSemaphore(1);
	videoThreadData->playbackSemaphore = SDL_CreateSemaphore(1);

	//SDL_Thread *videoDecoderThread = SDL_CreateThread(videoThread, "video", videoThreadData);

	// audio setup

	AVCodecContext *audioCodecContext = getCodecContext(formatContext->streams[audioStream]->codecpar);

	struct frameQueue *audioQueue = frameQueue_alloc();

	struct decoderThreadData *audioThreadData = malloc(sizeof(struct decoderThreadData));

	audioThreadData->formatContext = formatContext;
	audioThreadData->codecContext = audioCodecContext;
	audioThreadData->queue = audioQueue;
	audioThreadData->streaming = 1;
	audioThreadData->targetStream = audioStream;
	audioThreadData->readySemaphore = SDL_CreateSemaphore(1);
	audioThreadData->playbackSemaphore = SDL_CreateSemaphore(1);

	//SDL_Thread *audioDecoderThread = SDL_CreateThread(audioThread, "audio", audioThreadData);

	// wait for threads to become ready

	
	SDL_Thread *videoDecoderThread = SDL_CreateThread(videoThread, "video", videoThreadData);
	SDL_Thread *audioDecoderThread = SDL_CreateThread(audioThread, "audio", audioThreadData);

	SDL_SemWait(videoThreadData->readySemaphore);
	SDL_SemWait(audioThreadData->readySemaphore);
	

	uint8_t hasStarted = 0;

	// start playback/precache

	AVPacket *packet = av_packet_alloc();

	SDL_Event e;
	
	while (av_read_frame(formatContext, packet) >= 0) {
		while(SDL_PollEvent(&e) != 0) {
			switch(e.type) {
			case SDL_QUIT: 
				return 0;	// TODO: cancel and await threads
			}
		}

		// TODO: keep ahead of frames via timestamp, not queued frames since audio immediately consumes the queue

		if (!hasStarted && (videoQueue->len >= PRECACHE_LEN && audioQueue >= PRECACHE_LEN)) {
			printf("STARTING VIDEO\n");
			hasStarted = 1;
			SDL_SemPost(videoThreadData->playbackSemaphore);
			SDL_SemPost(audioThreadData->playbackSemaphore);
		}

		/*while (videoQueue->len >= PRECACHE_LEN && audioQueue >= PRECACHE_LEN) {
			SDL_Delay(1);
		}*/

		if (packet->stream_index == videoStream) {
			//printf("video %d, %d frames queued\n", packet->pts, videoQueue->len);
			frameQueue_enqueue(videoQueue, packet);
		} else if (packet->stream_index == audioStream) {
			//printf("audio %d, %d frames queued\n", packet->pts, audioQueue->len);
			//printf("audio %d\n", packet->pts);
			frameQueue_enqueue(audioQueue, packet);
		}
		//printf("video %d audio %d\n", videoQueue->len, audioQueue->len);

		
		av_packet_unref(packet);
	}

	/*
	SDL_Thread *videoDecoderThread = SDL_CreateThread(videoThread, "video", videoThreadData);
	SDL_Thread *audioDecoderThread = SDL_CreateThread(audioThread, "audio", audioThreadData);

	// i'm not sure the semaphores are actually working...
	SDL_SemWait(videoThreadData->readySemaphore);
	SDL_SemWait(audioThreadData->readySemaphore);

	if (!hasStarted && (videoQueue->len >= PRECACHE_LEN && audioQueue >= PRECACHE_LEN)) {
		hasStarted = 1;
		SDL_SemPost(videoThreadData->playbackSemaphore);
		SDL_SemPost(audioThreadData->playbackSemaphore);
	}
	*/

	videoThreadData->streaming = 0;
	audioThreadData->streaming = 0;
	SDL_WaitThread(videoDecoderThread, NULL);
	SDL_WaitThread(audioDecoderThread, NULL);

	SDL_DestroySemaphore(videoThreadData->readySemaphore);
	SDL_DestroySemaphore(videoThreadData->playbackSemaphore);

	//SDL_FreeSurface(surf);

	SDL_Quit();

	return 0;
}