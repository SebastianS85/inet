import { useState, useEffect } from "react";
import "bootstrap/dist/css/bootstrap.min.css";
import "./App.css"; // Custom styles
import SpectrumAnalyzer from './SpectrumAnalyzer';

// FontAwesome Icon Imports
import { FaPlay, FaPause } from "react-icons/fa"; // Importing Play/Pause icons

const stations: string[] = [
  "KISS FM", "Radio Park", "Greek Ethink", "Spanisch Cocktail", "Mykonos Scorpions",
  "Greek Taverna", "Bar House", "Chillout", "Greek Emporika", "Greek vs Ethnik",
  "Sunshine Live", "90s", "Fiesta Mexico", "RMF FM", "Eska",
  "Radio 357", "Muzyczne Radio", "Radio Zet", "Antyradio", "Cinemix",
  "timeDanceFM", "Radio 857", "Live House", "Playa Radio", "Sidewinder", "Isekoi Radio",
  "pirat fm"
];

export default function App() {
  const [currentStation, setCurrentStation] = useState<string>("Loading...");
  const [selectedIndex, setSelectedIndex] = useState<number>(0);
  const [isPlaying, setIsPlaying] = useState<boolean>(false);

  useEffect(() => {
    fetchCurrentStation();
    
  }, []);

  

  const fetchCurrentStation = async () => {
    try {
      const response = await fetch("/current-station");
      if (!response.ok) throw new Error("Failed to fetch station");
      const stationName = await response.text();
      setCurrentStation(stations[Number(stationName)]);
      setSelectedIndex(Number(stationName));
    } catch (error) {
      console.error("Error fetching station:", error);
      setCurrentStation("Unknown Station");
    }
  };

  const changeStation = async (index: number) => {
    try {
      await fetch('/set-station', {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ index })
      });
      setSelectedIndex(index);
      setCurrentStation(stations[index]);
    } catch (error) {
      console.error("Error changing station:", error);
    }
  };

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
          {stations.map((station, index) => (
            <div className="col-4 d-flex" key={index}>
              <button
                className={`btn modern-btn w-100 ${index === selectedIndex ? "selected" : ""}`}
                onClick={() => changeStation(index)}
              >
                {station}
              </button>
            </div>
          ))}
        </div>

       
        
      </div>
    </div>
  );
}
