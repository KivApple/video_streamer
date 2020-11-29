#pragma once

#include <cstdint>
#include <cstdio>
#include <vector>
#include <thread>
#include <string>
#include "unique_fd.h"

namespace video_streamer {
	
	class image_buffer;
	
	class image_buffer_releaser {
	public:
		virtual void release(image_buffer &buffer) = 0;
	};
	
	class image_buffer final {
		uint8_t *m_data;
		size_t m_size;
		image_buffer_releaser *m_releaser;
		
	public:
		explicit image_buffer(size_t size);
		
		image_buffer(
				uint8_t *data, size_t size,
				image_buffer_releaser *releaser = nullptr
		): m_data(data), m_size(size), m_releaser(releaser) {
		}
		
		image_buffer(
				image_buffer &&buffer
		) noexcept: m_data(buffer.m_data), m_size(buffer.m_size), m_releaser(buffer.m_releaser) {
			buffer.m_data = nullptr;
			buffer.m_size = 0;
			buffer.m_releaser = nullptr;
		}
		
		~image_buffer() {
			if (m_releaser) {
				m_releaser->release(*this);
			}
		}
		
		uint8_t *data() const {
			return m_data;
		}
		
		size_t size() const {
			return m_size;
		}
		
	};
	
	class frame {
	public:
		virtual ~frame() = default;
		virtual int width() const = 0;
		virtual int height() const = 0;
		virtual image_buffer& buffer() = 0;
		virtual const image_buffer& buffer() const = 0;
		
	};
	
	class uncompressed_frame: public frame {
		image_buffer m_buffer;
		int m_width;
		int m_height;
		int m_bytes_per_pixel;
	
	public:
		uncompressed_frame(
				image_buffer buffer, int width, int height, int bytes_per_pixel
		): m_buffer(std::move(buffer)), m_width(width), m_height(height), m_bytes_per_pixel(bytes_per_pixel) {
		}
		
		uncompressed_frame(
				int width, int height, int bytes_per_pixel
		): uncompressed_frame(
				image_buffer(width * height * bytes_per_pixel),
				width, height, bytes_per_pixel
		) {
		}
		
		image_buffer& buffer() override {
			return m_buffer;
		}
		
		const image_buffer& buffer() const override {
			return m_buffer;
		}
		
		int width() const override {
			return m_width;
		}
		
		int height() const override {
			return m_height;
		}
		
		int bytes_per_pixel() const {
			return m_bytes_per_pixel;
		}
		
		uint32_t pixel(int x, int y) const {
			unsigned long offset = (y * m_width + x) * m_bytes_per_pixel;
			uint32_t result = 0;
			for (auto i = 0; i < m_bytes_per_pixel; i++) {
				result = result | ((uint32_t) m_buffer.data()[offset + i] << (8 * i));
			}
			return result;
		}
		
		void set_pixel(int x, int y, uint32_t color) {
			unsigned long offset = (y * m_width + x) * m_bytes_per_pixel;
			for (auto i = 0; i < m_bytes_per_pixel; i++) {
				m_buffer.data()[offset + i] = (color >> (i * 8)) & 0xFF;
			}
		}
		
	};
	
	class stream_server_exception: public std::exception {
		std::string m_message;
	
	public:
		explicit stream_server_exception(std::string message): m_message(std::move(message)) {
		}
		const char *what() const noexcept override {
			return m_message.c_str();
		}
		
	};
	
	class stream_server: std::thread {
		std::vector<posix::unique_fd> m_server_sockets;
		std::mutex m_server_sockets_mutex;
		std::vector<posix::unique_fd> m_client_sockets;
		std::mutex m_client_sockets_mutex;
		bool m_quit = false;
		int m_send_buffer_size;
		
		void run();
		void accept_client(posix::unique_fd &server_socket);
		
	public:
		explicit stream_server(std::vector<std::string> server_addresses, int sendBufferSize = -1);
		~stream_server();
		void send(const void *data, size_t data_size);
		void send(const image_buffer &buffer);
		void send(const frame &frame);
	
	};
	
}

#include "jpeg_frame.h"

namespace video_streamer {
	int main(
			int argc, char *argv[],
			std::function<uncompressed_frame(uncompressed_frame)> frame_processor =
					std::function<uncompressed_frame(uncompressed_frame)>()
	);
}
