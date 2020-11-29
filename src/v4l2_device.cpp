#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <utility>
#include <thread>
#include <easylogging++.h>
#include "v4l2_device.h"

video_streamer::v4l2::exception::exception(std::string message, int errNo): m_message(std::move(message)) {
	if (errNo) {
		LOG(ERROR) << m_message << ": " << strerror(errNo);
	} else {
		LOG(ERROR) << m_message;
	}
}

video_streamer::v4l2::capture_device::capture_device(
		std::string path,
		bool forceRead
): m_path(std::move(path)), m_fd(open(m_path.c_str(), O_RDWR)), m_format(),
   m_buffer_count(0), m_last_used_buffer(0)
{
	LOG(INFO) << "Opened " << m_path << " V4L2 capture device";
	if (m_fd < 0) {
		throw video_streamer::v4l2::exception("Unable to open V4L2 device", errno);
	}
	v4l2_capability cap = {};
	if (ioctl(VIDIOC_QUERYCAP, &cap) != 0) {
		throw video_streamer::v4l2::exception("VIDIOC_QUERYCAP failed", errno);
	}
	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		throw video_streamer::v4l2::exception("This is not a video capture device");
	}
	query_format();
	m_method = video_streamer::v4l2::capture_method::READ;
	if (cap.capabilities & V4L2_CAP_STREAMING) {
		if (forceRead) {
			LOG(WARNING) << "The device supports streaming, but read_buffer will be forced";
		} else {
			m_method = video_streamer::v4l2::capture_method::MMAP;
		}
	}
	if (m_method == video_streamer::v4l2::capture_method::READ) {
		if (!(cap.capabilities & V4L2_CAP_READWRITE)) {
			throw video_streamer::v4l2::exception("The device doesn't support read_buffer");
		}
	}
}

video_streamer::v4l2::capture_device::~capture_device() {
	std::unique_lock<std::mutex> lock(m_read_mutex);
	if (m_buffer_count) {
		for (auto i = 0; i < m_buffer_count; i++) {
			std::unique_lock<std::mutex>(m_buffers[i].mutex);
		}
		switch (m_method) {
			case video_streamer::v4l2::capture_method::READ:
				finish_read();
				break;
			case video_streamer::v4l2::capture_method::MMAP:
				stop_mmap();
				break;
		}
	}
	LOG(INFO) << "Closing V4L2 capture device " << m_path;
}

int video_streamer::v4l2::capture_device::ioctl(unsigned long request, void *param) {
	int r;
	do {
		r = ::ioctl(m_fd, request, param);
	} while (r != 0 && errno == EINTR);
	return r;
}

void video_streamer::v4l2::capture_device::query_format() {
	m_format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(VIDIOC_G_FMT, &m_format) != 0) {
		throw video_streamer::v4l2::exception("VIDIOC_G_FMT failed", errno);
	}
	unsigned int min = m_format.fmt.pix.width * 2;
	if (m_format.fmt.pix.bytesperline < min) {
		m_format.fmt.pix.bytesperline = min;
	}
	min = m_format.fmt.pix.bytesperline * m_format.fmt.pix.height;
	if (m_format.fmt.pix.sizeimage < min) {
		m_format.fmt.pix.sizeimage = min;
	}
}

video_streamer::v4l2::format video_streamer::v4l2::capture_device::pixel_format() const {
	switch (m_format.fmt.pix.pixelformat) {
		case V4L2_PIX_FMT_YVYU:
			return video_streamer::v4l2::format::YVYU;
		case V4L2_PIX_FMT_YUYV:
			return video_streamer::v4l2::format::YUYV;
		case V4L2_PIX_FMT_VYUY:
			return video_streamer::v4l2::format::VYUY;
		case V4L2_PIX_FMT_UYVY:
			return format::UYVY;
		case V4L2_PIX_FMT_MJPEG:
			return video_streamer::v4l2::format::MJPEG;
		case V4L2_PIX_FMT_H264:
			return video_streamer::v4l2::format::H264;
		default:
			return video_streamer::v4l2::format::UNKNOWN;
	}
}

int video_streamer::v4l2::capture_device::frame_width() const {
	return m_format.fmt.pix.width;
}

int video_streamer::v4l2::capture_device::frame_height() const {
	return m_format.fmt.pix.height;
}

void video_streamer::v4l2::capture_device::set_format(int width, int height, format pixel_format) {
	switch (pixel_format) {
		case format::YVYU:
			m_format.fmt.pix.pixelformat = V4L2_PIX_FMT_YVYU;
			break;
		case format::YUYV:
			m_format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
			break;
		case format::VYUY:
			m_format.fmt.pix.pixelformat = V4L2_PIX_FMT_VYUY;
			break;
		case format::UYVY:
			m_format.fmt.pix.pixelformat = V4L2_PIX_FMT_UYVY;
			break;
		case format::MJPEG:
			m_format.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
			break;
		case format::H264:
			m_format.fmt.pix.pixelformat = V4L2_PIX_FMT_H264;
			break;
		case format::UNKNOWN:
			break;
	}
	if (width >= 0) {
		m_format.fmt.pix.width = width;
	}
	if (height >= 0) {
		m_format.fmt.pix.height = height;
	}
	if (ioctl(VIDIOC_S_FMT, &m_format) < 0) {
		throw exception("VIDIOC_S_FMT failed", errno);
	}
	query_format();
	if (pixel_format != format::UNKNOWN && this->pixel_format() != pixel_format) {
		throw exception("Unable to set desired pixel format");
	}
}

unsigned int video_streamer::v4l2::capture_device::compute_buffer_count() {
	unsigned int count = std::thread::hardware_concurrency();
	if (!count) {
		LOG(WARNING) << "Unable to determine CPU count. Defaulting to 2";
	}
	if (count < 2) {
		count = 2;
	}
	return count;
}

video_streamer::image_buffer video_streamer::v4l2::capture_device::wrap_buffer(
		capture_buffer &buffer,
		size_t length
) const {
	if (m_format.fmt.pix.pixelformat == V4L2_PIX_FMT_MJPEG) {
		while (length > 0 && ((const char*) buffer.base)[length - 1] == 0) {
			length--;
		}
	}
	return image_buffer((uint8_t*) buffer.base, (size_t) length, &buffer);
}

void video_streamer::v4l2::capture_device::init_read() {
	unsigned long count = compute_buffer_count();
	m_buffer_count = count;
	m_buffers = std::unique_ptr<video_streamer::v4l2::capture_buffer[]>(new video_streamer::v4l2::capture_buffer[count]);
	for (unsigned int i = 0; i < count; i++) {
		m_buffers[i].length = m_format.fmt.pix.sizeimage;
		m_buffers[i].base = new char[m_buffers[i].length];
	}
	LOG(INFO) << "Using " << m_buffer_count << " capture buffers for READ";
}

video_streamer::image_buffer video_streamer::v4l2::capture_device::read_read() {
	m_last_used_buffer = (m_last_used_buffer + 1) % m_buffer_count;
	video_streamer::v4l2::capture_buffer &buffer = m_buffers[m_last_used_buffer];
	std::unique_lock<std::mutex> lock(buffer.mutex);
	ssize_t result = ::read(m_fd, buffer.base, buffer.length);
	if (result < 0) {
		throw video_streamer::v4l2::exception("Unable to read_buffer a frame from the capture device", errno);
	}
	lock.release();
	return wrap_buffer(buffer, result);
}

void video_streamer::v4l2::capture_device::finish_read() {
	for (auto i = 0; i < m_buffer_count; i++) {
		delete[] (char*) m_buffers[i].base;
	}
	m_buffers = nullptr;
}

void video_streamer::v4l2::capture_device::start_mmap() {
	v4l2_requestbuffers req = {};
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;
	req.count = compute_buffer_count();
	if (ioctl(VIDIOC_REQBUFS, &req) < 0) {
		throw video_streamer::v4l2::exception("VIDIOC_REQBUFS failed", errno);
	}
	if (req.count < 2) {
		throw video_streamer::v4l2::exception("Insufficient capture_buffer memory on the device");
	}
	m_buffer_count = req.count;
	m_buffers = std::unique_ptr<video_streamer::v4l2::capture_buffer[]>(new video_streamer::v4l2::capture_buffer[m_buffer_count]);
	LOG(INFO) << "Using " << req.count << " capture buffers";
	for (unsigned int i = 0; i < req.count; i++) {
		m_buffers[i].device = this;
		m_buffers[i].index = i;
	}
	for (unsigned int i = 0; i < req.count; i++) {
		v4l2_buffer buf = {};
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;
		if (ioctl(VIDIOC_QUERYBUF, &buf) < 0) {
			throw video_streamer::v4l2::exception("VIDIOC_QUERYBUF failed for capture_buffer #" + std::to_string(i), errno);
		}
		m_buffers[i].length = buf.length;
		void *ptr = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, buf.m.offset);
		if (!ptr) {
			throw video_streamer::v4l2::exception("mmap() failed for capture_buffer #" + std::to_string(i), errno);
		}
		m_buffers[i].base = ptr;
	}
	for (int i = 0; i < m_buffer_count; i++) {
		if (!m_buffers[i].base) continue;
		v4l2_buffer buf = {};
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;
		if (ioctl(VIDIOC_QBUF, &buf) < 0) {
			throw video_streamer::v4l2::exception("VIDIOC_QBUF failed for capture_buffer #" + std::to_string(i), errno);
		}
	}
	v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(VIDIOC_STREAMON, &type) != 0) {
		throw video_streamer::v4l2::exception("VIDIOC_STREAMON failed", errno);
	}
}

video_streamer::image_buffer video_streamer::v4l2::capture_device::read_mmap() {
	v4l2_buffer buf = {};
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;
	if (ioctl(VIDIOC_DQBUF, &buf) < 0) {
		throw video_streamer::v4l2::exception("VIDIOC_DQBUF failed", errno);
	}
	if (buf.index >= m_buffer_count) {
		throw video_streamer::v4l2::exception(
				"buf.index (" + std::to_string(buf.index) + ") > m_buffers.size (" +
				std::to_string(m_buffer_count) + ")"
		);
	}
	capture_buffer &buffer = m_buffers[buf.index];
	return wrap_buffer(buffer, buf.bytesused);
}

void video_streamer::v4l2::capture_device::stop_mmap() {
	v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(VIDIOC_STREAMOFF, &type) != 0) {
		LOG(WARNING) << "VIDIOC_STREAMOFF failed: " << strerror(errno);
	}
	for (auto i = 0; i < m_buffer_count; i++) {
		if (!m_buffers[i].base) continue;
		if (munmap(m_buffers[i].base, m_buffers[i].length) == 0) {
			m_buffers[i].base = nullptr;
			m_buffers[i].length = 0;
		} else {
			LOG(WARNING) << "munmap() failed for capture_buffer #" << i;
		}
	}
}

video_streamer::image_buffer video_streamer::v4l2::capture_device::read_buffer() {
	std::unique_lock<std::mutex> lock(m_read_mutex);
	switch (m_method) {
		case video_streamer::v4l2::capture_method::READ:
			if (!m_buffer_count) {
				init_read();
			}
			return read_read();
		case video_streamer::v4l2::capture_method::MMAP:
			if (!m_buffer_count) {
				start_mmap();
			}
			return read_mmap();
	}
}

video_streamer::jpeg_frame video_streamer::v4l2::capture_device::read_jpeg() {
	if (pixel_format() != format::MJPEG) {
		throw video_streamer::v4l2::exception("Pixel format is not MJPEG");
	}
	return jpeg_frame(read_buffer());
}

void video_streamer::v4l2::capture_buffer::release(image_buffer &buffer) {
	if (device->pixel_format() == format::MJPEG) {
		v4l2_buffer buf = {};
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = index;
		if (device->ioctl(VIDIOC_QBUF, &buf) < 0) {
			LOG(WARNING) << "VIDIOC_QBUF failed for buffer #1" << index << ": " << strerror(errno);
		}
	}
	mutex.unlock();
}
