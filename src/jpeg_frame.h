#pragma once

#include <cstdio>
#include <jpeglib.h>
#include <memory>
#include <vector>
#include <mutex>
#include <stdexcept>

namespace video_streamer {
	class jpeg_frame;
}

#include "video_streamer.h"

namespace video_streamer {
	
	class libjpeg_exception: public std::exception {
		char m_messageBuffer[JMSG_LENGTH_MAX];
		
	public:
		explicit libjpeg_exception(jpeg_common_struct *cinfo, bool logOutput = true, bool abort = true);
		const char *what() const noexcept override {
			return m_messageBuffer;
		}
	};
	
	struct jpeg_decompressor_impl: public jpeg_decompress_struct {
		jpeg_error_mgr m_err = {};
		
	public:
		jpeg_decompressor_impl();
		~jpeg_decompressor_impl();
		
	};
	
	struct jpeg_compressor_impl: public jpeg_compress_struct {
		jpeg_error_mgr m_err = {};
	
	public:
		jpeg_compressor_impl();
		~jpeg_compressor_impl();
	};
	
	template<typename T> class libjpeg_instance final {
		static std::mutex impl_queue_mutex;
		static std::vector<std::unique_ptr<T>> impl_queue;
		std::unique_ptr<T> m_impl;
		
	public:
		libjpeg_instance(): m_impl() {
		}
		libjpeg_instance(libjpeg_instance<T> &&impl) noexcept: m_impl(std::move(impl.m_impl)) {
		}
		~libjpeg_instance() {
			if (!m_impl) return;
			std::unique_lock<std::mutex> lock(impl_queue_mutex);
			impl_queue.emplace_back(std::move(m_impl));
		}
		T *get() {
			if (!m_impl) {
				std::unique_lock<std::mutex> lock(impl_queue_mutex);
				if (impl_queue.empty()) {
					lock.unlock();
					m_impl = std::make_unique<T>();
				} else {
					m_impl = std::move(impl_queue.back());
					impl_queue.pop_back();
					lock.unlock();
				}
			}
			return m_impl.get();
		}
		
	};
	
	template<typename T> std::mutex video_streamer::libjpeg_instance<T>::impl_queue_mutex;
	template<typename T> std::vector<std::unique_ptr<T>> video_streamer::libjpeg_instance<T>::impl_queue;
	
	class jpeg_frame: public frame {
		image_buffer m_buffer;
		int m_width;
		int m_height;
		
		static image_buffer compress_frame(
				uncompressed_frame& frame, J_COLOR_SPACE color_space, int num_components, int quality
		);
		void read_header(libjpeg_instance<jpeg_decompressor_impl>& decompressor);
		
	public:
		explicit jpeg_frame(image_buffer buffer);
		explicit jpeg_frame(
				uncompressed_frame& frame,
				J_COLOR_SPACE color_space,
				int num_components,
				int quality = 80
		);
		jpeg_frame(jpeg_frame &&frame) = default;
		int width() const override {
			return m_width;
		}
		int height() const override {
			return m_height;
		}
		image_buffer& buffer() override {
			return m_buffer;
		}
		const image_buffer& buffer() const override {
			return m_buffer;
		}
		uncompressed_frame uncompress(J_COLOR_SPACE color_space, int num_components);
		
	};

}
