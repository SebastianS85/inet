import { ListGroup } from 'react-bootstrap';

interface Network {
  ssid: string;
  bssid: string;
  rssi: number;
  channel: number;
  authmode: number;
}

interface NetworkListProps {
  networks: Network[];
  onSelectNetwork: (ssid: string) => void;
  selectedSsid: string;
}

const NetworkList = ({ networks, onSelectNetwork, selectedSsid }: NetworkListProps) => {
  const getSignalStrength = (rssi: number) => {
    // Convert RSSI to percentage (typical RSSI range is -100 to 0)
    return Math.min(100, Math.max(0, (rssi + 100) * 2));
  };

  const getAuthMode = (mode: number) => {
    const modes: { [key: number]: string } = {
      0: 'Open',
      1: 'WEP',
      2: 'WPA-PSK',
      3: 'WPA2-PSK',
      4: 'WPA/WPA2-PSK',
      5: 'WPA2-ENTERPRISE',
      6: 'WPA3-PSK',
      7: 'WPA2/WPA3-PSK'
    };
    return modes[mode] || 'Unknown';
  };

  return (
    <ListGroup className="mb-3">
      {networks.length === 0 ? (
        <ListGroup.Item>No networks found</ListGroup.Item>
      ) : (
        networks.sort((a, b) => b.rssi - a.rssi).map((network) => (
          <ListGroup.Item
            key={network.bssid}
            action
            active={network.ssid === selectedSsid}
            onClick={() => onSelectNetwork(network.ssid)}
          >
            <div className="d-flex justify-content-between align-items-center">
              <div>
                <h6 className="mb-0">{network.ssid}</h6>
                <small className="text-muted">
                  Signal: {getSignalStrength(network.rssi)}% | 
                  Channel: {network.channel} | 
                  Security: {getAuthMode(network.authmode)}
                </small>
              </div>
              <div className="signal-strength">
                {/* You could add signal strength icons here */}
              </div>
            </div>
          </ListGroup.Item>
        ))
      )}
    </ListGroup>
  );
};

export default NetworkList;
