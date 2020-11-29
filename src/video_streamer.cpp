#include <sys/select.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <easylogging++.h>
#include <atomic>
#include "video_streamer.h"
#include "v4l2_device.h"

namespace video_streamer {
	
	class heap_image_buffer_releaser: public image_buffer_releaser {
	public:
		void release(image_buffer &buffer) override {
			delete[] buffer.data();
		}
		
	};
	
}

static video_streamer::heap_image_buffer_releaser _heap_image_buffer_releaser;

video_streamer::image_buffer::image_buffer(size_t size): video_streamer::image_buffer::image_buffer(
		new uint8_t[size], size, &_heap_image_buffer_releaser
) {
}

video_streamer::stream_server::stream_server(
		std::vector<std::string> server_addresses
): std::thread(&stream_server::run, this) {
	std::unique_lock<std::mutex> lock(m_client_sockets_mutex);
	int one = 1;
	for (auto &address : server_addresses) {
		size_t dot_pos = address.rfind(':');
		if (dot_pos == std::string::npos) {
			throw stream_server_exception("Port number is missing in " + address);
		}
		if (dot_pos == 0) {
			throw stream_server_exception("Hostname or IP address is missing in " + address);
		}
		size_t address_start = 0;
		size_t address_end = dot_pos;
		if (address[address_start] == '[' && address[address_end - 1] == ']') {
			address_start++;
			address_end--;
		}
		std::string host(address, address_start, address_end - address_start);
		std::string port(address, dot_pos + 1);
		addrinfo hints = {};
		hints.ai_socktype = SOCK_STREAM;
		addrinfo *result;
		int error = getaddrinfo(host.data(), port.data(), &hints, &result);
		if (error != 0) {
			if (error == EAI_SYSTEM) {
				LOG(ERROR) << "getaddrinfo() failed for host=" << host << ", port=" << port << ": " << strerror(errno);
			} else {
				LOG(ERROR) << "getaddrinfo() failed for host=" << host << ", port=" << port << ": " << gai_strerror(errno);
			}
			throw stream_server_exception("getaddrinfo() failed for host=" + host + ", port=" + port);
		}
		posix::unique_fd socket = posix::unique_fd(::socket(
				result->ai_family, result->ai_socktype, result->ai_protocol
		));
		if (socket < 0) {
			LOG(ERROR) << "socket() failed for host=" << host << ", port=" << port << ": " << strerror(errno);
			throw stream_server_exception(std::string("Unable to create a socket: ") + strerror(errno));
		}
		if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int)) < 0) {
			LOG(WARNING) << "setsockopt(SO_REUSEADDR) failed: "<< strerror(errno);
		}
		if (bind(socket, result->ai_addr, result->ai_addrlen) != 0) {
			LOG(ERROR) << "bind() failed for host=" << host << ", port=" << port << ": " << strerror(errno);
			throw stream_server_exception(std::string("Unable to bind a socket: ") + strerror(errno));
		}
		if (listen(socket, 5) != 0) {
			LOG(ERROR) << "listen() failed for host=" << host << ", port=" << port << ": " << strerror(errno);
			throw stream_server_exception(std::string("Unable to listen a socket: ") + strerror(errno));
		}
		freeaddrinfo(result);
		LOG(INFO) << "Listening on address " << host << ", port " << port;
		m_server_sockets.push_back(std::move(socket));
	}
}

video_streamer::stream_server::~stream_server() {
	if (joinable()) {
		m_quit = true;
		join();
	}
}

void video_streamer::stream_server::send(const void *data, size_t data_size) {
	std::unique_lock<std::mutex> lock(m_client_sockets_mutex);
	auto it = m_client_sockets.begin();
	while (it != m_client_sockets.end()) {
		size_t offset = 0;
		while (offset < data_size) {
			ssize_t r = ::send(*it, (const char*) data + offset, data_size - offset, 0);
			if (r <= 0) {
				break;
			}
			offset += r;
		}
		if (offset < data_size) {
			it = m_client_sockets.erase(it);
		} else {
			++it;
		}
	}
}

void video_streamer::stream_server::send(const image_buffer &buffer) {
	send(buffer.data(), buffer.size());
}

void video_streamer::stream_server::send(const frame &frame) {
	send(frame.buffer());
}

void video_streamer::stream_server::accept_client(posix::unique_fd &server_socket) {
	sockaddr_storage socket_addr = {};
	socklen_t socket_addr_len = sizeof(socket_addr);
	posix::unique_fd socket = accept(server_socket, (sockaddr*) &socket_addr, &socket_addr_len);
	if (socket < 0) {
		LOG(ERROR) << "accept() failed: " << strerror(errno);
		throw stream_server_exception("accept() failed");
	}
	char address_buffer[std::max(INET_ADDRSTRLEN, INET6_ADDRSTRLEN)];
	const char *address = inet_ntop(
			socket_addr.ss_family,
			socket_addr.ss_family == AF_INET6 ?
			(void*) &((sockaddr_in6*) &socket_addr)->sin6_addr :
			(void*) &((sockaddr_in*) &socket_addr)->sin_addr,
			address_buffer,
			sizeof(address_buffer)
	);
	uint16_t port = ntohs(
			socket_addr.ss_family == AF_INET6 ?
			((sockaddr_in6*) &socket_addr)->sin6_port :
			((sockaddr_in*) &socket_addr)->sin_port
	);
	LOG(INFO) << "New client connected from " << address << ", port " << port;
	std::unique_lock<std::mutex> lock(m_client_sockets_mutex);
	m_client_sockets.push_back(std::move(socket));
}

void video_streamer::stream_server::run() {
	usleep(100);
	while (!m_quit) {
		fd_set read_fds;
		FD_ZERO(&read_fds);
		int max_fd = -1;
		{
			std::unique_lock<std::mutex> lock(m_server_sockets_mutex);
			for (auto &&socket : m_server_sockets) {
				FD_SET(socket, &read_fds);
				if (socket > max_fd) {
					max_fd = socket;
				}
			}
		}
		timeval timeout { .tv_sec = 1, .tv_usec = 0 };
		int r = select(max_fd + 1, &read_fds, nullptr, nullptr, &timeout);
		if (r < 0) {
			LOG(ERROR) << "select() failed: " << strerror(errno);
			throw stream_server_exception("select() failed");
		}
		if (r == 0) continue;
		std::unique_lock<std::mutex> lock(m_server_sockets_mutex);
		for (auto &&server_socket : m_server_sockets) {
			if (!FD_ISSET(server_socket, &read_fds)) continue;
			accept_client(server_socket);
		}
	}
	m_quit = false;
	LOG(INFO) << "Stopped listening for incoming connections";
}

static void configure_loggers(const char *config_file_name, bool traceLibJpeg) {
	el::Configurations conf;
	conf.setGlobally(el::ConfigurationType::Filename, "video_streamer.log");
	conf.setGlobally(
			el::ConfigurationType::Format,
			"%datetime{%Y-%M-%d %H:%m:%s.%g} [%level] [%logger] %msg"
	);
	if (config_file_name) {
		conf.parseFromFile(config_file_name);
	}
	el::Loggers::setDefaultConfigurations(conf, true);
	
	if (!traceLibJpeg) {
		conf.set(el::Level::Trace, el::ConfigurationType::Enabled, "false");
		el::Loggers::reconfigureLogger("libjpeg", conf);
	}
}

volatile bool running = true;
struct sigaction prev_sigint_handler;

static void sigint_handler(int sig) {
	if (running) {
		running = false;
		LOG(INFO) << "Received SIGINT";
	} else {
		prev_sigint_handler.sa_handler(sig);
	}
}

static void setup_sigint_handler() {
	struct sigaction sigint_action = {};
	sigint_action.sa_handler = sigint_handler;
	sigaction(SIGINT, &sigint_action, &prev_sigint_handler);
}

static std::atomic<int> frame_counter, byte_counter, jpeg_quality(80);

int video_streamer::main(int argc, char **argv, std::function<uncompressed_frame(uncompressed_frame)> frame_processor) {
	std::vector<std::string> listen_addresses;
	std::string capture_device_path = "/dev/video0";
	int capture_frame_width = -1;
	int capture_frame_height = -1;
	bool show_stats = false;
	const char *log_config_file = nullptr;
	bool trace_libjpeg = false;
	int target_bitrate = -1;
	for (auto i = 0; i < argc; i++) {
		std::string arg(argv[i]);
		if (arg == "--listen" && i < argc - 1) {
			listen_addresses.emplace_back(argv[++i]);
		} else if (arg == "--device" && i < argc - 1) {
			capture_device_path = argv[++i];
		} else if (arg == "--width" && i < argc - 1) {
			capture_frame_width = atoi(argv[++i]);
		} else if (arg == "--height" && i < argc - 1) {
			capture_frame_height = atoi(argv[++i]);
		} else if (arg == "--stats") {
			show_stats = true;
		} else if (arg == "--log-config" && i < argc - 1) {
			log_config_file = argv[++i];
		} else if (arg == "--trace-libjpeg") {
			trace_libjpeg = true;
		} else if (arg == "--target-bitrate") {
			target_bitrate = atoi(argv[++i]);
		} else if (!arg.empty()) {
			std::cerr << "Invalid command line argument: " << arg << std::endl;
		}
	}
	if (listen_addresses.empty()) {
		std::cerr << "Usage: " << argv[0] << " --device /dev/video0 --listen 127.0.0.1:1234 ..." << std::endl;
		std::cerr << "\t" << "--width NNN" << std::endl;
		std::cerr << "\t" << "--height NNN" << std::endl;
		std::cerr << "\t" << "--stats" << std::endl;
		std::cerr << "\t" << "--log-config FILE-NAME" << std::endl;
		std::cerr << "\t" << "--trace-libjpeg" << std::endl;
		std::cerr << "\t" << "--bitrate NNN" << std::endl;
		return EXIT_SUCCESS;
	}
	configure_loggers(log_config_file, trace_libjpeg);
	
	video_streamer::v4l2::capture_device device(capture_device_path);
	device.set_format(capture_frame_width, capture_frame_height, video_streamer::v4l2::format::MJPEG);
	LOG(INFO) << "Capture size is " << device.frame_width() << "x" << device.frame_height();
	LOG(INFO) << "Capture pixel format is " << device.pixel_format();
	
	video_streamer::stream_server server(listen_addresses);
	
	std::vector<std::thread> stream_threads;
	stream_threads.reserve(std::thread::hardware_concurrency());
	for (auto i = 0; i < std::thread::hardware_concurrency(); i++) {
		stream_threads.emplace_back([&device, &server, &frame_processor] {
			while (running) {
				try {
					auto frame = device.read_jpeg();
					if (frame_processor) {
						auto processed_frame = frame_processor(frame.uncompress(JCS_RGB, 3));
						auto compressed_frame = jpeg_frame(
								processed_frame, JCS_RGB, 3, jpeg_quality
						);
						server.send(compressed_frame);
						byte_counter += (int) compressed_frame.buffer().size();
					} else {
						// TODO: Recompress JPEG if the frame is exceed target bitrate
						server.send(frame);
						byte_counter += (int) frame.buffer().size();
					}
					// TODO: Adjust quality if it is exceed target bitrate
					frame_counter++;
				} catch (const video_streamer::libjpeg_exception &e) {
					LOG(WARNING) << "libjpeg error: " << e.what();
				}
			}
		});
	}
	
	setup_sigint_handler();
	while (running) {
		sleep(1);
		if (show_stats) {
			LOG(DEBUG) << "Processed " << frame_counter.exchange(0) << " frames (" <<
					   (8 * byte_counter.exchange(0) / (1024 * 1024)) << " MBit/s)";
		}
	}
	
	for (auto &&thread : stream_threads) {
		thread.join();
	}
	
	return EXIT_SUCCESS;
}
