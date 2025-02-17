const VitalFile = require("./vitaldb"); // Ensure the correct path

const fs = require("fs");

async function displayVitalData(filepath) {
  try {
    const vital = new VitalFile(filepath);
    const data = await vital.load_vital(filepath);

    console.log("Devices:", data.devices);
    console.log("Tracks:", Object.keys(data.tracks));

    for (const [tid, track] of Object.entries(data.tracks)) {
      console.log(`Track ${track.name}: ${track.records.length} records`);
      console.log(track.records.slice(0, 5)); // Print first 5 records
    }
  } catch (err) {
    console.error("Error reading VITAL file:", err);
  }
}

// Example usage
const filePath = "0726.vital"; // Adjust to your file's path
displayVitalData(filePath);
