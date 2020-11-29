#pragma once

#include <linux/videodev2.h>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <stdexcept>
#include "unique_fd.h"
#include "video_streamer.h"
#include "jpeg_frame.h"

namespace video_streamer {
	
	namespace v4l2 {
		
		class capture_device;
		
		enum class capture_method {
			READ,
			MMAP
		};
		enum class format {
			UNKNOWN,
			YVYU,
			YUYV,
			VYUY,
			UYVY,
			MJPEG,
			H264
		};
		
		class capture_buffer: public image_buffer_releaser {
		public:
			capture_device *device;
			unsigned int index;
			void *base;
			size_t length;
			std::mutex mutex;
			
			void release(image_buffer &buffer) override;
		
		};
		
		class capture_device {
			std::string m_path;
			posix::unique_fd m_fd;
			capture_method m_method;
			v4l2_format m_format;
			unsigned int m_buffer_count;
			std::unique_ptr<capture_buffer[]> m_buffers;
			std::mutex m_read_mutex;
			unsigned int m_last_used_buffer;
			
			static unsigned int compute_buffer_count();
			void query_format();
			video_streamer::image_buffer wrap_buffer(capture_buffer &buffer, size_t length) const;
			void init_read();
			video_streamer::image_buffer read_read();
			void finish_read();
			void start_mmap();
			video_streamer::image_buffer read_mmap();
			void stop_mmap();
			
		public:
			explicit capture_device(std::string path, bool forceRead = false);
			~capture_device();
			int ioctl(unsigned long request, void *param);
			format pixel_format() const;
			int frame_width() const;
			int frame_height() const;
			void set_format(int width, int height, format pixel_format);
			video_streamer::image_buffer read_buffer();
			jpeg_frame read_jpeg();
			
		};
		
		class exception: public std::exception {
			std::string m_message;
			
		public:
			explicit exception(std::string message, int errNo = 0);
			const char *what() const noexcept override {
				return m_message.c_str();
			}
			
		};
	}
	
}

inline std::ostream &operator<<(std::ostream &ostream, video_streamer::v4l2::capture_method method) {
	static const char *methods[] = { "READ", "MMAP" };
	return ostream << methods[(int) method];
}

inline std::ostream &operator<<(std::ostream &ostream, video_streamer::v4l2::format format) {
	static const char *formats[] = { "UNKNOWN", "YVYU", "YUYV", "VYUY", "UYVY", "MJPEG", "H264" };
	return ostream << formats[(int) format];
}
