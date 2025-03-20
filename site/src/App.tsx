import { useState, useEffect } from "react";
import "bootstrap/dist/css/bootstrap.min.css";
import "./App.css"; // Custom styles
import SpectrumAnalyzer from './SpectrumAnalyzer';

// FontAwesome Icon Imports
import { FaPlay, FaPause } from "react-icons/fa"; // Importing Play/Pause icons

export default function App() {
  const [stations, setStations] = useState<{ index: number; name: string }[]>([]);
  const [currentStation, setCurrentStation] = useState<string>("Loading...");
  const [selectedIndex, setSelectedIndex] = useState<number>(0);
  const [isPlaying, setIsPlaying] = useState<boolean>(false);



  useEffect(() => {
    if (stations.length > 0) {
      fetchCurrentStation();
    }
  }, [stations]);  // Trigger when stations are populated

  // Fetch the current station from the backend
  const fetchCurrentStation = async () => {
    try {
      const response = await fetch("/current-station");
      if (!response.ok) throw new Error("Failed to fetch current station");
      const stationIndex = await response.text();
      setCurrentStation(stations[Number(stationIndex)]?.name || "Unknown Station");
      setSelectedIndex(Number(stationIndex));
    } catch (error) {
      console.error("Error fetching current station:", error);
      setCurrentStation("Unknown Station");
    }
  };
  // Fetch the station list when the component mounts
  useEffect(() => {
    fetchStationList();
  }, []);

  // Fetch the list of stations from the backend
  const fetchStationList = async () => {
    try {
      const response = await fetch("/stations");
      if (!response.ok) throw new Error("Failed to fetch station list");
      const data = await response.json();
      setStations(data);  // Set the stations state to the fetched data
    } catch (error) {
      console.error("Error fetching station list:", error);
    }
  };

  // Fetch the current station from the backend only when stations are fetched
 

  // Change the station by sending the index to the backend
  const changeStation = async (index: number) => {
    try {
      await fetch('/set-station', {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ index })
      });
      setSelectedIndex(index);
      setCurrentStation(stations[index]?.name || "Unknown Station");
    } catch (error) {
      console.error("Error changing station:", error);
    }
  };

  // Toggle between play and pause states
  const togglePlayPause = async () => {
    const action = isPlaying ? "/pause" : "/start";
    try {
      await fetch(action);
      setIsPlaying(!isPlaying);
    } catch (error) {
      console.error("Error toggling play/pause:", error);
    }
  };

  return (
    <div className="d-flex justify-content-center align-items-center vh-100 modern-bg">
      <div className="card modern-card text-center p-4">
        {/* Spectrum Analyzer */}
        <SpectrumAnalyzer />

        {/* Now Playing */}
        <div className="alert modern-alert">
          🎵 Now Playing: <strong>{currentStation}</strong>
        </div>

        {/* Controls */}
        <div className="mb-3">
          <button className="btn modern-btn me-2 play-pause-btn" onClick={togglePlayPause}>
            {isPlaying ? <FaPause size={24} /> : <FaPlay size={24} />}
          </button>
        </div>

        {/* Station Selection */}
        <div className="row g-2">
          {stations.map((station) => (
            <div className="col-4 d-flex" key={station.index}>
              <button
                className={`btn modern-btn w-100 ${station.index === selectedIndex ? "selected" : ""}`}
                onClick={() => changeStation(station.index)}
              >
                {station.name}
              </button>
            </div>
          ))}
        </div>
      </div>
    </div>
  );
}
