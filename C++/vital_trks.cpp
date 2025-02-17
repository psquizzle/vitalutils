#include "VitalLib.h"
#include <iostream>

int main(int argc, char *argv[])
{
	if (argc < 2)
	{
		std::cerr << "Usage: " << argv[0] << " [-s] <filename>\n";
		return 1;
	}

	bool is_short = false;
	std::string vitalFile;
	// Simple logic to interpret command line
	if (std::string(argv[1]) == "-s" && argc >= 3)
	{
		is_short = true;
		vitalFile = argv[2];
	}
	else
	{
		vitalFile = argv[1];
	}

	try
	{
		// Call our library function
		VitalFileData data = parseVitalFile(vitalFile, is_short);

		// If short list is requested, maybe print a summary

		// Otherwise, print full info
		std::cout << "#dgmt," << data.tzBias << "\n";
		std::cout << "#dtstart," << data.dtStart << "\n";
		std::cout << "#dtend," << data.dtEnd << "\n";
		std::cout << "tname,tid,dname,did,rectype,dtstart,dtend,srate,minval,maxval,cnt,avgval,firstval\n";

		// Print track info
		for (auto &kv : data.tracks)
		{
			const auto &track = kv.second;

			std::string stype;
			switch (track.recType)
			{
			case 1:
				stype = "WAV";
				break;
			case 2:
				stype = "NUM";
				break;
			case 5:
				stype = "STR";
				break;
			default:
				stype = "";
				break;
			}

			double avg = 0.0;
			if (track.count > 0)
			{
				avg = track.sum / (double)track.count;
			}

			std::cout << track.trackName << ","
					  << track.tid << ","
					  << track.deviceName << ","
					  << track.deviceId << ","
					  << stype << ","
					  << track.dtStart << ","
					  << track.dtEnd << ","
					  << track.sampleRate << ","
					  << track.minVal << ","
					  << track.maxVal << ","
					  << track.count << ","
					  << avg << ","
					  << track.firstVal << "\n";
		}

		// Example: gather EEG1/EEG2 waveforms and save to CSV
		std::vector<float> eeg1_waveform, eeg2_waveform;

		for (auto &kv : data.tracks)
		{
			const auto &track = kv.second;
			if (track.recType == 1)
			{ // WAV
				if (track.trackName == "EEG1_WAV")
				{
					eeg1_waveform = track.waveform;
				}
				else if (track.trackName == "EEG2_WAV")
				{
					eeg2_waveform = track.waveform;
				}
			}
		}
		if (!eeg1_waveform.empty() || !eeg2_waveform.empty())
		{
			save_waveforms_to_csv("EEG_Waveforms.csv",
								  eeg1_waveform, "EEG1_WAV",
								  eeg2_waveform, "EEG2_WAV");
		}
	}
	catch (const std::exception &ex)
	{
		std::cerr << "Error parsing file: " << ex.what() << "\n";
		return 1;
	}

	return 0;
}
