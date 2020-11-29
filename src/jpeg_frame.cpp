#include <easylogging++.h>
#include "jpeg_frame.h"
#include "video_streamer.h"

namespace video_streamer {
	class c_heap_image_buffer_releaser: public image_buffer_releaser {
	public:
		void release(image_buffer &buffer) override {
			free(buffer.data());
		}
		
	};
}

static video_streamer::c_heap_image_buffer_releaser _c_heap_image_buffer_releaser;

static auto getLibJPEGLogger() {
	static auto logger = el::Loggers::getLogger("libjpeg");
	return logger;
}

video_streamer::libjpeg_exception::libjpeg_exception(jpeg_common_struct *cinfo, bool logOutput, bool abort): m_messageBuffer() {
	cinfo->err->format_message(cinfo, m_messageBuffer);
	if (logOutput) {
		getLibJPEGLogger()->error(m_messageBuffer);
	}
	if (abort) {
		jpeg_abort(cinfo);
	}
}

static jpeg_error_mgr *init_jpeg_err(jpeg_error_mgr *err) {
	err = jpeg_std_error(err);
	err->error_exit = [](jpeg_common_struct *cinfo) {
		throw video_streamer::libjpeg_exception(cinfo);
	};
	err->emit_message = [](jpeg_common_struct *cinfo, int msgLevel) {
		char buffer[JMSG_LENGTH_MAX];
		cinfo->err->format_message(cinfo, buffer);
		if (msgLevel >= 0) {
			getLibJPEGLogger()->trace(buffer);
		} else {
			getLibJPEGLogger()->warn(buffer);
		}
	};
	return err;
}

video_streamer::jpeg_decompressor_impl::jpeg_decompressor_impl(): jpeg_decompress_struct() {
	err = init_jpeg_err(&m_err);
	jpeg_create_decompress(this);
}

video_streamer::jpeg_decompressor_impl::~jpeg_decompressor_impl() {
	jpeg_destroy_decompress(this);
}

video_streamer::jpeg_compressor_impl::jpeg_compressor_impl(): jpeg_compress_struct() {
	err = init_jpeg_err(&m_err);
	jpeg_create_compress(this);
}

video_streamer::jpeg_compressor_impl::~jpeg_compressor_impl() {
	jpeg_destroy_compress(this);
}

video_streamer::jpeg_frame::jpeg_frame(image_buffer buffer): m_buffer(std::move(buffer)) {
	libjpeg_instance<jpeg_decompressor_impl> decompressor;
	read_header(decompressor);
	m_width = decompressor.get()->image_width;
	m_height = decompressor.get()->image_height;
	jpeg_abort_decompress(decompressor.get());
}

video_streamer::image_buffer video_streamer::jpeg_frame::compress_frame(
		uncompressed_frame& frame, J_COLOR_SPACE color_space, int num_components, int quality
) {
	libjpeg_instance<jpeg_compressor_impl> compressor;
	uint8_t *buffer = nullptr;
	size_t bufferSize = 0;
	jpeg_mem_dest(compressor.get(), &buffer, &bufferSize);
	compressor.get()->image_width = frame.width();
	compressor.get()->image_height = frame.height();
	compressor.get()->in_color_space = color_space;
	compressor.get()->input_components = num_components;
	jpeg_set_defaults(compressor.get());
	jpeg_set_quality(compressor.get(), quality, true);
	jpeg_start_compress(compressor.get(), true);
	JSAMPROW ptr[] = {(JSAMPLE *) frame.buffer().data()};
	while (compressor.get()->next_scanline < compressor.get()->image_height) {
		jpeg_write_scanlines(compressor.get(), ptr, 1);
		ptr[0] += compressor.get()->image_width * 3;
	}
	jpeg_finish_compress(compressor.get());
	return image_buffer(buffer, bufferSize, &_c_heap_image_buffer_releaser);
}

video_streamer::jpeg_frame::jpeg_frame(
		uncompressed_frame& frame, J_COLOR_SPACE color_space, int num_components, int quality
): m_buffer(compress_frame(frame, color_space, num_components, quality)),
	m_width(frame.width()), m_height(frame.height())
{
}

void video_streamer::jpeg_frame::read_header(video_streamer::libjpeg_instance<jpeg_decompressor_impl>& decompressor) {
	jpeg_mem_src(decompressor.get(), m_buffer.data(), m_buffer.size());
	if (jpeg_read_header(decompressor.get(), true) != JPEG_HEADER_OK) {
		throw video_streamer::libjpeg_exception((jpeg_common_struct*) decompressor.get(), false, false);
	}
}

video_streamer::uncompressed_frame video_streamer::jpeg_frame::uncompress(
		J_COLOR_SPACE color_space, int num_components
) {
	libjpeg_instance<jpeg_decompressor_impl> decompressor;
	read_header(decompressor);
	decompressor.get()->out_color_space = color_space;
	decompressor.get()->out_color_components = num_components;
	uncompressed_frame image(width(), height(), 3);
	jpeg_start_decompress(decompressor.get());
	JSAMPROW ptr[] = { image.buffer().data() };
	for (auto i = 0; i < image.height(); i++) {
		jpeg_read_scanlines(decompressor.get(), ptr, 1);
		ptr[0] += width() * 3;
	}
	jpeg_finish_decompress(decompressor.get());
	return image;
}
