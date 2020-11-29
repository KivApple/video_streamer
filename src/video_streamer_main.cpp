#include <easylogging++.h>
#include "v4l2_device.h"

INITIALIZE_EASYLOGGINGPP

int main(int argc, char *argv[]) {
	return video_streamer::main(argc, argv);
}
