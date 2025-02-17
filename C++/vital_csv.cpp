#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <zlib.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <time.h>
#include <random>
#include <iostream>
#include <cfloat> // For DBL_MAX

#include "GZReader.h"
#include "Util.h"

using namespace std;

void print_usage(const char *progname)
{
	fprintf(stderr, "Usage : %s INPUT_FILENAME [OUTPUT_FOLDER]\n\n", progname);
}

int main(int argc, char *argv[])
{
	random_device rd;
	mt19937_64 rnd(rd());

	srand(time(nullptr));

	if (argc < 2)
	{
		print_usage(argv[0]);
		return -1;
	}

	string filename = basename(argv[1]);
	string caseid = filename;
	auto dotpos = caseid.rfind('.');
	if (dotpos != string::npos)
		caseid = caseid.substr(0, dotpos);

	string odir = (argc > 2) ? argv[2] : ".";

	GZReader gz(argv[1]);
	if (!gz.opened())
	{
		fprintf(stderr, "File does not exist\n");
		return -1;
	}

	// Header processing
	char sign[4];
	if (!gz.read(sign, 4) || strncmp(sign, "VITA", 4) != 0)
	{
		fprintf(stderr, "Invalid vital file format\n");
		return -1;
	}

	if (!gz.skip(4))
		return -1; // Skip version info

	unsigned short headerlen;
	if (!gz.read(&headerlen, 2))
		return -1;

	short dgmt;
	if (headerlen >= 2)
	{
		if (!gz.read(&dgmt, sizeof(dgmt)))
			return -1;
		headerlen -= 2;
	}

	if (!gz.skip(headerlen))
		return -1;

	// Data structures
	map<unsigned short, double> tid_dtstart, tid_dtend, tid_srates;
	map<unsigned short, unsigned char> tid_rectypes, tid_recfmts;
	map<unsigned short, double> tid_gains, tid_offsets;
	map<unsigned short, string> tid_tnames, tid_dnames;
	map<unsigned long, string> did_dnames;
	set<unsigned short> tids;

	double dtstart = DBL_MAX, dtend = 0;

	while (!gz.eof())
	{
		unsigned char type = 0;
		if (!gz.read(&type, 1))
			break;

		unsigned long datalen = 0;
		if (!gz.read(&datalen, 4) || datalen > 1000000)
			break;

		if (type == 0)
		{ // Track info
			unsigned short tid;
			unsigned char rectype, recfmt;
			string tname, unit;
			float minval, maxval, srate;
			unsigned long col, did;
			double adc_gain, adc_offset;
			unsigned char montype;

			if (!gz.fetch(tid, datalen) ||
				!gz.fetch(rectype, datalen) ||
				!gz.fetch(recfmt, datalen) ||
				!gz.fetch(tname, datalen) ||
				!gz.fetch(unit, datalen) ||
				!gz.fetch(minval, datalen) ||
				!gz.fetch(maxval, datalen) ||
				!gz.fetch(col, datalen) ||
				!gz.fetch(srate, datalen) ||
				!gz.fetch(adc_gain, datalen) ||
				!gz.fetch(adc_offset, datalen) ||
				!gz.fetch(montype, datalen) ||
				!gz.fetch(did, datalen))
			{
				gz.skip(datalen);
				continue;
			}

			tid_tnames[tid] = tname;
			tid_dnames[tid] = did_dnames[did];
			tid_rectypes[tid] = rectype;
			tid_recfmts[tid] = recfmt;
			tid_gains[tid] = adc_gain;
			tid_offsets[tid] = adc_offset;
			tid_srates[tid] = srate;
			tid_dtstart[tid] = DBL_MAX;
			tid_dtend[tid] = 0.0;
		}

		if (type == 9)
		{ // Device info
			unsigned long did;
			string dtype, dname;
			if (!gz.fetch(did, datalen) ||
				!gz.fetch(dtype, datalen) ||
				!gz.fetch(dname, datalen))
			{
				gz.skip(datalen);
				continue;
			}
			if (dname.empty())
				dname = dtype;
			did_dnames[did] = dname;
		}

		if (type == 1)
		{ // Recording
			unsigned short infolen, tid;
			double dt_rec_start;

			if (!gz.fetch(infolen, datalen) ||
				!gz.fetch(dt_rec_start, datalen) ||
				!gz.fetch(tid, datalen))
			{
				gz.skip(datalen);
				continue;
			}

			tids.insert(tid);
			auto rectype = tid_rectypes[tid];
			auto srate = tid_srates[tid];
			unsigned long nsamp = 0;
			double dt_rec_end = dt_rec_start;

			if (rectype == 1 && gz.fetch(nsamp, datalen))
			{
				if (srate > 0)
					dt_rec_end += nsamp / srate;
			}

			tid_dtstart[tid] = min(tid_dtstart[tid], dt_rec_start);
			tid_dtend[tid] = max(tid_dtend[tid], dt_rec_end);
			dtstart = min(dtstart, dt_rec_start);
			dtend = max(dtend, dt_rec_end);
		}

		gz.skip(datalen);
	}

	gz.rewind();
	gz.skip(10 + headerlen);

	// Writing CSV files
	auto write_csv = [&](const string &filepath, auto &data)
	{
		FILE *f = fopen(filepath.c_str(), "wt");
		if (!f)
			return;

		for (const auto &[tid, records] : data)
		{
			for (const auto &[dt, val] : records)
			{
				if constexpr (std::is_same_v<std::decay_t<decltype(val)>, std::string>)
				{
					fprintf(f, "%hu,%f,%s\n", tid, dt, escape_csv(val).c_str());
				}
				else
				{
					fprintf(f, "%hu,%f,%f\n", tid, dt, static_cast<double>(val));
				}
			}
		}

		fclose(f);
	};

	map<unsigned short, vector<pair<double, float>>> nums;
	map<unsigned short, vector<pair<double, string>>> strs;
	map<unsigned short, vector<short>> wavs;

	for (auto tid : tids)
	{
		auto rectype = tid_rectypes[tid];
		if (rectype == 1)
		{
			long wave_size = static_cast<long>((tid_dtend[tid] - tid_dtstart[tid]) * tid_srates[tid]);
			wavs[tid] = vector<short>(wave_size);
		}
		else if (rectype == 2)
		{
			nums[tid] = {};
		}
		else if (rectype == 5)
		{
			strs[tid] = {};
		}
	}

	write_csv(odir + "/" + filename + ".num.csv", nums);
	write_csv(odir + "/" + filename + ".str.csv", strs);

	return 0;
}
