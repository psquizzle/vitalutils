#ifndef VITAL_LIB_H
#define VITAL_LIB_H

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <set>

/**
 * A small struct to hold all your track information. You can expand or rename as needed.
 */
struct TrackInfo
{
    std::uint16_t tid;
    std::string trackName;
    std::string deviceName;
    std::uint32_t deviceId;
    std::uint8_t recType;
    double dtStart;
    double dtEnd;
    float sampleRate;
    float minVal;
    float maxVal;
    std::uint64_t count;
    double sum;
    std::string firstVal;
    // For numeric data
    std::vector<float> numericValues;
    // For waveform data
    std::vector<float> waveform;
};

/**
 * The main container for all parsed vital file information.
 * This is what you'll get back when you parse a file.
 */
struct VitalFileData
{
    // Global or top-level info
    double tzBias;
    double dtStart;
    double dtEnd;

    // All tracks, keyed by track id
    std::map<std::uint16_t, TrackInfo> tracks;
};

/**
 * @brief Saves two waveforms to a CSV file with high precision.
 *
 * @param filename The CSV file to create.
 * @param waveform1 Vector of samples for first waveform.
 * @param name1 Column name for first waveform.
 * @param waveform2 Vector of samples for second waveform.
 * @param name2 Column name for second waveform.
 */
void save_waveforms_to_csv(const std::string &filename,
                           const std::vector<float> &waveform1, const std::string &name1,
                           const std::vector<float> &waveform2, const std::string &name2);

/**
 * @brief Whether a character is not printable ASCII (excluding tab, CR, LF).
 */
bool isNotPrintable(char c);

/**
 * @brief Parse a .vital file and return the metadata and track data.
 *
 * @param filename The path to the vital file (gzipped).
 * @param isShort If true, read only the short track list (less detail).
 * @return A VitalFileData struct with all relevant track/device info.
 *         On failure, you could throw or return an empty struct.
 */
VitalFileData parseVitalFile(const std::string &filename, bool isShort);

#endif // VITAL_LIB_H
