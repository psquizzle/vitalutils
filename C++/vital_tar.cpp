#include <stdio.h>
#include <stdlib.h> // exit()
#include <assert.h>
#include <fcntl.h>

#ifdef _WIN32
#include <io.h>		// Windows-only
#include <direct.h> // For mkdir() on Windows
#else
#include <unistd.h>	  // For fileno() on Unix
#include <sys/stat.h> // For mkdir() on Unix
#endif

#include <sys/stat.h>
#include <zlib.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <stdarg.h> // For va_start, etc.
#include <memory>	// For std::unique_ptr
#include <time.h>
#include "GZReader.h"
#include "Util.h"

using namespace std;

void print_usage(const char *progname)
{
	fprintf(stderr, "Extract tracks from vital file into another vital file.\n\n\
Usage : %s DNAME/TNAME INPUT1 [INPUT2] [INPUT3]\n\n\
INPUT_PATH: vital file path\n\
DEVNAME/TRKNAME : comma-separated device and track name list. ex) BIS/BIS,BIS/SEF\n\
if omitted, all tracks are copied.\n\n",
			progname);
}

#pragma pack(push, 1)
struct tar_header
{
	char name[100];
	char mode[8];
	char owner[8];
	char group[8];
	char size[12];
	char mtime[12];
	char checksum[8];
	char type;
	char linkname[100];
	char _padding[255];
};
#pragma pack(pop)

struct tar_file
{
	FILE *m_fd = nullptr;

	unsigned round_up(unsigned n, unsigned incr)
	{
		return n + (incr - n % incr) % incr;
	}

	unsigned long checksum(const tar_header *rh)
	{
		unsigned long i;
		unsigned char *p = (unsigned char *)rh;
		unsigned long res = 256;
		for (i = 0; i < offsetof(tar_header, checksum); i++)
		{
			res += p[i];
		}
		for (i = offsetof(tar_header, type); i < sizeof(*rh); i++)
		{
			res += p[i];
		}
		return res;
	}

	tar_file()
	{
#ifdef _WIN32
		_setmode(_fileno(stdout), _O_BINARY); // Windows: Set binary mode
#else
		// No need for _setmode() on Unix
#endif
		m_fd = stdout;
	}

	tar_file(const char *filename)
	{
		m_fd = fopen(filename, "wb");
	}

	virtual ~tar_file()
	{
		// finalize
		write_null_bytes(sizeof(tar_header) * 2);
		if (m_fd)
			fclose(m_fd);
	}

private:
	bool write_null_bytes(long n)
	{
		char c = 0;
		for (long i = 0; i < n; i++)
		{
			if (!fwrite(&c, 1, 1, m_fd))
			{
				return false;
			}
		}
		return true;
	}

public:
	bool write(const char *name, const vector<unsigned char> &data)
	{
		// Write header
		tar_header rh = {
			0,
		};
		strcpy(rh.name, name);
		strcpy(rh.mode, "0664");
		sprintf(rh.size, "%o", (unsigned int)data.size());
		rh.type = '0'; // regular file

		// Calculate and write checksum
		auto chksum = checksum(&rh);
		sprintf(rh.checksum, "%06o", (unsigned int)chksum);
		rh.checksum[7] = ' ';

		// header
		if (!fwrite(&rh, 1, sizeof(rh), m_fd))
			return false;

		// Write data
		for (size_t pos = 0; pos < data.size(); pos += 512)
		{
			long nwrite = 512;
			if (pos + 512 > data.size())
				nwrite = data.size() - pos;

			if (!fwrite(&data[pos], 1, nwrite, m_fd))
			{
				return false;
			}

			if (nwrite != 512)
			{
				return write_null_bytes(512 - nwrite);
			}
		}

		return true;
	}
};

int main(int argc, char *argv[])
{
	if (argc <= 2)
	{
		print_usage(argv[0]);
		return -1;
	}

	argc--;
	argv++;
	string dtname = argv[0];
	argc--;
	argv++;
	vector<string> tnames;
	vector<string> dnames;
	set<unsigned short> tids;

// Cross-platform mkdir
#ifdef _WIN32
	_mkdir(argv[1]); // Windows version (no second argument)
#else
	mkdir(argv[1], 0755); // macOS/Linux version with permissions
#endif

	tar_file tar; // Tar output
	for (unsigned long ifile = 0; ifile < argc; ifile++)
	{
		string ipath = argv[ifile];

		bool is_vital = true;
		if (ipath.size() < 6)
			is_vital = false;
		else if (ipath.substr(ipath.size() - 6) != ".vital")
			is_vital = false;

		if (!is_vital)
		{
			vector<unsigned char> buf;
			auto f = fopen(ipath.c_str(), "rb");
			fseek(f, 0, SEEK_END);
			auto sz = ftell(f);
			buf.resize(sz);
			rewind(f);
			fread(&buf[0], 1, sz, f);
			fclose(f);
			tar.write(ipath.c_str(), buf);
			continue;
		}

		GZBuffer fw;
		GZReader fr(ipath.c_str());
		if (!fr.opened() || !fw.opened())
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
			return -1;
		memcpy(&header[4], ver, 4);

		unsigned short headerlen;
		if (!fr.read(&headerlen, 2))
			return -1;
		memcpy(&header[8], &headerlen, 2);

		header.resize(10 + headerlen);
		if (!fr.read(&header[10], headerlen))
			return -1;

		fw.write(&header[0], header.size());
		tar.write(ipath.c_str(), fw.m_comp);
	}

	return 0;
}
