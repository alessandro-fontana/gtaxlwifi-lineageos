/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * gtaxl-enctest: encodes N synthetic frames with MediaCodec and writes an
 * MP4 (or WebM for VP8). A square moving over a gradient, so that motion
 * estimation has work to do and the decoded image is recognisable.
 *
 *   gtaxl-enctest <avc|hevc|vp8|mpeg4|h263> <width> <height> <frames> <bitrate> <file> [name]
 *
 * With ENC_SURFACE=1 the frames go as RGBA into the codec's input Surface
 * (the path screenrecord and the camera take), not into I420 buffers.
 */
#include <android/binder_process.h>
#include <android/native_window.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkMediaMuxer.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <string>

static double now()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* RGBA into the Surface: the same pattern, in colour. */
static void draw(ANativeWindow *win, int w, int h, int n)
{
	ANativeWindow_Buffer b;
	if (ANativeWindow_lock(win, &b, nullptr)) {
		fprintf(stderr, "ANativeWindow_lock failed\n");
		return;
	}
	int bx = (n * 8) % (w - 128), by = (n * 4) % (h - 128);
	for (int y = 0; y < h && y < b.height; y++) {
		uint32_t *row = (uint32_t *)b.bits + (size_t)y * b.stride;
		for (int x = 0; x < w && x < b.width; x++) {
			bool box = x >= bx && x < bx + 128 && y >= by && y < by + 128;
			uint8_t g = (uint8_t)((x + y + n) % 200);
			row[x] = box ? 0xffffffff
				     : 0xff000000 | (uint32_t)(255 - g) << 16 | (uint32_t)g << 8 |
					       (uint32_t)((x * 255) / w);
		}
	}
	ANativeWindow_unlockAndPost(win);
}

/*
 * I420 with the stride and plane height the codec declares in its input
 * format: Y, then U and V at half resolution. Returns the bytes used.
 */
static size_t fill(uint8_t *buf, size_t cap, int w, int h, int stride, int vstride, int n)
{
	size_t ysize = (size_t)stride * vstride, csize = ysize / 4;
	if (cap < ysize + 2 * csize)
		return 0;
	int bx = (n * 8) % (w - 128), by = (n * 4) % (h - 128);
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			bool box = x >= bx && x < bx + 128 && y >= by && y < by + 128;
			buf[y * stride + x] = box ? 235 : (uint8_t)(16 + (x + y + n) % 200);
		}
	uint8_t *u = buf + ysize, *v = u + csize;
	for (int y = 0; y < h / 2; y++)
		for (int x = 0; x < w / 2; x++) {
			u[y * (stride / 2) + x] = (uint8_t)(64 + (x * 128) / (w / 2));
			v[y * (stride / 2) + x] = (uint8_t)(64 + (y * 128) / (h / 2));
		}
	return ysize + 2 * csize;
}

int main(int argc, char **argv)
{
	if (argc < 7) {
		fprintf(stderr, "usage: %s <avc|hevc|vp8|mpeg4|h263> <w> <h> <frames> <bitrate> <file> [name]\n",
			argv[0]);
		return 2;
	}
	/* The codec service calls back over binder: without threads it never does. */
	ABinderProcess_setThreadPoolMaxThreadCount(2);
	ABinderProcess_startThreadPool();

	std::string codec = argv[1];
	int w = atoi(argv[2]), h = atoi(argv[3]), frames = atoi(argv[4]);
	int bitrate = atoi(argv[5]);
	const char *path = argv[6];
	const char *mime = codec == "hevc"    ? "video/hevc"
			   : codec == "vp8"   ? "video/x-vnd.on2.vp8"
			   : codec == "mpeg4" ? "video/mp4v-es"
			   : codec == "h263"  ? "video/3gpp"
					      : "video/avc";

	AMediaCodec *enc = argc > 7 ? AMediaCodec_createCodecByName(argv[7])
				    : AMediaCodec_createEncoderByType(mime);
	if (!enc) {
		fprintf(stderr, "no encoder for %s\n", mime);
		return 1;
	}
	char *name = nullptr;
	AMediaCodec_getName(enc, &name);
	printf("encoder %s\n", name ? name : "?");

	AMediaFormat *fmt = AMediaFormat_new();
	AMediaFormat_setString(fmt, AMEDIAFORMAT_KEY_MIME, mime);
	AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_WIDTH, w);
	AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_HEIGHT, h);
	AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_BIT_RATE, bitrate);
	AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_FRAME_RATE, 30);
	AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, 1);
	AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_COLOR_FORMAT, 19 /* YUV420Planar */);
	const char *surf = getenv("ENC_SURFACE");
	bool surface = surf && surf[0] == '1';
	if (surface)
		AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_COLOR_FORMAT, 0x7F000789 /* Surface */);
	media_status_t st = AMediaCodec_configure(enc, fmt, nullptr, nullptr,
						  AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
	if (st != AMEDIA_OK) {
		fprintf(stderr, "configure: %d\n", st);
		return 1;
	}
	ANativeWindow *win = nullptr;
	if (surface && (st = AMediaCodec_createInputSurface(enc, &win)) != AMEDIA_OK) {
		fprintf(stderr, "createInputSurface: %d\n", st);
		return 1;
	}
	if (win)
		ANativeWindow_setBuffersGeometry(win, w, h, AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM);
	if ((st = AMediaCodec_start(enc)) != AMEDIA_OK) {
		fprintf(stderr, "start: %d\n", st);
		return 1;
	}

	int stride = w, vstride = h;
	AMediaFormat *inf = AMediaCodec_getInputFormat(enc);
	if (inf) {
		AMediaFormat_getInt32(inf, AMEDIAFORMAT_KEY_STRIDE, &stride);
		AMediaFormat_getInt32(inf, AMEDIAFORMAT_KEY_SLICE_HEIGHT, &vstride);
		printf("input format: %s\n", AMediaFormat_toString(inf));
		AMediaFormat_delete(inf);
	}
	if (stride < w)
		stride = w;
	if (vstride < h)
		vstride = h;

	int fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0644);
	AMediaMuxer *mux = AMediaMuxer_new(fd, codec == "vp8"	 ? AMEDIAMUXER_OUTPUT_FORMAT_WEBM
					       : codec == "h263" ? AMEDIAMUXER_OUTPUT_FORMAT_THREE_GPP
								 : AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4);
	ssize_t track = -1;
	int in = 0, out = 0, keys = 0;
	size_t bytes = 0;
	bool eos = false;
	double t0 = now(), tFirst = 0;

	while (!eos) {
		if (surface && in <= frames) {
			if (in == frames) {
				AMediaCodec_signalEndOfInputStream(enc);
			} else {
				draw(win, w, h, in);
				/* at most 30 fps, as a camera would */
				usleep(33000);
			}
			in++;
		} else if (in <= frames) {
			ssize_t i = AMediaCodec_dequeueInputBuffer(enc, 10000);
			if (i >= 0) {
				size_t cap;
				uint8_t *buf = AMediaCodec_getInputBuffer(enc, i, &cap);
				uint64_t pts = (uint64_t)in * 1000000 / 30;
				media_status_t qs;
				if (in == frames) {
					qs = AMediaCodec_queueInputBuffer(enc, i, 0, 0, pts,
						AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
				} else {
					size_t used = fill(buf, cap, w, h, stride, vstride, in);
					if (!used)
						fprintf(stderr, "input buffer of %zu bytes, too small\n",
							cap);
					qs = AMediaCodec_queueInputBuffer(enc, i, 0, used, pts, 0);
				}
				if (qs != AMEDIA_OK)
					fprintf(stderr, "queueInputBuffer %d: %d\n", in, qs);
				in++;
			}
		}
		AMediaCodecBufferInfo info;
		ssize_t o = AMediaCodec_dequeueOutputBuffer(enc, &info, 10000);
		if (o == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
			AMediaFormat *of = AMediaCodec_getOutputFormat(enc);
			printf("output format: %s\n", AMediaFormat_toString(of));
			track = AMediaMuxer_addTrack(mux, of);
			AMediaMuxer_start(mux);
			AMediaFormat_delete(of);
		} else if (o >= 0) {
			size_t cap;
			uint8_t *buf = AMediaCodec_getOutputBuffer(enc, o, &cap);
			if (info.size > 0 && !(info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) &&
			    track >= 0) {
				if (!out)
					tFirst = now() - t0;
				AMediaMuxer_writeSampleData(mux, track, buf, &info);
				out++;
				bytes += info.size;
				if (info.flags & 1 /* KEY_FRAME */)
					keys++;
			}
			if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM)
				eos = true;
			AMediaCodec_releaseOutputBuffer(enc, o, false);
		}
		if (now() - t0 > 60) {
			fprintf(stderr, "timeout: %d in, %d out\n", in, out);
			break;
		}
	}
	double t = now() - t0;

	if (win)
		ANativeWindow_release(win);
	AMediaCodec_stop(enc);
	AMediaCodec_delete(enc);
	if (track >= 0)
		AMediaMuxer_stop(mux);
	AMediaMuxer_delete(mux);
	close(fd);

	printf("%d frames, %d keyframes, %zu bytes (%.0f kbit/s at 30 fps), %.2f s, %.1f fps, "
	       "first after %.3f s\n",
	       out, keys, bytes, out ? bytes * 8.0 * 30 / out / 1000 : 0, t, out / t, tFirst);
	return out == frames ? 0 : 1;
}
