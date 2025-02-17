#include "VitalLib.h"
#include "GZReader.h" // Your custom GZ reader (as in your original code)
#include "Util.h"     // If you have string_format, escape_csv, etc. in here
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <stdexcept> // For throw, if you choose to throw on parse errors

// Example of the library function to save waveforms
void save_waveforms_to_csv(const std::string &filename,
                           const std::vector<float> &waveform1, const std::string &name1,
                           const std::vector<float> &waveform2, const std::string &name2)
{
    size_t max_samples = std::max(waveform1.size(), waveform2.size());

    std::ofstream csv_file(filename);
    if (!csv_file.is_open())
    {
        std::cerr << "Failed to open CSV file: " << filename << std::endl;
        return;
    }

    csv_file << std::fixed << std::setprecision(6);
    csv_file << "Sample_Index," << name1 << "," << name2 << "\n";

    for (size_t i = 0; i < max_samples; i++)
    {
        csv_file << i << ",";
        if (i < waveform1.size())
            csv_file << waveform1[i];
        csv_file << ",";
        if (i < waveform2.size())
            csv_file << waveform2[i];
        csv_file << "\n";
    }

    csv_file.close();
    std::cout << "Saved waveforms to " << filename
              << " (" << max_samples << " samples)\n";
}

// Example function to check if a character is not printable
bool isNotPrintable(char c)
{
    return !((c >= 32 && c < 127) || c == 10 || c == 13 || c == 9);
}

// Helper function (private to this .cpp) to handle reading from GZReader with sanity checks.
// You can adapt or rename as needed.
static bool readVitalHeader(GZReader &gz, double &tzBias, double &dtStart, double &dtEnd)
{
    // 1) Read "VITA"
    char sign[4];
    if (!gz.read(sign, 4))
        return false;
    if (strncmp(sign, "VITA", 4) != 0)
    {
        std::cerr << "Not a valid vital file.\n";
        return false;
    }

    // 2) Read format_ver (4 bytes)
    std::uint32_t format_ver = 0;
    if (!gz.read(&format_ver, 4))
        return false;

    // 3) Read headerlen (2 bytes)
    std::uint16_t headerLen = 0;
    if (!gz.read(&headerLen, 2))
        return false;

    // The total header is 10 + headerLen. We've read 10 so far,
    // so we have `headerLen` more bytes to read for the entire header.

    // 4) If the file is "standard" Vital version 3, headerLen is usually 26.
    // Let's allocate a buffer to read those 26 (or however many) bytes:
    if (headerLen > 0)
    {
        std::vector<unsigned char> hdrBuf(headerLen);
        if (!gz.read(hdrBuf.data(), headerLen))
        {
            std::cerr << "Failed to read the remaining header bytes.\n";
            return false;
        }

        // If it's exactly 26, parse it as:
        //  0..1   : tzbias (2 bytes)
        //  2..5   : inst_id (4 bytes)
        //  6..9   : prog_ver (4 bytes)
        //  10..17 : dtstart (double, 8 bytes)
        //  18..25 : dtend   (double, 8 bytes)
        // If there's more or less, adjust accordingly, or skip older/unknown fields gracefully.

        if (headerLen >= 2)
        {
            std::int16_t dgmt = *(reinterpret_cast<const std::int16_t *>(&hdrBuf[0]));
            tzBias = dgmt / 60.0;
        }
        if (headerLen >= 26)
        {
            dtStart = *(reinterpret_cast<const double *>(&hdrBuf[10]));
            dtEnd = *(reinterpret_cast<const double *>(&hdrBuf[18]));
        }
        // If you want inst_id, prog_ver, etc., parse them similarly from the buffer.
    }

    // Now we are perfectly aligned with the first packet of the body.
    // If we wanted to handle older or future formats, we could look at format_ver or headerLen
    // to decide how to parse the buffer or skip fields.

    return true; // success
}

// The main function that actually parses a .vital file
VitalFileData parseVitalFile(const std::string &filename, bool isShort)
{
    VitalFileData result;
    result.tzBias = 0.0;
    result.dtStart = 0.0;
    result.dtEnd = 0.0;

    GZReader gz(filename.c_str());
    if (!gz.opened())
    {
        throw std::runtime_error("File does not exist: " + filename);
    }

    // 1) Read header

    if (!readVitalHeader(gz, result.tzBias, result.dtStart, result.dtEnd))
    {
        throw std::runtime_error("Invalid vital file header: " + filename);
    }

    // Maps for device id → device name
    std::map<std::uint32_t, std::string> did_dnames;

    while (!gz.eof())
    {
        std::uint8_t type = 0;
        if (!gz.read(&type, 1))
            break;

        std::uint32_t datalen = 0;
        if (!gz.read(&datalen, 4))
            break;
        if (datalen > 1000000)
        { // sanity check
            std::cerr << "Suspiciously large datalen, abort parse.\n";
            break;
        }

        // type = 0 => track info
        // type = 1 => record
        // type = 9 => device info
        if (type == 9)
        { // devinfo
            std::uint32_t did = 0;
            if (!gz.fetch(did, datalen))
                goto skipPacket;

            std::string dtype;
            if (!gz.fetch_with_len(dtype, datalen))
                goto skipPacket;
            std::string dname;
            if (!gz.fetch_with_len(dname, datalen))
                goto skipPacket;
            if (dname.empty())
                dname = dtype;

            did_dnames[did] = dname;
        }
        else if (type == 0)
        { // track info
            std::uint16_t tid = 0;
            if (!gz.fetch(tid, datalen))
                goto skipPacket;
            std::uint8_t rectype = 0;
            if (!gz.fetch(rectype, datalen))
                goto skipPacket;
            std::uint8_t recfmt = 0;
            if (!gz.fetch(recfmt, datalen))
                goto skipPacket;

            std::string tname;
            if (!gz.fetch_with_len(tname, datalen))
                goto skipPacket;

            // Some optional fields
            std::string unit;
            float minval = 0.f, maxval = 0.f, srate = 0.f;
            double adc_gain = 1.0, adc_offset = 0.0;
            std::uint8_t montype = 0;
            std::uint32_t did = 0;

            if (!gz.fetch_with_len(unit, datalen))
                goto trackInfoDone;
            if (!gz.fetch(minval, datalen))
                goto trackInfoDone;
            if (!gz.fetch(maxval, datalen))
                goto trackInfoDone;
            std::uint32_t col;
            if (!gz.fetch(col, datalen))
                goto trackInfoDone;
            if (!gz.fetch(srate, datalen))
                goto trackInfoDone;
            if (!gz.fetch(adc_gain, datalen))
                goto trackInfoDone;
            if (!gz.fetch(adc_offset, datalen))
                goto trackInfoDone;
            if (!gz.fetch(montype, datalen))
                goto trackInfoDone;
            if (!gz.fetch(did, datalen))
                goto trackInfoDone;

        trackInfoDone:
            // Insert into result
            TrackInfo &tr = result.tracks[tid];
            tr.tid = tid;
            tr.trackName = tname;
            tr.deviceId = did;
            tr.deviceName = (did_dnames.count(did) ? did_dnames[did] : "");
            tr.recType = rectype;
            tr.sampleRate = srate;
            // minval, maxval, etc. can be stored if you wish
        }
        else if (type == 1)
        { // record
            std::uint16_t infolen = 0;
            if (!gz.fetch(infolen, datalen))
                goto skipPacket;

            double dt = 0.0;
            if (!gz.fetch(dt, datalen))
                goto skipPacket;

            std::uint16_t tid = 0;
            if (!gz.fetch(tid, datalen))
                goto skipPacket;

            // Update global dtStart/dtEnd
            if (result.dtStart == 0.0)
                result.dtStart = dt;
            else if (result.dtStart > dt)
                result.dtStart = dt;
            if (result.dtEnd < dt)
                result.dtEnd = dt;

            // If user only wants short list, skip
            if (isShort)
            {
                goto skipPacket;
            }

            // Otherwise, fetch data
            if (result.tracks.count(tid) == 0)
            {
                // We never had track info for this tid— skip or handle error
                goto skipPacket;
            }
            TrackInfo &track = result.tracks[tid];

            if (track.recType == 2)
            { // numeric
                float fval = 0.f;
                if (!gz.fetch(fval, datalen))
                    goto skipPacket;

                track.numericValues.push_back(fval);

                // Update stats
                if (track.count == 0)
                {
                    track.minVal = track.maxVal = fval;
                }
                else
                {
                    if (fval < track.minVal)
                        track.minVal = fval;
                    if (fval > track.maxVal)
                        track.maxVal = fval;
                }
                track.count++;
                track.sum += fval;

                if (track.firstVal.empty())
                {
                    track.firstVal = string_format("%f", fval);
                }
            }
            else if (track.recType == 5)
            { // string
                // skip 4 bytes
                if (!gz.skip(4, datalen))
                    goto skipPacket;
                std::string sval;
                if (!gz.fetch_with_len(sval, datalen))
                    goto skipPacket;
                sval.erase(std::remove_if(sval.begin(), sval.end(), isNotPrintable), sval.end());

                if (track.firstVal.empty())
                    track.firstVal = sval;
                else
                    track.firstVal += " | " + sval;
            }
            else if (track.recType == 1)
            { // WAV
                // For waveform data
                // in your original code: num_samples = datalen / sizeof(float)
                std::uint32_t num_samples = datalen / sizeof(float);
                for (std::uint32_t i = 0; i < num_samples; i++)
                {
                    float sample;
                    if (!gz.fetch(sample, datalen))
                        goto skipPacket;
                    track.waveform.push_back(sample);
                }
            }
        }

    skipPacket:
        // skip the rest of this packet if any left
        if (!gz.skip(datalen))
            break;
    }

    // Return the entire dataset
    return result;
}
