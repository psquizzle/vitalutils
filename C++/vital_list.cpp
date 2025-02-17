#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <string>
#include <vector>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <map>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h> // for access(), etc. if needed
#include <dirent.h> // for opendir(), readdir(), closedir() on POSIX
#include "Util.h"	// you might have your custom utils here

using namespace std;

string dt_to_str(double dt)
{
	static char buf[4096];
	time_t t = (time_t)(dt); // ut -> localtime
	tm *ptm = localtime(&t); // simple conversion
	snprintf(buf, 4096, "%04d-%02d-%02d %02d:%02d:%02d",
			 1900 + ptm->tm_year,
			 1 + ptm->tm_mon,
			 ptm->tm_mday,
			 ptm->tm_hour,
			 ptm->tm_min,
			 ptm->tm_sec);
	return buf;
}

bool parse_csv(const string &csvSource, vector<vector<string>> &lines)
{
	bool inQuote = false;
	bool newLine = false;
	string field;
	lines.clear();
	vector<string> line;
	string::const_iterator aChar = csvSource.begin();
	while (aChar != csvSource.end())
	{
		switch (*aChar)
		{
		case '"':
			newLine = false;
			inQuote = !inQuote;
			break;
		case ',':
			newLine = false;
			if (inQuote == true)
			{
				field += *aChar;
			}
			else
			{
				line.push_back(field);
				field.clear();
			}
			break;
		case '\n':
		case '\r':
			if (inQuote == true)
			{
				field += *aChar;
			}
			else
			{
				if (newLine == false)
				{
					line.push_back(field);
					lines.push_back(line);
					field.clear();
					line.clear();
					newLine = true;
				}
			}
			break;
		default:
			newLine = false;
			field.push_back(*aChar);
			break;
		}
		aChar++;
	}
	if (field.size())
		line.push_back(field);
	if (line.size())
		lines.push_back(line);
	return true;
}

int is_dir(const char *path)
{
	struct stat path_stat;
	stat(path, &path_stat);
	return S_IFDIR & path_stat.st_mode;
}

int is_file(const char *path)
{
	struct stat path_stat;
	stat(path, &path_stat);
	return S_IFREG & path_stat.st_mode;
}

// revursively scan dir
bool scan_dir(vector<string> &out, const string &directory)
{
	// On Windows, you'd do the WIN32_FIND_DATA approach
	// On macOS/Linux:
	DIR *dir;
	struct dirent *ent;
	struct stat st;

	// FIX #1: pass c_str() to opendir
	dir = opendir(directory.c_str());
	if (!dir)
		return false;

	while ((ent = readdir(dir)) != NULL)
	{
		const string file_name = ent->d_name;
		// Build full path name
		const string full_file_name = directory + "/" + file_name;

		if (file_name[0] == '.')
			continue;

		if (stat(full_file_name.c_str(), &st) == -1)
			continue;

		const bool is_directory = (st.st_mode & S_IFDIR) != 0;
		if (is_directory)
		{
			// Recursively scan subdirectories
			scan_dir(out, full_file_name);
		}
		else
		{
			out.push_back(full_file_name);
		}
	}
	closedir(dir);
	return true;
}

int stripos(string shaystack, string sneedle)
{
	std::transform(shaystack.begin(), shaystack.end(), shaystack.begin(), ::toupper);
	std::transform(sneedle.begin(), sneedle.end(), sneedle.begin(), ::toupper);
	size_t pos = shaystack.find(sneedle);
	if (pos == shaystack.npos)
		return -1;
	return (int)pos;
}

int main(int argc, char *argv[])
{
	if (argc < 2)
	{
		fprintf(stderr, "Print the summary of vital files in a directory.\n\n\
Usage : %s [DIR]\n\n",
				/* adjust how you do basename if needed */ argv[0]);
		return -1;
	}
	argc--;
	argv++;

	string path = argv[0];
	vector<string> filelist;
	if (is_file(path.c_str()))
	{
		filelist.push_back(path);
	}
	else if (is_dir(path.c_str()))
	{
		scan_dir(filelist, path);
	}
	else
	{
		fprintf(stderr, "file does not exist\n");
		return -1;
	}

	printf("filename,path,dtstart,dtend,hrend,length,sevo,des,ppf,rftn,abp,cvp,co,bis,invos,abpavg,cvpavg\n");

	for (size_t i = 0; i < filelist.size(); i++)
	{
		string path = filelist[i];
		if (path.size() <= 6)
			continue;
		if (stripos(path.substr(path.size() - 6), ".vital") == -1)
			continue;

		// FIX #2: use popen / pclose on macOS, not _popen / _pclose
		string cmd = string("vital_trks \"") + path + "\"";
		FILE *f = popen(cmd.c_str(), "r");
		if (!f)
			continue;
		ostringstream output;
		while (!feof(f) && !ferror(f))
		{
			char buf[128];
			int bytesRead = (int)fread(buf, 1, 128, f);
			if (bytesRead > 0)
				output.write(buf, bytesRead);
		}
		pclose(f);

		string result = output.str();

		// If you have an escape_csv or basename in "Util.h", use them
		// otherwise provide your own
		printf("%s,%s,", /*escape_csv(*/ path.c_str() /*)*/, /*escape_csv(*/ path.c_str() /*)*/);

		// parse CSV
		vector<vector<string>> rows;
		if (!parse_csv(result, rows))
		{
			continue;
		}

		// gather header info
		int nfirstline = 0;
		map<string, double> infos;
		for (int j = 0; j < (int)rows.size(); j++)
		{
			vector<string> &tabs = rows[j];
			if (tabs.size() < 2)
				continue;
			if (tabs[0].size() < 2)
				continue;
			if (tabs[0][0] == '#')
			{
				infos[tabs[0].substr(1)] = atof(tabs[1].c_str());
				nfirstline = j + 1;
			}
		}

		if (rows.size() <= 1)
		{
			putchar('\n');
			continue;
		}

		double dtstart = infos["dtstart"];
		double dtend = infos["dtend"];
		double dtlen = dtend - dtstart;

		// FIX #3: pass a string literal to printf
		if (dtstart)
			printf("%s", dt_to_str(dtstart).c_str());
		putchar(',');
		if (dtend)
			printf("%s", dt_to_str(dtend).c_str());
		putchar(',');

		unsigned char hassevo = 0;
		unsigned char hasdes = 0;
		unsigned char hasppf = 0;
		unsigned char hasrftn = 0;
		unsigned char hasabp = 0;
		unsigned char hascvp = 0;
		unsigned char hasco = 0;
		unsigned char hasbis = 0;
		unsigned char hasinvos = 0;

		vector<string> tabnames;
		if ((int)rows.size() > nfirstline)
		{
			tabnames = rows[nfirstline];
		}
		nfirstline++;

		// find column indexes
		auto findIndex = [&](const string &colName)
		{
			auto it = find(tabnames.begin(), tabnames.end(), colName);
			return (int)distance(tabnames.begin(), it);
		};
		int dtend_idx = findIndex("dtend");
		int tname_idx = findIndex("tname");
		int maxval_idx = findIndex("maxval");
		int rectype_idx = findIndex("rectype");
		int firstval_idx = findIndex("firstval");
		int avgval_idx = findIndex("avgval");

		double hrend = 0;
		string abpavg;
		string cvpavg;

		for (int j = 0; j < (int)rows.size(); j++)
		{
			vector<string> &tabs = rows[j];
			if ((int)tabs.size() <= maxval_idx || (int)tabs.size() <= firstval_idx || (int)tabs.size() <= rectype_idx)
				continue;

			string tname = tabs[tname_idx];
			float maxval = atof(tabs[maxval_idx].c_str());
			string firstval = tabs[firstval_idx];
			string rectype = tabs[rectype_idx];

			if (!hassevo && stripos(tname, "SEVO") > -1 && maxval > 0)
				hassevo = 1;
			if (!hassevo && stripos(tname, "AGENT") > -1 && stripos(firstval, "SEVO") > -1)
				hassevo = 1;

			if (!hasdes && stripos(tname, "DES") > -1 && maxval > 0)
				hasdes = 1;
			if (!hasdes && stripos(tname, "AGENT") > -1 && stripos(firstval, "DES") > -1)
				hasdes = 1;

			if (!hasppf && stripos(tname, "DRUG") > -1 && stripos(firstval, "PROP") > -1)
				hasppf = 1;
			if (!hasrftn && stripos(tname, "DRUG") > -1 && stripos(firstval, "REMI") > -1)
				hasrftn = 1;

			if (!hasabp && stripos(tname, "ART") > -1 && rectype == "NUM" && maxval > 50)
				hasabp = 1;
			if (!hascvp && stripos(tname, "CVP") > -1 && rectype == "NUM")
				hascvp = 1;
			if (!hasco && tname == "CO" && rectype == "NUM")
				hasco = 1;
			if (!hasbis && stripos(tname, "BIS") > -1 && rectype == "NUM" && maxval > 0)
				hasbis = 1;
			if (!hasinvos && stripos(tname, "SCO") > -1 && rectype == "NUM" && maxval > 0)
				hasinvos = 1;

			// get dtend for HR
			if ((int)tabs.size() > dtend_idx && tname == "HR")
			{
				hrend = atof(tabs[dtend_idx].c_str());
			}
			// get ABP/CVP averages
			if ((int)tabs.size() > avgval_idx)
			{
				if (stripos(tname, "ART") > -1 && stripos(tname, "MBP") > -1 && rectype == "NUM")
					abpavg = tabs[avgval_idx];
				if (stripos(tname, "CVP") > -1 && stripos(tname, "MBP") > -1 && rectype == "NUM")
					cvpavg = tabs[avgval_idx];
			}
		}

		// print hrend
		printf("%s,", dt_to_str(hrend).c_str());

		// length
		if (dtstart || dtend)
			printf("%lf", dtlen);
		putchar(',');

		// print flags + abpavg + cvpavg
		printf("%u,%u,%u,%u,%u,%u,%u,%u,%u,%s,%s\n",
			   hassevo, hasdes, hasppf, hasrftn, hasabp, hascvp, hasco, hasbis, hasinvos,
			   abpavg.c_str(), cvpavg.c_str());
	}

	return 0;
}
