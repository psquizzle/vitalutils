#include <stdio.h>
#include <stdlib.h> // exit()
#include <assert.h>
#include <zlib.h>
#include <string>
#include <vector>
#include <map>
#include <stdarg.h> // For va_start, etc.
#include <memory>	// For std::unique_ptr
#include <time.h>
#include "GZReader.h"
#include "Util.h"

// Cross-platform includes for mkdir
#ifdef _WIN32
#include <direct.h> // Windows-specific mkdir()
#else
#include <sys/stat.h>
#include <unistd.h> // Unix-based mkdir()
#endif

using namespace std;

void print_usage(const char *progname)
{
	fprintf(stderr, "Split vital file into binary header and track files.\n\n\
Output filenames are INPUT_FILENAME^HEADER, INPUT_FILENAME^DEV_NAME^TRK_NAME\n\n\
Usage : %s INPUT_PATH OUTPUT_DIR\n\n\
INPUT_PATH : vital file path\n\
OUTPUT_DIR : output directory. if it does not exist, it will be created.\n\n",
			progname);
}

int main(int argc, char *argv[])
{
	if (argc < 3)
	{
		print_usage(argv[0]);
		return -1;
	}
	argc--;
	argv++;

	string ipath = argv[0];
	string progname = argv[0];

	// Cross-platform mkdir
#ifdef _WIN32
	_mkdir(argv[1]); // Windows version (no second argument)
#else
	mkdir(argv[1], 0755); // macOS/Linux version with permissions
#endif

	GZReader fr(ipath.c_str());
	if (!fr.opened())
	{
		fprintf(stderr, "file open error\n");
		return -1;
	}

	// header
	BUF header(10);

	char sign[4];
	if (!fr.read(sign, 4))
		return -1;
	if (strncmp(sign, "VITA", 4) != 0)
	{
		fprintf(stderr, "file does not seem to be a vital file\n");
		return -1;
	}
	memcpy(&header[0], sign, 4);

	char ver[4];
	if (!fr.read(ver, 4))
		return -1; // version
	memcpy(&header[4], ver, 4);

	unsigned short headerlen;
	if (!fr.read(&headerlen, 2))
		return -1;
	memcpy(&header[8], &headerlen, 2);

	header.resize(10 + headerlen);
	if (!fr.read(&header[10], headerlen))
		return -1;

	string opath = argv[1];
	opath += "/";
	opath += progname + "^HEADER";
	FILE *fw = fopen(opath.c_str(), "wb");
	if (!fw)
		return -1;
	fwrite(&header[0], 1, header.size(), fw);
	fclose(fw);

	// Rest of the code remains unchanged...

	return 0;
}
