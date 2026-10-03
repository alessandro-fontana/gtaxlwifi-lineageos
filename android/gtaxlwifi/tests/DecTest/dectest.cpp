/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * gtaxl-dectest: decodes the first video track of a file with MediaCodec and
 * writes every frame as I420, cropped, so that the host can compare it with
 * ffmpeg's decoding of the same file. Into buffers, or with DEC_SURFACE=1 into
 * the Surface of an AImageReader, the path of video players.
 *
 *   gtaxl-dectest <input> <output.yuv | -> [codec name]
 */
#include <android/binder_process.h>
#include <media/NdkImageReader.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <deque>
#include <vector>

static double now()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

struct Layout {
	int width = 0, height = 0, stride = 0, sliceHeight = 0, color = 0;
	int left = 0, top = 0, right = -1, bottom = -1;
};

static void readLayout(AMediaFormat *f, Layout *l)
{
	AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_WIDTH, &l->width);
	AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_HEIGHT, &l->height);
	AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_STRIDE, &l->stride);
	AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_SLICE_HEIGHT, &l->sliceHeight);
	AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_COLOR_FORMAT, &l->color);
	if (!AMediaFormat_getRect(f, AMEDIAFORMAT_KEY_DISPLAY_CROP, &l->left, &l->top, &l->right,
				  &l->bottom)) {
		l->left = l->top = 0;
		l->right = l->width - 1;
		l->bottom = l->height - 1;
	}
	if (l->stride < l->width)
		l->stride = l->width;
	if (l->sliceHeight < l->height)
		l->sliceHeight = l->height;
}

/* One frame, cropped, as I420. Planar (19) and semi-planar (21) inputs. */
static void writeI420(int fd, const uint8_t *buf, size_t size, const Layout &l)
{
	int w = l.right - l.left + 1, h = l.bottom - l.top + 1;
	size_t ysize = (size_t)l.stride * l.sliceHeight;
	if (size < ysize + ysize / 2) {
		fprintf(stderr, "output buffer of %zu bytes, %zu expected\n", size, ysize * 3 / 2);
		return;
	}
	std::vector<uint8_t> out((size_t)w * h * 3 / 2);
	uint8_t *y = out.data(), *u = y + w * h, *v = u + (w / 2) * (h / 2);
	for (int r = 0; r < h; r++)
		memcpy(y + r * w, buf + (size_t)(r + l.top) * l.stride + l.left, w);
	const uint8_t *c = buf + ysize;
	for (int r = 0; r < h / 2; r++) {
		int sr = r + l.top / 2;
		for (int x = 0; x < w / 2; x++) {
			int sx = x + l.left / 2;
			if (l.color == 21) {
				u[r * (w / 2) + x] = c[(size_t)sr * l.stride + 2 * sx];
				v[r * (w / 2) + x] = c[(size_t)sr * l.stride + 2 * sx + 1];
			} else {
				u[r * (w / 2) + x] = c[(size_t)sr * (l.stride / 2) + sx];
				v[r * (w / 2) + x] = c[ysize / 4 + (size_t)sr * (l.stride / 2) + sx];
			}
		}
	}
	if (write(fd, out.data(), out.size()) != (ssize_t)out.size())
		fprintf(stderr, "short write\n");
}

/* One AImage, cropped, as I420: any plane layout. */
static void writeImage(int fd, AImage *img)
{
	AImageCropRect r;
	AImage_getCropRect(img, &r);
	int w = r.right - r.left, h = r.bottom - r.top;
	std::vector<uint8_t> out((size_t)w * h * 3 / 2);
	uint8_t *dst = out.data();
	for (int p = 0; p < 3; p++) {
		uint8_t *data;
		int len, rowStride, pixStride = 1;
		AImage_getPlaneData(img, p, &data, &len);
		AImage_getPlaneRowStride(img, p, &rowStride);
		if (p)
			AImage_getPlanePixelStride(img, p, &pixStride);
		int pw = p ? w / 2 : w, ph = p ? h / 2 : h;
		int x0 = p ? r.left / 2 : r.left, y0 = p ? r.top / 2 : r.top;
		for (int y = 0; y < ph; y++)
			for (int x = 0; x < pw; x++)
				*dst++ = data[(size_t)(y + y0) * rowStride + (size_t)(x + x0) * pixStride];
	}
	if (write(fd, out.data(), out.size()) != (ssize_t)out.size())
		fprintf(stderr, "short write\n");
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: %s <input> <output.yuv | -> [codec name]\n", argv[0]);
		return 2;
	}
	/* The codec service calls back over binder: without threads it never does. */
	ABinderProcess_setThreadPoolMaxThreadCount(2);
	ABinderProcess_startThreadPool();

	int in = open(argv[1], O_RDONLY);
	struct stat st;
	if (in < 0 || fstat(in, &st)) {
		fprintf(stderr, "cannot open %s\n", argv[1]);
		return 1;
	}
	AMediaExtractor *ex = AMediaExtractor_new();
	if (AMediaExtractor_setDataSourceFd(ex, in, 0, st.st_size) != AMEDIA_OK) {
		fprintf(stderr, "extractor: unsupported file\n");
		return 1;
	}
	AMediaFormat *fmt = nullptr;
	const char *mime = nullptr;
	for (size_t t = 0; t < AMediaExtractor_getTrackCount(ex); t++) {
		AMediaFormat *f = AMediaExtractor_getTrackFormat(ex, t);
		const char *m;
		if (AMediaFormat_getString(f, AMEDIAFORMAT_KEY_MIME, &m) && !strncmp(m, "video/", 6)) {
			AMediaExtractor_selectTrack(ex, t);
			fmt = f;
			mime = m;
			break;
		}
		AMediaFormat_delete(f);
	}
	if (!fmt) {
		fprintf(stderr, "no video track\n");
		return 1;
	}
	printf("track: %s\n", AMediaFormat_toString(fmt));

	AMediaCodec *dec = argc > 3 ? AMediaCodec_createCodecByName(argv[3])
				    : AMediaCodec_createDecoderByType(mime);
	if (!dec) {
		fprintf(stderr, "no decoder for %s\n", mime);
		return 1;
	}
	char *name = nullptr;
	AMediaCodec_getName(dec, &name);
	printf("decoder %s\n", name ? name : "?");
	/* DEC_HOLD=n keeps the last n pictures before giving them back to the decoder. */
	const char *holdEnv = getenv("DEC_HOLD");
	size_t hold = holdEnv ? (size_t)atoi(holdEnv) : 0;
	std::deque<AImage *> held;
	const char *surf = getenv("DEC_SURFACE");
	AImageReader *reader = nullptr;
	ANativeWindow *win = nullptr;
	if (surf && surf[0] == '1') {
		int w = 0, h = 0;
		AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_WIDTH, &w);
		AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_HEIGHT, &h);
		if (AImageReader_newWithUsage(w, h, AIMAGE_FORMAT_YUV_420_888,
					      AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, 8 + hold,
					      &reader) != AMEDIA_OK ||
		    AImageReader_getWindow(reader, &win) != AMEDIA_OK) {
			fprintf(stderr, "AImageReader failed\n");
			return 1;
		}
	}
	/* Ask for planar YUV; the codec may still answer with another layout. */
	AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_COLOR_FORMAT, 19);
	media_status_t s = AMediaCodec_configure(dec, fmt, win, nullptr, 0);
	if (s != AMEDIA_OK || (s = AMediaCodec_start(dec)) != AMEDIA_OK) {
		fprintf(stderr, "configure/start: %d\n", s);
		return 1;
	}

	int out = -1;
	if (strcmp(argv[2], "-"))
		out = open(argv[2], O_CREAT | O_TRUNC | O_WRONLY, 0644);
	Layout layout;
	AMediaFormat *of = AMediaCodec_getOutputFormat(dec);
	readLayout(of, &layout);
	AMediaFormat_delete(of);

	int queued = 0, frames = 0;
	bool inEos = false, outEos = false;
	double t0 = now(), tFirst = 0, tLast = t0;
	while (!outEos) {
		if (!inEos) {
			ssize_t i = AMediaCodec_dequeueInputBuffer(dec, 10000);
			if (i >= 0) {
				size_t cap;
				uint8_t *buf = AMediaCodec_getInputBuffer(dec, i, &cap);
				ssize_t n = AMediaExtractor_readSampleData(ex, buf, cap);
				if (n < 0) {
					AMediaCodec_queueInputBuffer(dec, i, 0, 0, 0,
						AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
					inEos = true;
				} else {
					AMediaCodec_queueInputBuffer(dec, i, 0, n,
						AMediaExtractor_getSampleTime(ex), 0);
					AMediaExtractor_advance(ex);
					queued++;
				}
			}
		}
		AMediaCodecBufferInfo info;
		ssize_t o = AMediaCodec_dequeueOutputBuffer(dec, &info, 10000);
		if (o == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
			of = AMediaCodec_getOutputFormat(dec);
			printf("output format: %s\n", AMediaFormat_toString(of));
			readLayout(of, &layout);
			AMediaFormat_delete(of);
		} else if (o >= 0) {
			if (info.size > 0) {
				if (!frames)
					tFirst = now() - t0;
				if (reader) {
					AMediaCodec_releaseOutputBuffer(dec, o, true);
					o = -1;
					/* the frame reaches the reader asynchronously */
					AImage *img = nullptr;
					for (int k = 0; k < 200 && !img; k++)
						if (AImageReader_acquireNextImage(reader, &img) != AMEDIA_OK) {
							img = nullptr;
							usleep(1000);
						}
					if (!img) {
						fprintf(stderr, "frame %d never reached the reader\n", frames);
					} else {
						if (out >= 0)
							writeImage(out, img);
						held.push_back(img);
						while (held.size() > hold) {
							AImage_delete(held.front());
							held.pop_front();
						}
					}
				} else {
					size_t cap;
					uint8_t *buf = AMediaCodec_getOutputBuffer(dec, o, &cap);
					if (out >= 0 && buf)
						writeI420(out, buf + info.offset, info.size, layout);
				}
				frames++;
				tLast = now();
			}
			if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM)
				outEos = true;
			if (o >= 0)
				AMediaCodec_releaseOutputBuffer(dec, o, false);
		}
		if (now() - tLast > 10) {
			fprintf(stderr, "stalled: %d queued, %d decoded\n", queued, frames);
			break;
		}
	}
	double t = now() - t0;

	for (AImage *img : held)
		AImage_delete(img);
	AMediaCodec_stop(dec);
	AMediaCodec_delete(dec);
	if (reader)
		AImageReader_delete(reader);
	AMediaExtractor_delete(ex);
	AMediaFormat_delete(fmt);
	if (out >= 0)
		close(out);
	close(in);
	printf("%d frames of %d samples, %dx%d (crop %d,%d-%d,%d, stride %d, color %d), "
	       "%.2f s, %.1f fps, first after %.3f s\n",
	       frames, queued, layout.width, layout.height, layout.left, layout.top, layout.right,
	       layout.bottom, layout.stride, layout.color, t, frames / t, tFirst);
	return frames == queued && outEos ? 0 : 1;
}
