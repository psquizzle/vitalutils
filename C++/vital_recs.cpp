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
#include <cfloat>  // For DBL_MAX and DBL_MIN
#include <cmath>   // For fabs, etc.
#include <cstdint> // For int64_t, etc.
#include "GZReader.h"
#include "Util.h"
using namespace std;

void print_usage(const char *progname)
{
	fprintf(stderr,
			"Usage : %s -OPTIONS INPUT_FILENAME INTERVAL [DNAME/TNAME]\n\n"
			"OPTIONS : one or many of the following (e.g. -rlt):\n"
			"  a : print human readable time\n"
			"  u : print unix timestamp\n"
			"  r : all tracks should exist\n"
			"  l : replace blank value with the last value\n"
			"  h : print header at the first row\n"
			"  c : print filename at the first column\n"
			"  n : print the closest value from the start of the time interval\n"
			"  m : print mean value for numeric and wave tracks\n"
			"  d : print device name\n"
			"  s : skip blank rows\n\n"
			"INPUT_FILENAME : vital file name\n\n"
			"INTERVAL : time interval of each row in sec. default = 1. ex) 1/100\n\n"
			"DEVNAME/TRKNAME : comma-separated device and track name list. ex) BIS/BIS,BIS/SEF\n"
			"  if omitted, all tracks are exported.\n\n",
			basename(progname).c_str());
}

double minval(const vector<double> &v)
{
	double ret = DBL_MAX;
	for (double x : v)
	{
		if (x < ret)
			ret = x;
	}
	return ret;
}

double maxval(const vector<double> &v)
{
	double ret = -DBL_MAX; // or DBL_MIN, but safer to use -DBL_MAX
	for (double x : v)
	{
		if (x > ret)
			ret = x;
	}
	return ret;
}

// c++ string --> newly allocated C string
char *cstr(const string &s)
{
	char *ret = new char[s.size() + 1];
	copy(s.begin(), s.end(), ret);
	ret[s.size()] = '\0';
	return ret;
}

int main(int argc, char *argv[])
{
	const char *progname = argv[0];
	--argc;
	++argv; // skip program name

	bool absolute_time = false;
	bool unix_time = false;
	bool all_required = false;
	bool fill_last = false;
	bool print_header = false;
	bool print_filename = false;
	bool print_mean = false;
	bool print_dname = false;
	bool print_closest = false;
	bool skip_blank_row = false;

	// Parse any options in argv[0] if it starts with '-'
	if (argc > 0)
	{
		string opts(argv[0]);
		if (!opts.empty() && opts[0] == '-')
		{
			--argc;
			++argv;
			if (opts.find('a') != string::npos)
				absolute_time = true;
			if (opts.find('u') != string::npos)
				unix_time = true;
			if (opts.find('r') != string::npos)
				all_required = true;
			if (opts.find('l') != string::npos)
				fill_last = true;
			if (opts.find('h') != string::npos)
				print_header = true;
			if (opts.find('c') != string::npos)
				print_filename = true;
			if (opts.find('m') != string::npos)
				print_mean = true;
			if (opts.find('s') != string::npos)
				skip_blank_row = true;
			if (opts.find('n') != string::npos)
				print_closest = true;
			if (opts.find('d') != string::npos)
				print_dname = true;
		}
	}

	double epoch = 1.0;
	if (argc < 1)
	{
		print_usage(progname);
		return -1;
	}

	// If we have 2 or more remaining args, the second one is interval
	if (argc >= 2)
	{
		string sspan(argv[1]);
		auto pos = sspan.find('/');
		epoch = atof(sspan.c_str());
		if (pos != string::npos)
		{
			double divider = atof(sspan.substr(pos + 1).c_str());
			if (divider == 0)
			{
				fprintf(stderr, "divider of [TIMESPAN] should not be 0\n");
				return -1;
			}
			epoch /= divider;
		}
	}
	if (epoch <= 0)
	{
		fprintf(stderr, "[TIMESPAN] should be > 0\n");
		return -1;
	}

	// parse dname/tname if 3 or more args
	vector<string> tnames;
	vector<string> dnames;
	vector<unsigned short> tids;
	bool alltrack = true;
	if (argc >= 3)
	{
		alltrack = false;
		tnames = explode(argv[2], ',');
		size_t ncols = tnames.size();
		tids.resize(ncols);
		dnames.resize(ncols);
		for (size_t j = 0; j < ncols; j++)
		{
			auto pos = tnames[j].find('/');
			if (pos != string::npos)
			{
				dnames[j] = tnames[j].substr(0, pos);
				tnames[j] = tnames[j].substr(pos + 1);
			}
		}
	}

	string filename = argv[0];
	GZReader gz(filename.c_str());
	if (!gz.opened())
	{
		fprintf(stderr, "file does not exist\n");
		return -1;
	}

	// read vital header
	char sign[4];
	if (!gz.read(sign, 4))
		return -1;
	if (strncmp(sign, "VITA", 4) != 0)
	{
		fprintf(stderr, "file does not seem to be a vital file\n");
		return -1;
	}
	// skip version
	if (!gz.skip(4))
		return -1;

	unsigned short headerlen;
	if (!gz.read(&headerlen, 2))
		return -1;

	short dgmt = 0;
	if (headerlen >= 2)
	{
		if (!gz.read(&dgmt, sizeof(dgmt)))
			return -1;
		headerlen -= 2;
	}
	if (!gz.skip(headerlen))
		return -1;
	headerlen += 2; // adjust if needed later

	// track data
	map<unsigned short, double> tid_dtstart;
	map<unsigned short, double> tid_dtend;
	map<unsigned long, string> did_dnames;
	map<unsigned short, unsigned char> rectypes;
	map<unsigned short, unsigned char> recfmts;
	map<unsigned short, double> gains;
	map<unsigned short, double> offsets;
	map<unsigned short, float> srates;
	map<unsigned short, string> tid_tnames;
	map<unsigned short, string> tid_dnames;
	map<unsigned short, size_t> tid_col;
	map<unsigned short, bool> tid_used;

	// First pass: parse track info
	while (!gz.eof())
	{
		unsigned char type;
		if (!gz.read(&type, 1))
			break;
		unsigned long datalen;
		if (!gz.read(&datalen, 4))
			break;
		if (datalen > 1000000)
			break; // basic sanity check

		if (type == 0)
		{
			// trkinfo
			unsigned short tid;
			if (!gz.fetch(tid, datalen))
			{
				if (!gz.skip(datalen))
					break;
				continue;
			}
			unsigned char rectype;
			if (!gz.fetch(rectype, datalen))
			{
				if (!gz.skip(datalen))
					break;
				continue;
			}
			unsigned char recfmt;
			if (!gz.fetch(recfmt, datalen))
			{
				if (!gz.skip(datalen))
					break;
				continue;
			}

			string tname, unit;
			float minv, maxv, srate;
			unsigned long col, did;
			double adc_gain, adc_offset;
			unsigned char montype;

			// fetch tname
			if (!gz.fetch_with_len(tname, datalen))
			{
				if (!gz.skip(datalen))
					break;
				continue;
			}
			// fetch unit
			if (!gz.fetch_with_len(unit, datalen))
			{
				// just skip the rest if fails
				if (!gz.skip(datalen))
					break;
				continue;
			}
			if (!gz.fetch(minv, datalen))
			{
				if (!gz.skip(datalen))
					break;
				continue;
			}
			if (!gz.fetch(maxv, datalen))
			{
				if (!gz.skip(datalen))
					break;
				continue;
			}
			if (!gz.fetch(col, datalen))
			{
				if (!gz.skip(datalen))
					break;
				continue;
			}
			if (!gz.fetch(srate, datalen))
			{
				if (!gz.skip(datalen))
					break;
				continue;
			}
			if (!gz.fetch(adc_gain, datalen))
			{
				if (!gz.skip(datalen))
					break;
				continue;
			}
			if (!gz.fetch(adc_offset, datalen))
			{
				if (!gz.skip(datalen))
					break;
				continue;
			}
			if (!gz.fetch(montype, datalen))
			{
				if (!gz.skip(datalen))
					break;
				continue;
			}
			if (!gz.fetch(did, datalen))
			{
				if (!gz.skip(datalen))
					break;
				continue;
			}

			// save track info
			string dname = did_dnames[did];
			tid_tnames[tid] = tname;
			tid_dnames[tid] = dname;
			rectypes[tid] = rectype;
			recfmts[tid] = recfmt;
			gains[tid] = adc_gain;
			offsets[tid] = adc_offset;
			srates[tid] = srate;
			tid_dtstart[tid] = DBL_MAX;
			tid_dtend[tid] = 0.0;

			// if user requested specific track names, see if it matches
			if (!alltrack)
			{
				long colPos = -1;
				for (size_t i = 0; i < tnames.size(); i++)
				{
					if (tnames[i] == tname)
					{
						// if device name is blank or matches
						if (dnames[i].empty() || dnames[i] == dname)
						{
							colPos = (long)i;
							break;
						}
					}
				}
				if (colPos >= 0)
				{
					tids[colPos] = tid;
					tid_col[tid] = size_t(colPos);
				}
			}
		}
		else if (type == 9)
		{
			// devinfo
			unsigned long did;
			if (!gz.fetch(did, datalen))
			{
				if (!gz.skip(datalen))
					break;
				continue;
			}
			string dtype;
			if (!gz.fetch_with_len(dtype, datalen))
			{
				if (!gz.skip(datalen))
					break;
				continue;
			}
			string dname;
			if (!gz.fetch_with_len(dname, datalen))
			{
				if (!gz.skip(datalen))
					break;
				continue;
			}
			if (dname.empty())
				dname = dtype;
			did_dnames[did] = dname;
		}
		else if (type == 1)
		{
			// rec
			unsigned short infolen;
			if (!gz.fetch(infolen, datalen))
			{
				if (!gz.skip(datalen))
					break;
				continue;
			}
			double dt_rec_start;
			if (!gz.fetch(dt_rec_start, datalen))
			{
				if (!gz.skip(datalen))
					break;
				continue;
			}
			if (!dt_rec_start)
			{
				if (!gz.skip(datalen))
					break;
				continue;
			}
			unsigned short tid;
			if (!gz.fetch(tid, datalen))
			{
				if (!gz.skip(datalen))
					break;
				continue;
			}
			// update dtstart/dtend for that track
			unsigned char rectype = rectypes[tid];
			float srate = srates[tid];
			unsigned long nsamp = 0;
			double dt_rec_end = dt_rec_start;
			if (rectype == 1) // wave
			{
				if (!gz.fetch(nsamp, datalen))
				{
					if (!gz.skip(datalen))
						break;
					continue;
				}
				if (srate > 0)
					dt_rec_end += nsamp / srate;
			}
			if (alltrack && !tid_used[tid])
			{
				size_t col = tnames.size();
				tnames.push_back(tid_tnames[tid]);
				dnames.push_back(tid_dnames[tid]);
				tids.push_back(tid);
				tid_col[tid] = col;
				tid_used[tid] = true;
			}
			if (tid_dtstart[tid] > dt_rec_start)
				tid_dtstart[tid] = dt_rec_start;
			if (tid_dtend[tid] < dt_rec_end)
				tid_dtend[tid] = dt_rec_end;
		}

		// skip leftover data in the chunk
		if (!gz.skip(datalen))
			break;
	}

	// figure out global start/end
	double dtstart = 0, dtend = 0;
	vector<double> dtstarts, dtends;
	for (auto t : tids)
	{
		if (t == 0)
			continue;
		dtstarts.push_back(tid_dtstart[t]);
		dtends.push_back(tid_dtend[t]);
	}
	if (dtstarts.empty() || dtends.empty())
	{
		fprintf(stderr, "No data\n");
		return -1;
	}

	if (all_required)
	{
		// all tracks must have data
		dtstart = -DBL_MAX;
		for (double v : dtstarts)
			if (v > dtstart)
				dtstart = v; // max
		dtend = DBL_MAX;
		for (double v : dtends)
			if (v < dtend)
				dtend = v; // min
	}
	else
	{
		dtstart = minval(dtstarts);
		dtend = maxval(dtends);
	}

	if (dtend <= dtstart)
	{
		fprintf(stderr, "No data\n");
		return -1;
	}
	if (dtend - dtstart > 48 * 3600)
	{
		fprintf(stderr, "Data duration > 48 hrs\n");
		return -1;
	}

	// rewind and parse again
	gz.rewind();
	// skip 10 + headerlen
	if (!gz.skip(10 + headerlen))
		return -1;

	size_t ncols = tids.size();
	// how many rows
	long nrows = (long)ceil((dtend - dtstart) / epoch);

	// allocate memory for table
	vector<char *> vals(ncols * nrows, nullptr);

	// if computing mean
	vector<double> sums;
	vector<long> cnts;
	if (print_mean)
	{
		sums.resize(ncols * nrows, 0.0);
		cnts.resize(ncols * nrows, 0L);
	}

	// if printing closest
	vector<double> dists;
	if (print_closest)
	{
		dists.resize(ncols * nrows, DBL_MAX);
	}

	vector<bool> has_data_in_col(ncols, false);
	vector<bool> has_data_in_row(nrows, false);

	// second pass
	while (!gz.eof())
	{
		unsigned char type;
		if (!gz.read(&type, 1))
			break;
		unsigned long datalen;
		if (!gz.read(&datalen, 4))
			break;
		if (datalen > 1000000)
			break;

		// If it's not a rec packet, skip
		if (type != 1)
		{
			if (!gz.skip(datalen))
				break;
			continue;
		}

		// Declarations
		unsigned short infolen = 0;
		double dt_rec_start = 0.0;
		unsigned short tid = 0;
		float srate = 0.f;
		unsigned long nsamp = 0;
		unsigned char rectype = 0;
		unsigned char recfmt = 0;
		unsigned long fmtsize = 4;
		double gain = 0.0;
		double offset = 0.0;

		// fetch them carefully
		// If any fails, skip/continue
		if (!gz.fetch(infolen, datalen))
		{
			if (!gz.skip(datalen))
				break;
			continue;
		}
		if (!gz.fetch(dt_rec_start, datalen))
		{
			if (!gz.skip(datalen))
				break;
			continue;
		}
		if (dt_rec_start < dtstart)
		{
			if (!gz.skip(datalen))
				break;
			continue;
		}
		if (!gz.fetch(tid, datalen))
		{
			if (!gz.skip(datalen))
				break;
			continue;
		}
		// check if known tid
		if (tid_col.find(tid) == tid_col.end())
		{
			if (!gz.skip(datalen))
				break;
			continue;
		}

		rectype = rectypes[tid];
		srate = srates[tid];
		if (rectype == 1)
		{
			if (!gz.fetch(nsamp, datalen))
			{
				if (!gz.skip(datalen))
					break;
				continue;
			}
		}
		recfmt = recfmts[tid];
		switch (recfmt)
		{
		case 2:
			fmtsize = 8;
			break; // double
		case 3:
		case 4:
			fmtsize = 1;
			break; // char / unsigned char
		case 5:
		case 6:
			fmtsize = 2;
			break; // short / unsigned short
			// case 1, 7, or 8 => default fmtsize=4
		}
		gain = gains[tid];
		offset = offsets[tid];

		size_t icol = tid_col[tid];

		// handle track data
		if (rectype == 1)
		{
			// wave
			for (long i = 0; i < (long)nsamp; i++)
			{
				double ftime = dt_rec_start + double(i) / srate;
				double frow = (ftime - dtstart) / epoch;
				long irow = (long)(frow + (print_closest ? 0.5 : 0.0));
				if (irow < 0)
				{
					if (!gz.skip(fmtsize, datalen))
						break;
					continue;
				}
				if (irow >= nrows)
				{
					if (!gz.skip(fmtsize, datalen))
						break;
					continue;
				}
				bool skip_sample = true;
				if (print_closest)
				{
					double dist = fabs(frow - irow);
					size_t idx = size_t(irow) * ncols + icol;
					if (dist < dists[idx])
					{
						dists[idx] = dist;
						skip_sample = false;
					}
				}
				else if (print_mean)
				{
					skip_sample = false;
				}
				else
				{
					// if not closest or mean, only fill once if empty
					skip_sample = (vals[size_t(irow) * ncols + icol] != nullptr);
				}
				if (skip_sample)
				{
					if (!gz.skip(fmtsize, datalen))
						break;
					continue;
				}

				// read this sample
				string sval;
				float fval = 0.f;
				bool fetch_ok = true;
				switch (recfmt)
				{
				case 1: // float
				{
					float v;
					if (!gz.fetch(v, datalen))
						fetch_ok = false;
					sval = string_format("%f", v);
					fval = v;
					break;
				}
				case 2: // double
				{
					double v;
					if (!gz.fetch(v, datalen))
						fetch_ok = false;
					sval = string_format("%lf", v);
					fval = float(v);
					break;
				}
				case 3: // char
				{
					char v;
					if (!gz.fetch(v, datalen))
						fetch_ok = false;
					fval = float(v) * float(gain) + float(offset);
					sval = string_format("%f", fval);
					break;
				}
				case 4: // unsigned char
				{
					unsigned char v;
					if (!gz.fetch(v, datalen))
						fetch_ok = false;
					fval = float(v) * float(gain) + float(offset);
					sval = string_format("%f", fval);
					break;
				}
				case 5: // short
				{
					short v;
					if (!gz.fetch(v, datalen))
						fetch_ok = false;
					fval = float(v) * float(gain) + float(offset);
					sval = string_format("%f", fval);
					break;
				}
				case 6: // unsigned short
				{
					unsigned short v;
					if (!gz.fetch(v, datalen))
						fetch_ok = false;
					fval = float(v) * float(gain) + float(offset);
					sval = string_format("%f", fval);
					break;
				}
				case 7: // long
				{
					long v;
					if (!gz.fetch(v, datalen))
						fetch_ok = false;
					fval = float(v) * float(gain) + float(offset);
					sval = string_format("%f", fval);
					break;
				}
				case 8: // unsigned long
				{
					unsigned long v;
					if (!gz.fetch(v, datalen))
						fetch_ok = false;
					fval = float(v) * float(gain) + float(offset);
					sval = string_format("%f", fval);
					break;
				}
				}
				if (!fetch_ok)
				{
					// if fetch fails, skip leftover data and break loop
					if (!gz.skip(datalen))
						break;
					break;
				}

				size_t idx = size_t(irow) * ncols + icol;
				if (print_mean)
				{
					sums[idx] += double(fval);
					cnts[idx]++;
				}
				else
				{
					vals[idx] = cstr(sval);
				}
				has_data_in_col[icol] = true;
				has_data_in_row[size_t(irow)] = true;
			}
		}
		else if (rectype == 2)
		{
			// numeric track
			double frow = (dt_rec_start - dtstart) / epoch;
			long irow = (long)(frow + (print_closest ? 0.5 : 0.0));
			if (irow < 0 || irow >= nrows)
			{
				if (!gz.skip(datalen))
					break;
				continue;
			}
			bool skip_sample = true;
			size_t idx = size_t(irow) * ncols + icol;
			if (print_closest)
			{
				double dist = fabs(frow - irow);
				if (dist < dists[idx])
				{
					dists[idx] = dist;
					skip_sample = false;
				}
			}
			else if (print_mean)
			{
				skip_sample = false;
			}
			else
			{
				skip_sample = (vals[idx] != nullptr);
			}
			if (skip_sample)
			{
				if (!gz.skip(datalen))
					break;
				continue;
			}
			float fval = 0.f;
			if (!gz.fetch(fval, datalen))
			{
				if (!gz.skip(datalen))
					break;
				continue;
			}
			if (print_mean)
			{
				sums[idx] += double(fval);
				cnts[idx]++;
			}
			else
			{
				vals[idx] = cstr(string_format("%f", fval));
			}
			has_data_in_col[icol] = true;
			has_data_in_row[size_t(irow)] = true;
		}
		else if (rectype == 5)
		{
			// string track
			double frow = (dt_rec_start - dtstart) / epoch;
			long irow = (long)(frow + (print_closest ? 0.5 : 0.0));
			if (irow < 0 || irow >= nrows)
			{
				if (!gz.skip(datalen))
					break;
				continue;
			}
			bool skip_sample = true;
			size_t idx = size_t(irow) * ncols + icol;
			if (print_closest)
			{
				double dist = fabs(frow - irow);
				if (dist < dists[idx])
				{
					dists[idx] = dist;
					skip_sample = false;
				}
			}
			else
			{
				skip_sample = (vals[idx] != nullptr);
			}
			if (skip_sample)
			{
				if (!gz.skip(datalen))
					break;
				continue;
			}
			// skip 4-byte length field
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
			vals[idx] = cstr(escape_csv(sval));
			has_data_in_col[icol] = true;
			has_data_in_row[size_t(irow)] = true;
		}

		// skip leftover data for this packet
		if (!gz.skip(datalen))
			break;
	}

	// all_required => check if any track had no data
	if (all_required)
	{
		for (size_t j = 0; j < ncols; j++)
		{
			if (!has_data_in_col[j])
			{
				fprintf(stderr, "No data\n");
				return -1;
			}
		}
	}

	// finalize mean if needed
	if (print_mean)
	{
		for (long i = 0; i < nrows; i++)
		{
			for (size_t j = 0; j < ncols; j++)
			{
				long idx = i * (long)ncols + (long)j;
				if (cnts[idx] > 0)
				{
					double m = sums[idx] / double(cnts[idx]);
					vals[idx] = cstr(string_format("%f", m));
				}
			}
		}
	}

	// print header
	if (print_header)
	{
		if (print_filename)
			printf("Filename,");

		printf("Time");
		for (size_t j = 0; j < ncols; j++)
		{
			string colName = tnames[j];
			if (print_dname && !dnames[j].empty())
			{
				colName = dnames[j] + "/" + colName;
			}
			printf(",%s", colName.c_str());
		}
		putchar('\n');
	}

	// Output rows
	vector<char *> lastval(ncols, nullptr);
	for (long i = 0; i < nrows; i++)
	{
		if (skip_blank_row && !has_data_in_row[size_t(i)])
			continue;

		double dt = dtstart + i * epoch;

		if (print_filename)
		{
			printf("%s,", basename(filename).c_str());
		}

		if (absolute_time)
		{
			// convert dt => local time
			time_t t_local = (time_t)(dt - dgmt * 60);
			struct tm *ts = gmtime(&t_local);
			int64_t msPart = (int64_t)((dt - (int64_t)dt) * 1000.0);
			printf("%04d-%02d-%02d %02d:%02d:%02d.%03lld",
				   ts->tm_year + 1900, ts->tm_mon + 1, ts->tm_mday,
				   ts->tm_hour, ts->tm_min, ts->tm_sec,
				   (long long)msPart);
		}
		else if (unix_time)
		{
			printf("%lf", dt);
		}
		else
		{
			printf("%lf", dt - dtstart);
		}

		// columns
		for (size_t j = 0; j < ncols; j++)
		{
			char *val = vals[i * (long)ncols + (long)j];
			if (fill_last)
			{
				if (!val)
				{
					val = lastval[j];
				}
				else
				{
					lastval[j] = val;
				}
			}
			if (val)
				printf(",%s", val);
			else
				printf(",");
		}
		putchar('\n');
	}

	// cleanup
	for (auto &p : vals)
		delete[] p;

	return 0;
}
