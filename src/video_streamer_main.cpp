#include <easylogging++.h>
#include <thread>
#include "v4l2_device.h"

INITIALIZE_EASYLOGGINGPP

int main(int argc, char *argv[]) {
	return video_streamer::main(argc, argv);
}
