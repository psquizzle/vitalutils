#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <set>
#include <string>
#include <memory>
#include <limits>
#include <cstdlib>
#include <cfloat>
#include <zlib.h>
#include "GZReader.h"
#include "Util.h"

#ifdef _WIN32
#include <direct.h> // Windows-specific
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace std;

void print_usage(const string &progname)
{
	cerr << "Copy tracks from a vital file into another vital file.\n\n"
		 << "Usage : " << progname << " INPUT_PATH OUTPUT_PATH [DNAME/TNAME] [MAX_LENGTH_IN_SEC]\n\n"
		 << "INPUT_PATH: vital file path\n"
		 << "OUTPUT_PATH: output file path\n"
		 << "DEVNAME/TRKNAME: comma-separated device and track name list. ex) BIS/BIS,BIS/SEF\n"
		 << "If omitted, all tracks are copied.\n\n"
		 << "MAX_LENGTH_IN_SEC: maximum length in seconds\n";
}

int main(int argc, char *argv[])
{
	if (argc < 3)
	{
		print_usage(argv[0]);
		return EXIT_FAILURE;
	}

	string input_path = argv[1];
	string output_path = argv[2];

	// Simple file copy mode
	if (argc == 3)
	{
		ifstream source(input_path, ios::binary);
		ofstream dest(output_path, ios::binary);

		if (!source.is_open() || !dest.is_open())
		{
			cerr << "File open error\n";
			return EXIT_FAILURE;
		}

		dest << source.rdbuf();
		return EXIT_SUCCESS;
	}

	// Parsing command-line arguments
	vector<string> tnames, dnames;
	set<unsigned short> tids;
	double max_length = 0.0;
	bool all_tracks = true;

	string str_dtnames, str_maxlen;
	if (argc >= 5)
	{
		if (is_numeric(argv[4]))
		{
			str_maxlen = argv[4];
			str_dtnames = argv[3];
		}
		else if (is_numeric(argv[3]))
		{
			str_maxlen = argv[3];
			str_dtnames = argv[4];
		}
	}
	else if (argc >= 4)
	{
		if (is_numeric(argv[3]))
		{
			str_maxlen = argv[3];
		}
		else
		{
			str_dtnames = argv[3];
		}
	}

	if (!str_maxlen.empty())
	{
		float n = strtof(str_maxlen.c_str(), nullptr);
		if (n > 0)
			max_length = n;
	}

	if (!str_dtnames.empty())
	{
		all_tracks = false;
		tnames = explode(str_dtnames, ',');
		dnames.resize(tnames.size());
		for (size_t j = 0; j < tnames.size(); j++)
		{
			size_t pos = tnames[j].find('/');
			if (pos != string::npos)
			{
				dnames[j] = tnames[j].substr(0, pos);
				tnames[j] = tnames[j].substr(pos + 1);
			}
		}
	}

	GZReader fr(input_path.c_str());
	GZWriter fw(output_path.c_str());

	if (!fr.opened() || !fw.opened())
	{
		cerr << "File open error\n";
		return EXIT_FAILURE;
	}

	// Read and write header
	BUF header(10);
	char sign[4];

	if (!fr.read(sign, 4) || strncmp(sign, "VITA", 4) != 0)
	{
		cerr << "File does not seem to be a vital file\n";
		return EXIT_FAILURE;
	}
	memcpy(&header[0], sign, 4);

	char ver[4];
	if (!fr.read(ver, 4))
		return EXIT_FAILURE;
	memcpy(&header[4], ver, 4);

	unsigned short headerlen;
	if (!fr.read(&headerlen, 2))
		return EXIT_FAILURE;
	memcpy(&header[8], &headerlen, 2);

	header.resize(10 + headerlen);
	if (!fr.read(&header[10], headerlen))
		return EXIT_FAILURE;

	fw.write(&header[0], header.size());

	// Track and device information
	map<unsigned long, string> did_dname;
	map<unsigned long, BUF> did_di;
	map<unsigned short, string> tid_tname;
	map<unsigned short, BUF> tid_ti;
	map<unsigned short, unsigned long> tid_did;
	map<unsigned short, BUF> tid_recs;

	// Scan file for data range
	double dt_start = numeric_limits<double>::max();
	double dt_end = 0.0;

	if (max_length)
	{
		while (!fr.eof())
		{
			unsigned char type;
			if (!fr.read(&type, 1))
				break;
			unsigned long data_len;
			if (!fr.read(&data_len, 4))
				break;
			if (data_len > 1000000)
				break;

			if (type == 1)
			{
				unsigned short info_len;
				double dt;
				if (!fr.fetch(info_len, data_len) || !fr.fetch(dt, data_len))
					continue;
				if (!dt)
					continue;
				dt_start = min(dt_start, dt);
				dt_end = max(dt_end, dt);
			}
			if (!fr.skip(data_len))
				break;
		}

		if (dt_end <= dt_start)
		{
			cerr << "No data\n";
			return EXIT_FAILURE;
		}
		if (dt_end - dt_start > 48 * 3600)
		{
			cerr << "Data duration > 48 hrs\n";
			return EXIT_FAILURE;
		}

		fr.rewind();
		if (!fr.skip(10 + headerlen))
			return EXIT_FAILURE;
	}

	// Read and process packets
	while (!fr.eof())
	{
		unsigned char packet_type;
		if (!fr.read(&packet_type, 1))
			break;
		unsigned long packet_len;
		if (!fr.read(&packet_len, 4))
			break;
		if (packet_len > 1000000)
			break;

		BUF packet_header(5);
		packet_header[0] = packet_type;
		memcpy(&packet_header[1], &packet_len, 4);

		BUF buf(packet_len);
		if (!fr.read(&buf[0], packet_len))
			break;

		if (packet_type == 9)
		{
			unsigned long did;
			string dtype, dname;
			if (buf.fetch(did) && buf.fetch_with_len(dtype) && buf.fetch_with_len(dname))
			{
				if (dname.empty())
					dname = dtype;
				did_dname[did] = dname;
			}
		}
		else if (packet_type == 0)
		{
			unsigned short tid;
			buf.skip(2);
			string tname;
			if (buf.fetch(tid) && buf.fetch_with_len(tname))
			{
				buf.skip(33);
				unsigned long did = 0;
				buf.fetch(did);

				tid_did[tid] = did;
				tid_tname[tid] = tname;
				string dname = did_dname[did];

				if (!all_tracks)
				{
					bool matched = any_of(tnames.begin(), tnames.end(), [&](const string &tn)
										  { return (tn == "*" || tn == tname) &&
												   (dnames.empty() || dnames[0] == "*" || dnames[0] == dname); });
					if (!matched)
						continue;
				}
			}
		}
		else if (packet_type == 1)
		{
			buf.skip(2);
			double dt;
			unsigned short tid;
			if (!buf.fetch(dt) || !buf.fetch(tid))
				continue;

			if (max_length && dt > dt_start + max_length)
				continue;
			if (!all_tracks && tids.find(tid) == tids.end())
				continue;
		}

		// Write packet
		fw.write(&packet_header[0], packet_header.size());
		fw.write(&buf[0], buf.size());
	}

	return EXIT_SUCCESS;
}
