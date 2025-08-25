import { useState, useEffect } from "react";
import "bootstrap/dist/css/bootstrap.min.css";
import "./App.css"; // Custom styles
import SpectrumAnalyzer from './SpectrumAnalyzer';
import WiFiScanner from './components/WiFiConfig/WiFiScanner';
import './components/WiFiConfig/WiFiConfig.css';
import AddStationForm from './components/stations/AddStationForm';


// FontAwesome Icon Imports
import { FaPlay, FaPause, FaWifi } from "react-icons/fa"; // Added FaWifi icon

export default function App() {
  const [stations, setStations] = useState<{ index: number; name: string }[]>([]);
  const [currentStation, setCurrentStation] = useState<string>("Loading...");
  const [selectedIndex, setSelectedIndex] = useState<number>(0);
  const [isPlaying, setIsPlaying] = useState<boolean>(false);
  const [showWiFiConfig, setShowWiFiConfig] = useState<boolean>(false);
  const [showAddStationForm, setShowAddStationForm] = useState<boolean>(false);
  const [wifiMode, setWifiMode] = useState<string | null>(null);

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

  useEffect(() => {
    fetch("/wifi-mode")
      .then(res => res.json())
      .then(data => setWifiMode(data.mode))
      .catch(() => setWifiMode(null));
  }, []);

  // Show only WiFi config if in AP or APSTA mode
  if (wifiMode === "ap" || wifiMode === "apsta") {
    return (
      <div className="modern-bg min-vh-100 d-flex align-items-center justify-content-center">
        <div className="card modern-card text-center p-4 mx-auto" style={{maxWidth: 500}}>
          <h2>WiFi Setup</h2>
          <WiFiScanner />
        </div>
      </div>
    );
  }

  return (
    <div className="modern-bg min-vh-100">
      <div className="container py-4">
        <div className="card modern-card text-center p-4 mx-auto" style={{maxWidth: 600}}>
          {/* Header with WiFi Button */}
          <div className="d-flex justify-content-end mb-3 sticky-top bg-transparent" style={{zIndex:2}}>
            <button 
              className="btn btn-link text-light" 
              onClick={() => setShowWiFiConfig(!showWiFiConfig)}
              title="WiFi Configuration"
            >
              <FaWifi size={24} />
            </button>
          </div>

          {showWiFiConfig ? (
            <WiFiScanner />
          ) : showAddStationForm ? (
            <>
              <div className="mb-3">
                <button
                  className="btn modern-btn play-pause-btn w-100"
                  style={{marginBottom: '10px'}}
                  onClick={() => setShowAddStationForm(false)}
                >
                  ← Back to Main
                </button>
              </div>
              <AddStationForm onSuccess={fetchStationList} />
            </>
          ) : (
            <>
              {/* Spectrum Analyzer */}
              <SpectrumAnalyzer />
              {/* Now Playing */}
              <div className="alert modern-alert">
                🎵 Now Playing: <strong>{currentStation}</strong>
              </div>
              {/* Edit Stations Button - moved above play button */}
              <div className="mb-3">
                <button
                  className="btn modern-btn play-pause-btn w-100"
                  style={{marginBottom: '10px'}}
                  onClick={() => setShowAddStationForm((v: boolean) => !v)}
                >
                  Edit Stations
                </button>
              </div>
              {/* Controls */}
              <div className="mb-3">
                <button className="btn modern-btn me-2 play-pause-btn" onClick={togglePlayPause}>
                  {isPlaying ? <FaPause size={24} /> : <FaPlay size={24} />}
                </button>
              </div>
              {/* Station Selection */}
              <div className="row g-2 overflow-auto" style={{maxHeight: '50vh'}}>
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
            </>
          )}
        </div>
      </div>
    </div>
  );
}
