#include "VitalLib.h"
#include <iostream>
#include <sstream>

int main(int argc, char *argv[])
{
	if (argc < 2)
	{
		std::cerr << "Usage: " << argv[0] << " [-s] <filename>\n";
		return 1;
	}

	bool is_short = false;
	std::string vitalFile;
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
		VitalFileData data = parseVitalFile(vitalFile, is_short);

		std::cout << "#dgmt," << data.tzBias << "\n";
		std::cout << "#dtstart," << data.dtStart << "\n";
		std::cout << "#dtend," << data.dtEnd << "\n";
		std::cout << "tname,tid,dname,did,rectype,dtstart,dtend,srate,minval,maxval,cnt,avgval,firstval\n";

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

		// Print NUM track values
		std::cout << "\n# NUMERIC VALUES\n";
		std::cout << "Time, Track, Value\n";
		for (const auto &kv : data.tracks)
		{
			const auto &track = kv.second;
			if (track.recType == 2 && track.trackName == "BIS") // NUM
			{
				for (size_t i = 0; i < track.recordTimestamps.size(); i++)
				{
					std::cout << track.recordTimestamps[i] << ","
							  << track.trackName << ","
							  << track.numericValues[i] << "\n";
				}
			}
		}

		// Print STR track values
		std::cout << "\n# STRING VALUES (EVENTS)\n";
		std::cout << "Time, Track, Value\n";
		for (const auto &kv : data.tracks)
		{
			const auto &track = kv.second;
			if (track.recType == 5 && track.trackName == "EVENT") // STR
			{
				for (size_t i = 0; i < track.recordTimestamps.size(); i++)
				{
					std::cout << track.recordTimestamps[i] << ","
							  << track.trackName << ","
							  << track.stringValues[i] << "\n";
				}
			}
		}

		// Example: Save EEG1/EEG2 waveforms to CSV
		std::vector<float> eeg1_waveform, eeg2_waveform;
		std::vector<double> eeg1_timestamps, eeg2_timestamps;
		for (auto &kv : data.tracks)
		{
			const auto &track = kv.second;
			if (track.recType == 1)
			{ // WAV
				if (track.trackName == "EEG1_WAV")
				{
					eeg1_waveform = track.waveform;
					eeg1_timestamps = track.waveformTimestamps;
				}
				else if (track.trackName == "EEG2_WAV")
				{
					eeg2_waveform = track.waveform;
					eeg2_timestamps = track.waveformTimestamps;
				}
			}
		}
		if (!eeg1_waveform.empty() || !eeg2_waveform.empty())
		{
			save_waveforms_to_csv("EEG_Waveforms.csv",
								  eeg1_timestamps, eeg1_waveform, "EEG1_WAV",
								  eeg2_timestamps, eeg2_waveform, "EEG2_WAV");
		}
	}
	catch (const std::exception &ex)
	{
		std::cerr << "Error parsing file: " << ex.what() << "\n";
		return 1;
	}

	return 0;
}
