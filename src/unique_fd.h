#pragma once

#include <unistd.h>

namespace video_streamer {
	namespace posix {
		
		class unique_fd {
			int m_value;
			
		public:
			unique_fd(int value): m_value(value) {
			}
			
			unique_fd(unique_fd &&fd): m_value(fd.m_value) {
				fd.m_value = -1;
			}
			
			unique_fd &operator=(unique_fd &&fd) noexcept {
				m_value = fd.m_value;
				fd.m_value = -1;
				return *this;
			}
			
			~unique_fd() {
				if (m_value >= 0) {
					close(m_value);
				}
			}
			
			operator int() const {
				return m_value;
			}
			
		};
	
	}
}

