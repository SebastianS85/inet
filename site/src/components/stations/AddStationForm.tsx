import React, { useState, useEffect } from "react";
import '../../App.css'; // Ensure modern-btn and modern-card styles are available

interface Station {
  index: number;
  name: string;
}

interface AddStationFormProps {
  onSuccess?: () => void; // Optional callback to refresh station list
}

const AddStationForm: React.FC<AddStationFormProps> = ({ onSuccess }) => {
  const [name, setName] = useState("");
  const [url, setUrl] = useState("");
  const [genre, setGenre] = useState("");
  const [error, setError] = useState("");
  const [success, setSuccess] = useState("");
  const [loading, setLoading] = useState(false);
  const [stations, setStations] = useState<Station[]>([]);
  const [deleting, setDeleting] = useState<number | null>(null);

  // Fetch stations for the list
  const fetchStations = async () => {
    try {
      const response = await fetch("/stations");
      if (!response.ok) throw new Error("Failed to fetch stations");
      const data = await response.json();
      setStations(data);
    } catch (err) {
      setStations([]);
    }
  };

  useEffect(() => {
    fetchStations();
  }, []);

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    setError("");
    setSuccess("");
    if (!name || !url) {
      setError("Name and URL are required.");
      return;
    }
    setLoading(true);
    try {
      const response = await fetch("/add-station", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ name, url, genre })
      });
      if (!response.ok) throw new Error("Failed to add station");
      setSuccess("Station added successfully!");
      setName("");
      setUrl("");
      setGenre("");
      fetchStations();
      if (onSuccess) onSuccess();
    } catch (err) {
      setError("Failed to add station. Please try again.");
    } finally {
      setLoading(false);
    }
  };

  const handleDelete = async (index: number) => {
    setDeleting(index);
    setError("");
    setSuccess("");
    try {
      const response = await fetch("/delete-station", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ index })
      });
      if (!response.ok) throw new Error("Failed to delete station");
      setSuccess("Station deleted successfully!");
      fetchStations();
      if (onSuccess) onSuccess();
    } catch (err) {
      setError("Failed to delete station. Please try again.");
    } finally {
      setDeleting(null);
    }
  };

  return (
    <div className="modern-card p-3" style={{maxWidth: 600, width: '100%', margin: '0 auto', background: '#23272b', borderRadius: 16, boxShadow: '0 4px 16px rgba(0,0,0,0.3)'}}>
      <form onSubmit={handleSubmit} className="mb-3">
        <div className="mb-2">
          <input
            type="text"
            className="form-control"
            placeholder="Station Name"
            value={name}
            onChange={e => setName(e.target.value)}
            disabled={loading}
          />
        </div>
        <div className="mb-2">
          <input
            type="text"
            className="form-control"
            placeholder="Stream URL"
            value={url}
            onChange={e => setUrl(e.target.value)}
            disabled={loading}
          />
        </div>
        <div className="mb-2">
          <input
            type="text"
            className="form-control"
            placeholder="Genre (optional)"
            value={genre}
            onChange={e => setGenre(e.target.value)}
            disabled={loading}
          />
        </div>
        {error && <div className="text-danger mb-2">{error}</div>}
        {success && <div className="text-success mb-2">{success}</div>}
        <button type="submit" className="btn modern-btn w-100" disabled={loading} style={{borderRadius: 12}}>
          {loading ? "Adding..." : "Add Station"}
        </button>
      </form>
      <h5 className="mt-4 mb-2 text-center">Station List</h5>
      <div style={{display: 'flex', justifyContent: 'center'}}>
        <ul className="list-group w-100" style={{background: 'rgba(43,51,56,0.95)', borderRadius: 12, boxShadow: '0 2px 8px rgba(0,0,0,0.2)', maxWidth: 500, width: '100%', padding: 0}}>
          {stations.map(station => (
            <li className="list-group-item d-flex align-items-center" key={station.index} style={{background: 'transparent', color: 'white', border: 'none', borderBottom: '1px solid #333', minHeight: 56, padding: '0 0.5rem'}}>
              <span style={{flex: 1, textAlign: 'left', paddingLeft: 12}}>{station.name}</span>
              <button
                className="btn modern-btn btn-sm ms-2"
                style={{background: '#b94a48', color: 'white', borderRadius: 8, minWidth: 80, margin: '8px 0'}}
                onClick={() => handleDelete(station.index)}
                disabled={deleting === station.index}
                title="Delete station"
              >
                {deleting === station.index ? "Deleting..." : "Delete"}
              </button>
            </li>
          ))}
          {stations.length === 0 && <li className="list-group-item text-center" style={{background: 'transparent', color: '#aaa'}}>No stations found.</li>}
        </ul>
      </div>
    </div>
  );
};

export default AddStationForm;
