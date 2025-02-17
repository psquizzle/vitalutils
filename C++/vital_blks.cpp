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
#include <random>
#include <limits.h>
#include <cfloat> // Required for DBL_MAX

using namespace std;

void print_usage(const char *progname)
{
	fprintf(stderr, "Usage : %s INPUT_FILENAME [OUTPUT_FOLDER]\n\n", basename(progname).c_str());
}

int main(int argc, char *argv[])
{
	random_device rd;
	mt19937_64 rnd(rd());

	srand(time(nullptr));

	const char *progname = argv[0];
	argc--;
	argv++; // skip self

	if (argc < 1)
	{
		print_usage(progname);
		return -1;
	}

	// File + caseid
	string filename = basename(argv[0]);
	string caseid = filename;
	auto dotpos = caseid.rfind('.');
	if (dotpos != -1)
		caseid = caseid.substr(0, dotpos);

	// Output folder
	string odir = ".";
	if (argc > 1)
		odir = argv[1];

	// Open GZ
	GZReader gz(argv[0]);
	if (!gz.opened())
	{
		fprintf(stderr, "file does not exist\n");
		return -1;
	}

	// Check signature
	char sign[4];
	if (!gz.read(sign, 4))
		return -1;
	if (strncmp(sign, "VITA", 4) != 0)
	{
		fprintf(stderr, "not a vital file\n");
		return -1;
	}

	// Skip version
	if (!gz.skip(4))
		return -1;

	// Read header
	unsigned short headerlen; // header length
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
	headerlen += 2; // ?

	// Various maps
	map<unsigned short, double> tid_dtstart;
	map<unsigned short, double> tid_dtend;
	map<unsigned int, string> did_dnames;
	map<unsigned short, unsigned char> tid_rectypes;
	map<unsigned short, unsigned char> tid_recfmts;
	map<unsigned short, double> tid_gains;
	map<unsigned short, double> tid_offsets;
	map<unsigned short, double> tid_srates;
	map<unsigned short, string> tid_tnames;
	map<unsigned short, string> tid_dnames;
	set<unsigned short> tids;

	double dtstart = DBL_MAX;
	double dtend = 0;

	unsigned char rectype = 0;
	unsigned int nsamp = 0;
	unsigned char recfmt = 0;
	unsigned int fmtsize = 0;
	double gain = 0;
	double offset = 0;
	unsigned short infolen = 0;
	double dt_rec_start = 0;
	unsigned short tid = 0;

	// -----------------------------
	// First main loop (type==0 or type==9 or type==1)
	// -----------------------------
	while (!gz.eof())
	{
		unsigned char type = 0;
		if (!gz.read(&type, 1))
			break;
		unsigned int datalen = 0;
		if (!gz.read(&datalen, 4))
			break;
		if (datalen > 1000000)
			break;

		if (type == 0)
		{
			// trkinfo
			tid = 0;
			if (!gz.fetch(tid, datalen))
				goto skip_packet;

			rectype = 0;
			if (!gz.fetch(rectype, datalen))
				goto skip_packet;

			unsigned char recfmt_local;
			if (!gz.fetch(recfmt_local, datalen))
				goto skip_packet;

			string tname, unit;
			float minval, maxval, srate = 0;
			unsigned int col = 0, did = 0;
			double adc_gain = 1, adc_offset = 0;
			unsigned char montype = 0;

			if (!gz.fetch(tname, datalen))
				goto skip_packet;
			if (!gz.fetch(unit, datalen))
				goto skip_packet;
			if (!gz.fetch(minval, datalen))
				goto skip_packet;
			if (!gz.fetch(maxval, datalen))
				goto skip_packet;
			if (!gz.fetch(col, datalen))
				goto skip_packet;
			if (!gz.fetch(srate, datalen))
				goto skip_packet;
			if (!gz.fetch(adc_gain, datalen))
				goto skip_packet;
			if (!gz.fetch(adc_offset, datalen))
				goto skip_packet;
			if (!gz.fetch(montype, datalen))
				goto skip_packet;
			if (!gz.fetch(did, datalen))
				goto skip_packet;

			{
				// save into maps
				string dname = did_dnames[did];
				tid_tnames[tid] = tname;
				tid_dnames[tid] = dname;
				tid_rectypes[tid] = rectype;
				tid_recfmts[tid] = recfmt_local;
				tid_gains[tid] = adc_gain;
				tid_offsets[tid] = adc_offset;
				tid_srates[tid] = srate;
				tid_dtstart[tid] = DBL_MAX;
				tid_dtend[tid] = 0.0;
			}
		}
		else if (type == 9)
		{
			// devinfo
			unsigned int did;
			if (!gz.fetch(did, datalen))
				goto skip_packet;
			string dtype;
			if (!gz.fetch(dtype, datalen))
				goto skip_packet;
			string dname;
			if (!gz.fetch(dname, datalen))
				goto skip_packet;
			if (dname.empty())
				dname = dtype;
			did_dnames[did] = dname;
		}
		else if (type == 1)
		{
			// rec
			unsigned short infolen_local = 0;
			if (!gz.fetch(infolen_local, datalen))
				goto skip_packet;
			double dt_rec_start_local = 0;
			if (!gz.fetch(dt_rec_start_local, datalen))
				goto skip_packet;
			if (!dt_rec_start_local)
				goto skip_packet;
			unsigned short tid_local = 0;
			if (!gz.fetch(tid_local, datalen))
				goto skip_packet;

			tids.insert(tid_local);

			// glean
			auto rectype_local = tid_rectypes[tid_local];
			auto srate_local = tid_srates[tid_local];
			unsigned int nsamp_local = 0;
			double dt_rec_end = dt_rec_start_local;
			if (rectype_local == 1)
			{
				if (!gz.fetch(nsamp_local, datalen))
					goto skip_packet;
				if (srate_local > 0)
					dt_rec_end += nsamp_local / srate_local;
			}

			if (tid_dtstart[tid_local] > dt_rec_start_local)
				tid_dtstart[tid_local] = dt_rec_start_local;
			if (tid_dtend[tid_local] < dt_rec_end)
				tid_dtend[tid_local] = dt_rec_end;
			if (dtstart > dt_rec_start_local)
				dtstart = dt_rec_start_local;
			if (dtend < dt_rec_end)
				dtend = dt_rec_end;
		}

	skip_packet:
		// always skip remainder of packet
		if (!gz.skip(datalen))
			break;
	}

	// Rewind
	gz.rewind();
	if (!gz.skip(10 + headerlen))
		return -1;

	// Prepare containers
	map<unsigned short, vector<pair<double, float>>> nums;
	map<unsigned short, vector<pair<double, string>>> strs;
	map<unsigned short, vector<short>> wavs;

	for (auto tid : tids)
	{
		auto rectype = tid_rectypes[tid];
		if (rectype == 1) // wav
		{
			int wave_tid_size = (int)ceil((tid_dtend[tid] - tid_dtstart[tid]) * tid_srates[tid]);
			wavs[tid] = vector<short>(wave_tid_size, SHRT_MAX);
		}
		else if (rectype == 2)
		{
			nums[tid] = vector<pair<double, float>>();
		}
		else if (rectype == 5)
		{
			strs[tid] = vector<pair<double, string>>();
		}
	}

	// -----------------------------
	// Second main loop (type==1)
	// -----------------------------
	while (!gz.eof())
	{
		unsigned char type = 0;
		if (!gz.read(&type, 1))
			break;
		unsigned int datalen = 0;
		if (!gz.read(&datalen, 4))
			break;
		if (datalen > 1000000)
			break;

		// CHANGED: If not type==1, skip and continue
		if (type != 1)
		{
			if (!gz.skip(datalen))
				break;
			continue;
		}

		// We only handle type==1 below
		infolen = 0;
		if (!gz.fetch(infolen, datalen))
		{
			if (!gz.skip(datalen))
				break;
			continue;
		}

		dt_rec_start = 0;
		if (!gz.fetch(dt_rec_start, datalen))
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

		// CHANGED: if (!tid) skip
		if (!tid)
		{
			if (!gz.skip(datalen))
				break;
			continue;
		}

		// CHANGED: if (dt_rec_start < tid_dtstart[tid]) skip
		if (dt_rec_start < tid_dtstart[tid])
		{
			if (!gz.skip(datalen))
				break;
			continue;
		}

		// glean rectype
		rectype = tid_rectypes[tid];
		auto srate_local = tid_srates[tid];
		nsamp = 0;
		if (rectype == 1)
		{
			if (!gz.fetch(nsamp, datalen))
			{
				if (!gz.skip(datalen))
					break;
				continue;
			}
		}

		recfmt = tid_recfmts[tid];
		fmtsize = 4;
		switch (recfmt)
		{
		case 2:
			fmtsize = 8;
			break;
		case 3: // char
		case 4: // unsigned char
			fmtsize = 1;
			break;
		case 5: // short
		case 6: // unsigned short
			fmtsize = 2;
			break;
		case 7: // int
		case 8: // unsigned int
			fmtsize = 4;
			break;
		default:
			fmtsize = 4;
		}

		// Based on rectype
		if (rectype == 1)
		{
			// wav
			int idxrec = (int)((dt_rec_start - dtstart) * srate_local);
			auto &v = wavs[tid];
			if (idxrec < 0)
			{
				if (!gz.skip(datalen))
					break;
				continue;
			}
			if (idxrec + (int)nsamp >= (int)v.size())
			{
				if (!gz.skip(datalen))
					break;
				continue;
			}
			for (int i = 0; i < (int)nsamp; i++)
			{
				short cnt = 0;
				switch (recfmt)
				{
				case 3:
				{
					char ival;
					if (!gz.fetch(ival, datalen))
					{
						if (!gz.skip(datalen))
							break;
						// break or continue?
						// We should break out of the for-loop, but
						// also must break from the WHILE (outer).
						// Let's do a ‘goto outer_loop_end;’ or
						// do a little trick:
						goto wave_done;
					}
					cnt = ival;
				}
				break;
				case 4:
				{
					unsigned char ival;
					if (!gz.fetch(ival, datalen))
					{
						if (!gz.skip(datalen))
							break;
						goto wave_done;
					}
					cnt = ival;
				}
				break;
				case 5:
				{
					short ival;
					if (!gz.fetch(ival, datalen))
					{
						if (!gz.skip(datalen))
							break;
						goto wave_done;
					}
					cnt = ival;
				}
				break;
				case 6:
				{
					unsigned short ival;
					if (!gz.fetch(ival, datalen))
					{
						if (!gz.skip(datalen))
							break;
						goto wave_done;
					}
					cnt = ival;
				}
				break;
				case 7:
				{
					int ival;
					if (!gz.fetch(ival, datalen))
					{
						if (!gz.skip(datalen))
							break;
						goto wave_done;
					}
					cnt = (short)ival;
				}
				break;
				case 8:
				{
					unsigned int ival;
					if (!gz.fetch(ival, datalen))
					{
						if (!gz.skip(datalen))
							break;
						goto wave_done;
					}
					cnt = (short)ival;
				}
				break;
				case 1:
				case 2:
				default:
					// floats/doubles are apparently not handled in
					// your snippet, so skip them or parse them
					// the same way
					if (!gz.skip(fmtsize, datalen))
					{
						if (!gz.skip(datalen))
							break;
						goto wave_done;
					}
					break;
				}
				v[idxrec + i] = cnt;
			}

		wave_done:;
		}
		else if (rectype == 2)
		{
			// num
			float fval;
			if (!gz.fetch(fval, datalen))
			{
				if (!gz.skip(datalen))
					break;
				continue;
			}
			nums[tid].push_back(make_pair(dt_rec_start, fval));
		}
		else if (rectype == 5)
		{
			// str
			// skip 4 bytes?
			if (!gz.skip(4, datalen))
			{
				if (!gz.skip(datalen))
					break;
				continue;
			}
			string sval;
			if (!gz.fetch(sval, datalen))
			{
				if (!gz.skip(datalen))
					break;
				continue;
			}
			strs[tid].push_back(make_pair(dt_rec_start, sval));
		}

		// CHANGED: after parsing, skip leftover in the packet
		if (!gz.skip(datalen))
			break;
	} // end while

	// Now do the rest of the logic as before...
	map<unsigned int, unsigned long long> tid_dbtid;
	for (auto t : tids)
	{
		if (tid_dbtid.find(t) != tid_dbtid.end())
			continue;
		unsigned long long dbtid = rnd();
		tid_dbtid[t] = dbtid & LLONG_MAX;
	}

	// Write .trk.csv
	auto f = ::fopen((odir + "/" + filename + ".trk.csv").c_str(), "wt");
	for (auto t : tids)
	{
		auto rectype = tid_rectypes[t];
		char tp = 0;
		if (rectype == 1)
			tp = 'w';
		else if (rectype == 2)
			tp = 'n';
		else if (rectype == 5)
			tp = 's';
		else
			continue;

		fprintf(f, "%llu,\"%s\",%c,\"%s/%s\",%f,%f,%f,%f,%f\n",
				tid_dbtid[t],
				caseid.c_str(),
				tp,
				tid_dnames[t].c_str(),
				tid_tnames[t].c_str(),
				tid_dtstart[t],
				tid_dtend[t],
				tid_srates[t],
				tid_gains[t],
				tid_offsets[t]);
	}
	fclose(f);

	// Write .num.csv
	f = ::fopen((odir + "/" + filename + ".num.csv").c_str(), "wt");
	for (auto &it : nums)
	{
		auto t = it.first;
		auto &recs = it.second;
		for (auto &rec : recs)
			fprintf(f, "%llu,%f,%f\n", tid_dbtid[t], rec.first, rec.second);
	}
	fclose(f);

	// Write .str.csv
	f = ::fopen((odir + "/" + filename + ".str.csv").c_str(), "wt");
	for (auto &it : strs)
	{
		auto t = it.first;
		auto &recs = it.second;
		for (auto &rec : recs)
		{
			fprintf(f, "%llu,%f,%s\n",
					tid_dbtid[t],
					rec.first,
					escape_csv(rec.second).c_str());
		}
	}
	fclose(f);

	// Write .wav.csv
	f = ::fopen((odir + "/" + filename + ".wav.csv").c_str(), "wt");
	for (auto &it : wavs)
	{
		auto t = it.first;
		auto &v = it.second;
		auto sr = tid_srates[t];
		double totalSeconds = (double)v.size() / sr;
		for (double dt = 0.0; dt < totalSeconds; dt += 1.0)
		{
			int idx_start = (int)(dt * sr);
			int idx_end = idx_start + (int)ceil(sr);
			if (idx_end > (int)v.size())
				idx_end = (int)v.size();

			fprintf(f, "%llu,%f,\"", tid_dbtid[t], (dtstart + dt));
			bool firstVal = true;
			for (int idx = idx_start; idx < idx_end; idx++)
			{
				if (!firstVal)
					fputc(',', f);
				firstVal = false;
				if (v[idx] != SHRT_MAX)
					fprintf(f, "%d", v[idx]);
			}
			fprintf(f, "\"\n");
		}
	}
	fclose(f);

	return 0;
}
