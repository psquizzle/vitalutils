/**
 * @author Eunsun Rachel Lee <eunsun.lee93@gmail.com>
 */

const path = require("path");
const fs = require("fs");
const zlib = require("zlib");

const montypes = {
  1: "ECG_WAV",
  2: "ECG_HR",
  3: "ECG_PVC",
  4: "IABP_WAV",
  5: "IABP_SBP",
  6: "IABP_DBP",
  7: "IABP_MBP",
  8: "PLETH_WAV",
  9: "PLETH_HR",
  10: "PLETH_SPO2",
  11: "RESP_WAV",
  12: "RESP_RR",
  13: "CO2_WAV",
  14: "CO2_RR",
  15: "CO2_CONC",
  16: "NIBP_SBP",
  17: "NIBP_DBP",
  18: "NIBP_MBP",
  19: "BT",
  20: "CVP_WAV",
  21: "CVP_CVP",
  22: "EEG_BIS",
  23: "TV",
  24: "MV",
  25: "PIP",
  26: "AGENT1_NAME",
  27: "AGENT1_CONC",
  28: "AGENT2_NAME",
  29: "AGENT2_CONC",
  30: "DRUG1_NAME",
  31: "DRUG1_CE",
  32: "DRUG2_NAME",
  33: "DRUG2_CE",
  34: "CO",
  36: "EEG_SEF",
  38: "PEEP",
  39: "ECG_ST",
  40: "AGENT3_NAME",
  41: "AGENT3_CONC",
  42: "STO2_L",
  43: "STO2_R",
  44: "EEG_WAV",
  45: "FLUID_RATE",
  46: "FLUID_TOTAL",
  47: "SVV",
  49: "DRUG3_NAME",
  50: "DRUG3_CE",
  52: "FILT1_1",
  53: "FILT1_2",
  54: "FILT2_1",
  55: "FILT2_2",
  56: "FILT3_1",
  57: "FILT3_2",
  58: "FILT4_1",
  59: "FILT4_2",
  60: "FILT5_1",
  61: "FILT5_2",
  62: "FILT6_1",
  63: "FILT6_2",
  64: "FILT7_1",
  65: "FILT7_2",
  66: "FILT8_1",
  67: "FILT8_2",
  70: "PSI",
  71: "PVI",
  72: "SPHB",
  73: "ORI",
  75: "ASKNA",
  76: "PAP_SBP",
  77: "PAP_MBP",
  78: "PAP_DBP",
  79: "FEM_SBP",
  80: "FEM_MBP",
  81: "FEM_DBP",
  82: "EEG_SEFL",
  83: "EEG_SEFR",
  84: "EEG_SR",
  85: "TOF_RATIO",
  86: "TOF_CNT",
  87: "SKNA_WAV",
  88: "ICP",
  89: "CPP",
  90: "ICP_WAV",
  91: "PAP_WAV",
  92: "FEM_WAV",
  93: "ALARM_LEVEL",
  95: "EEGL_WAV",
  96: "EEGR_WAV",
  97: "ANII",
  98: "ANIM",
  99: "PTC_CNT",
};

function VitalFile(
  filepath,
  track_names = [],
  track_names_only = false,
  exclude = []
) {
  this.devs = { 0: {} };
  this.trks = {};
  this.dtstart = 0;
  this.dtend = 0;
  this.dgmt = 0;
  this.filepath = filepath;

  if (path.extname(filepath) === ".vital") {
    this.load_vital(filepath, track_names, track_names_only, exclude);
  }
}

VitalFile.prototype.load_vital = function (
  filepath,
  track_names,
  track_names_only,
  exclude
) {
  return new Promise((resolve, reject) => {
    const rs = fs.createReadStream(filepath);
    const gunzip = zlib.createGunzip();
    const vfs = rs.pipe(gunzip);
    let header = false;
    let packet = null;
    let parsedData = { devices: {}, tracks: {} };

    vfs.on("data", (data) => {
      let pos = 0;
      if (!header) {
        header = true;
        if (data.toString("utf8", pos, pos + 4) !== "VITA") {
          return reject(new Error("Invalid VITAL file"));
        }
        pos += 4; // Skip signature
        pos += 4; // Skip format version
        pos += 2; // Skip header length
        this.dgmt = data.readIntLE(pos, 2);
        pos += 2;
        pos += 4; // Skip instance ID

        let prog_ver = "";
        for (let i = 0; i < 4; i++) {
          prog_ver += data.readUIntLE(pos, 1);
          pos += 1;
          if (i !== 3) prog_ver += ".";
        }
        parsedData.version = prog_ver;
      }

      if (packet) {
        data = Buffer.concat([packet, data]);
      }

      while (pos + 5 < data.length) {
        let packet_type = data.readIntLE(pos, 1);
        let packet_len = data.readUIntLE(pos + 1, 4);
        packet = data.slice(pos, pos + 5 + packet_len);
        if (data.length < pos + 5 + packet_len) return;
        pos += 5;

        if (packet_type === 9) {
          // Device info
          let did = packet.readUIntLE(5, 4);
          let devName = packet.toString("utf8", 9, 9 + packet.readUIntLE(5, 4));
          parsedData.devices[did] = { name: devName };
        } else if (packet_type === 0) {
          // Track info
          let tid = packet.readUIntLE(5, 2);
          let trackName = packet.toString(
            "utf8",
            7,
            7 + packet.readUIntLE(5, 4)
          );
          parsedData.tracks[tid] = { name: trackName, records: [] };
        } else if (packet_type === 1) {
          // Data record
          let tid = packet.readUIntLE(15, 2);
          let timestamp = packet.readDoubleLE(7);
          let value = packet.readFloatLE(17);

          if (parsedData.tracks[tid]) {
            parsedData.tracks[tid].records.push({
              time: timestamp,
              value: value,
            });
          }
        }
        pos += packet_len;
      }
    });

    vfs.on("end", () => resolve(parsedData));
    vfs.on("error", (err) => reject(err));
  });
};

module.exports = VitalFile;
