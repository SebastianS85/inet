import { useState, useEffect } from 'react';
import { Alert, Button, Container } from 'react-bootstrap';
import NetworkList from './NetworkList';
import WiFiForm from './WiFiForm';

interface WifiNetwork {
  ssid: string;
  bssid: string;
  rssi: number;
  channel: number;
  authmode: number;
}

const WiFiScanner = () => {
  const [networks, setNetworks] = useState<WifiNetwork[]>([]);
  const [scanning, setScanning] = useState(false);
  const [message, setMessage] = useState('');
  const [messageType, setMessageType] = useState<'success' | 'danger'>('success');
  const [selectedSsid, setSelectedSsid] = useState('');

  const scanNetworks = async () => {
    if (scanning) return;
    
    setScanning(true);
    setMessage('');
    
    try {
      const response = await fetch('/wifi_scan');
      if (!response.ok) throw new Error('Network scan failed');
      
      const data = await response.json();
      setNetworks(data.networks || []);
    } catch (error) {
      setMessage('Failed to scan networks. Please try again.');
      setMessageType('danger');
    } finally {
      setScanning(false);
    }
  };

  useEffect(() => {
    scanNetworks();
  }, []);

  const handleNetworkSelect = (ssid: string) => {
    setSelectedSsid(ssid);
  };

  const handleSaveSuccess = () => {
    setMessage('WiFi credentials saved successfully!');
    setMessageType('success');
    setSelectedSsid('');
  };

  const handleSaveError = () => {
    setMessage('Failed to save WiFi credentials.');
    setMessageType('danger');
  };

  return (
    <Container className="mt-4">
      <h2>WiFi Configuration</h2>
      <Button 
        variant="primary" 
        onClick={scanNetworks} 
        disabled={scanning}
        className="mb-3"
      >
        {scanning ? 'Scanning...' : 'Scan for Networks'}
      </Button>

      {message && (
        <Alert variant={messageType} className="mb-3">
          {message}
        </Alert>
      )}

      {scanning ? (
        <Alert variant="info">Scanning for networks...</Alert>
      ) : (
        <>
          <NetworkList 
            networks={networks} 
            onSelectNetwork={handleNetworkSelect}
            selectedSsid={selectedSsid}
          />
          {selectedSsid && (
            <WiFiForm
              ssid={selectedSsid}
              onSaveSuccess={handleSaveSuccess}
              onSaveError={handleSaveError}
            />
          )}
        </>
      )}
    </Container>
  );
};

export default WiFiScanner;
