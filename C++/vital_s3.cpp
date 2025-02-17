#include <stdio.h>
#include <stdlib.h> // exit()
#include <assert.h>
#include "zlib128/zlib.h"
#include <string>
#include <vector>
#include <map>
#include <stdarg.h> // For va_start, etc.
#include <memory>	// For std::unique_ptr
#include <time.h>
#include <set>
#include <iostream>
#include "GZReader.h"
#include "Util.h"
#include <limits.h> // LLONG_MAX, etc.
#include <filesystem>
#include <random>
#include <cfloat>	// <-- For DBL_MAX, FLT_MAX
#include <cmath>	// <-- For NAN, floor, etc.
#include <libgen.h> // <-- For basename(...) on Unix-like

namespace fs = std::filesystem;
using namespace std;

int main(int argc, char *argv[])
{
	if (argc < 2)
	{
		fprintf(stderr, "Usage : %s INPUT_FILENAME [OUTPUT_FOLDER]\n\n", argv[0]);
		return -1;
	}

	// Convert basename(...) to a std::string for convenience
	char *temp = basename(argv[1]);
	string ipath = (temp ? temp : "inputfile");

	// Decide output dir
	string odir;
	if (argc > 2)
	{
		odir = string(argv[2]);
		if (!odir.empty() && odir.back() != '/')
		{
			odir.push_back('/');
		}
	}
	else
	{
		// default to ipath if no output folder given
		odir = ipath;
	}

	fs::create_directories(odir);

	GZReader gz(argv[1]); // open the file
	if (!gz.opened())
	{
		fprintf(stderr, "file does not exist\n");
		return -1;
	}

	// header
	char sign[4];
	if (!gz.read(sign, 4))
		return -1;
	if (strncmp(sign, "VITA", 4) != 0)
	{
		fprintf(stderr, "file does not seem to be a vital file\n");
		return -1;
	}
	if (!gz.skip(4))
		return -1; // skip version

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
	headerlen += 2;

	// track data structures
	map<unsigned long, string> did_dnames;
	map<unsigned short, char> tid_rectypes; // 'W','N','S'
	map<unsigned short, unsigned char> tid_recfmts;
	map<unsigned short, double> tid_gains;
	map<unsigned short, double> tid_biases;
	map<unsigned short, double> tid_srates;

	map<unsigned short, string> tid_units;
	map<unsigned short, unsigned long> tid_samples;
	map<unsigned short, float> tid_mindisps;
	map<unsigned short, float> tid_maxdisps;
	map<unsigned short, unsigned long> tid_colors;
	map<unsigned short, unsigned char> tid_montypes;

	map<unsigned short, string> tid_dnames;
	map<unsigned short, string> tid_tnames;
	set<unsigned short> tids;

	// track start/end time
	double dtstart = DBL_MAX;
	double dtend = 0.0;

	unsigned short infolen = 0;
	double dtrec = 0.0;
	unsigned short tid = 0;

	// First pass: read metadata
	while (!gz.eof())
	{
		unsigned char type = 0;
		if (!gz.read(&type, 1))
			break;
		unsigned long datalen = 0;
		if (!gz.read(&datalen, 4))
			break;
		if (datalen > 1000000)
			break;

		if (type == 0)
		{ // trkinfo
			unsigned short tidVal = 0;
			if (!gz.fetch(tidVal, datalen))
				goto next_packet;
			unsigned char rectype = 0;
			if (!gz.fetch(rectype, datalen))
				goto next_packet;
			unsigned char recfmt;
			if (!gz.fetch(recfmt, datalen))
				goto next_packet;

			string tname, unit;
			float mindisp = 0.f, maxdisp = 0.f, srate = 0.f;
			unsigned long col = 0, didVal = 0;
			double gain = 1.0, bias = 0.0;
			unsigned char montype;

			if (!gz.fetch_with_len(tname, datalen))
				goto save_and_next_packet;
			if (!gz.fetch_with_len(unit, datalen))
				goto save_and_next_packet;
			if (!gz.fetch(mindisp, datalen))
				goto save_and_next_packet;
			if (!gz.fetch(maxdisp, datalen))
				goto save_and_next_packet;
			if (!gz.fetch(col, datalen))
				goto save_and_next_packet;
			if (!gz.fetch(srate, datalen))
				goto save_and_next_packet;
			if (!gz.fetch(gain, datalen))
				goto save_and_next_packet;
			if (!gz.fetch(bias, datalen))
				goto save_and_next_packet;
			if (!gz.fetch(montype, datalen))
				goto save_and_next_packet;
			if (!gz.fetch(didVal, datalen))
				goto save_and_next_packet;

		save_and_next_packet:
		{
			string dname = did_dnames[didVal];
			tid_dnames[tidVal] = dname;
			tid_tnames[tidVal] = tname;

			// rectype in [1:wav, 2:num, 5:str]
			if (rectype == 1)
				tid_rectypes[tidVal] = 'W';
			else if (rectype == 2)
				tid_rectypes[tidVal] = 'N';
			else if (rectype == 5)
				tid_rectypes[tidVal] = 'S';

			tid_units[tidVal] = unit;
			tid_colors[tidVal] = col;
			tid_mindisps[tidVal] = mindisp;
			tid_maxdisps[tidVal] = maxdisp;

			tid_recfmts[tidVal] = recfmt;
			tid_gains[tidVal] = gain;
			tid_biases[tidVal] = bias;
			tid_srates[tidVal] = srate;
			tid_montypes[tidVal] = montype;
			tid_samples[tidVal] = 0;
		}
		}
		else if (type == 9)
		{ // devinfo
			unsigned long didVal;
			if (!gz.fetch(didVal, datalen))
				goto next_packet;
			string dtype;
			if (!gz.fetch_with_len(dtype, datalen))
				goto next_packet;
			string dname;
			if (!gz.fetch_with_len(dname, datalen))
				goto next_packet;
			if (dname.empty())
				dname = dtype;
			did_dnames[didVal] = dname;
		}
		else if (type == 1)
		{ // rec
			unsigned short infolen2 = 0;
			if (!gz.fetch(infolen2, datalen))
				goto next_packet;
			double dtrecVal = 0.0;
			if (!gz.fetch(dtrecVal, datalen))
				goto next_packet;
			if (!dtrecVal)
				goto next_packet;
			unsigned short tidVal2 = 0;
			if (!gz.fetch(tidVal2, datalen))
				goto next_packet;

			char rectype = tid_rectypes[tidVal2];
			float srate = (float)tid_srates[tidVal2];
			unsigned long nsamp = 0;
			double dt_rec_end = dtrecVal;
			if (rectype == 'W')
			{
				if (!gz.fetch(nsamp, datalen))
					goto next_packet;
				if (srate > 0.f)
				{
					dt_rec_end += nsamp / srate;
				}
			}
			else if (rectype != 'N' && rectype != 'S')
			{
				goto next_packet; // unknown rectype
			}
			tids.insert(tidVal2);

			// track min start & max end
			if (dtstart > dtrecVal)
				dtstart = dtrecVal;
			if (dtend < dt_rec_end)
				dtend = dt_rec_end;
		}
	next_packet:
		if (!gz.skip(datalen))
			break;
	}

	gz.rewind();
	// skip 10 + headerlen
	if (!gz.skip(10 + headerlen))
		return -1;

	// allocate structures for second pass
	map<unsigned short, vector<pair<double, float>> *> nums;
	map<unsigned short, vector<pair<double, string>> *> strs;
	map<unsigned short, float *> wavs;

	// Prepare memory for wave / numeric / string tracks
	for (auto tidVal : tids)
	{
		char rt = tid_rectypes[tidVal];
		if (rt == 'W')
		{
			long wav_trk_len = (long)ceil((dtend - dtstart) * tid_srates[tidVal]);
			wavs[tidVal] = new float[wav_trk_len];
			// fill with FLT_MAX to indicate blanks
			std::fill(wavs[tidVal], wavs[tidVal] + wav_trk_len, FLT_MAX);
		}
		else if (rt == 'N')
		{
			nums[tidVal] = new vector<pair<double, float>>();
		}
		else if (rt == 'S')
		{
			strs[tidVal] = new vector<pair<double, string>>();
		}
	}

	// second pass
	while (!gz.eof())
	{
		unsigned char type = 0;
		if (!gz.read(&type, 1))
			break;
		unsigned long datalen = 0;
		if (!gz.read(&datalen, 4))
			break;
		if (datalen > 1000000)
			break;

		if (type != 1)
		{
			if (!gz.skip(datalen))
				break;
			continue;
		}

		unsigned long fmtsize = 4;
		if (!gz.fetch(infolen, datalen))
		{
			if (!gz.skip(datalen))
				break;
			continue;
		}
		if (!gz.fetch(dtrec, datalen))
		{
			if (!gz.skip(datalen))
				break;
			continue;
		}
		tid = 0;
		if (!gz.fetch(tid, datalen))
		{
			if (!gz.skip(datalen))
				break;
			continue;
		}
		if (!tid)
		{
			if (!gz.skip(datalen))
				break;
			continue;
		}

		char rt = tid_rectypes[tid];
		double sr = tid_srates[tid];
		unsigned long nsamp = 0;
		if (rt == 'W')
		{
			if (!gz.fetch(nsamp, datalen))
			{
				if (!gz.skip(datalen))
					break;
				continue;
			}
		}
		unsigned char recfmt = tid_recfmts[tid];
		switch (recfmt)
		{
		case 2:
			fmtsize = 8;
			break;
		case 3:
		case 4:
			fmtsize = 1;
			break;
		case 5:
		case 6:
			fmtsize = 2;
			break;
		}

		if (rt == 'W')
		{
			long idxrec = (long)((dtrec - dtstart) * sr);
			double gain = tid_gains[tid];
			double bias = tid_biases[tid];
			float *wptr = wavs[tid];
			if (!wptr)
			{
				if (!gz.skip(datalen))
					break;
				continue;
			}
			for (long i = 0; i < (long)nsamp; i++)
			{
				auto idxrow = idxrec + i;
				float fval = FLT_MAX;
				bool fetch_ok = true;
				switch (recfmt)
				{
				case 1:
				{ // float
					float tmp;
					if (!gz.fetch(tmp, datalen))
						fetch_ok = false;
					fval = tmp;
					break;
				}
				case 2:
				{ // double
					double dval;
					if (!gz.fetch(dval, datalen))
						fetch_ok = false;
					fval = (float)dval;
					break;
				}
				case 3:
				{ // char
					char ival;
					if (!gz.fetch(ival, datalen))
						fetch_ok = false;
					fval = float(ival);
					break;
				}
				case 4:
				{ // unsigned char
					unsigned char ival;
					if (!gz.fetch(ival, datalen))
						fetch_ok = false;
					fval = float(ival);
					break;
				}
				case 5:
				{ // short
					short ival;
					if (!gz.fetch(ival, datalen))
						fetch_ok = false;
					fval = float(ival);
					break;
				}
				case 6:
				{ // unsigned short
					unsigned short ival;
					if (!gz.fetch(ival, datalen))
						fetch_ok = false;
					fval = float(ival);
					break;
				}
				case 7:
				{ // long
					long ival;
					if (!gz.fetch(ival, datalen))
						fetch_ok = false;
					fval = float(ival);
					break;
				}
				case 8:
				{ // unsigned long
					unsigned long ival;
					if (!gz.fetch(ival, datalen))
						fetch_ok = false;
					fval = float(ival);
					break;
				}
				}
				if (!fetch_ok)
				{
					if (!gz.skip(datalen))
						break;
					break;
				}
				wptr[idxrow] = fval * gain + float(bias);
			}
		}
		else if (rt == 'N')
		{
			float fval = 0.f;
			if (!gz.fetch(fval, datalen))
			{
				if (!gz.skip(datalen))
					break;
				continue;
			}
			if (nums[tid])
			{
				nums[tid]->push_back({dtrec, fval});
			}
		}
		else if (rt == 'S')
		{
			// skip a 4-byte length field
			if (!gz.skip(4, datalen))
			{
				if (!gz.skip(datalen))
					break;
				continue;
			}
			string sval;
			if (!gz.fetch_with_len(sval, datalen))
			{
				if (!gz.skip(datalen))
					break;
				continue;
			}
			if (strs[tid])
			{
				strs[tid]->push_back({dtrec, sval});
			}
		}

		if (!gz.skip(datalen))
			break;
	}

	// Write out CSV.gz files per track
	map<unsigned short, unsigned long> tid_datasizes;
	map<unsigned short, unsigned long> tid_compsizes;

	for (auto tidVal : tids)
	{
		// Use ipath instead of filename
		auto opath = odir + '/' + ipath + '@' + tid_dnames[tidVal] + '@' + tid_tnames[tidVal] + ".csv.gz";

		// open a GZWriter
		GZWriter gzout(opath.c_str(), "w5b");

		// write header line
		{
			string hdr = "Time," + tid_dnames[tidVal] + '/' + tid_tnames[tidVal] + '\n';
			gzout.write(hdr);
		}

		char rt = tid_rectypes[tidVal];
		unsigned long num_samples = 0;

		if (rt == 'N')
		{
			auto &recs = nums[tidVal];
			sort(recs->begin(), recs->end(),
				 [](auto &a, auto &b)
				 { return a.first < b.first; });
			for (auto &rec : *recs)
			{
				double tsec = rec.first - dtstart;
				float val = rec.second;
				string line = num_to_str(tsec) + "," + num_to_str(val) + "\n";
				gzout.write(line);
				num_samples++;
			}
		}
		else if (rt == 'S')
		{
			auto &recs = strs[tidVal];
			sort(recs->begin(), recs->end(),
				 [](auto &a, auto &b)
				 { return a.first < b.first; });
			for (auto &rec : *recs)
			{
				double tsec = rec.first - dtstart;
				string line = num_to_str(tsec) + "," + escape_csv(rec.second) + "\n";
				gzout.write(line);
				num_samples++;
			}
		}
		else if (rt == 'W')
		{
			float *pstart = wavs[tidVal];
			if (!pstart)
				continue;
			double sr = tid_srates[tidVal];
			long wav_trk_len = (long)ceil((dtend - dtstart) * sr);
			float *pend = pstart + wav_trk_len;

			for (long i = 0; i < wav_trk_len; i++)
			{
				float val = pstart[i];
				double tsec = (double)i / sr;
				// We only store the actual time in the first 2 lines or last line
				// or just store time on every line, your choice
				// For now, let's store it on every line:
				string line = num_to_str(tsec);

				line += ",";
				if (val != FLT_MAX)
				{
					line += num_to_str(val);
					num_samples++;
				}
				line += "\n";
				gzout.write(line);
			}
			delete[] pstart;
			wavs[tidVal] = nullptr;
		}

		tid_samples[tidVal] = num_samples;
		tid_datasizes[tidVal] = gzout.get_datasize();
		tid_compsizes[tidVal] = gzout.get_compsize();
	}

	// Save tracklist.csv
	string tracklistPath = odir + "/tracklist.csv";
	FILE *f = ::fopen(tracklistPath.c_str(), "wt");
	if (!f)
	{
		fprintf(stderr, "Failed to open %s for writing\n", tracklistPath.c_str());
	}
	else
	{
		fprintf(f, "tname,samples,unit,mindisp,maxdisp,colors,datasize,compsize,rectype,srate,gain,bias\n");
		for (auto tidVal : tids)
		{
			fprintf(f, "%s,%lu,%s,%f,%f,%lu,%lu,%lu,%c,%f,%f,%f\n",
					(tid_dnames[tidVal] + '/' + tid_tnames[tidVal]).c_str(),
					tid_samples[tidVal],
					tid_units[tidVal].c_str(),
					tid_mindisps[tidVal],
					tid_maxdisps[tidVal],
					tid_colors[tidVal],
					tid_datasizes[tidVal],
					tid_compsizes[tidVal],
					tid_rectypes[tidVal],
					tid_srates[tidVal],
					tid_gains[tidVal],
					tid_biases[tidVal]);
		}
		::fclose(f);
	}

	// Clean up
	for (auto &kv : wavs)
	{
		if (kv.second)
			delete[] kv.second;
	}
	for (auto &kv : nums)
	{
		if (kv.second)
			delete kv.second;
	}
	for (auto &kv : strs)
	{
		if (kv.second)
			delete kv.second;
	}

	return 0;
}
