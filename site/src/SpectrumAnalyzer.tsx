import { useState, useEffect } from 'react';
import './App.css';

const SpectrumAnalyzer = () => {
  const [bars, setBars] = useState(Array(10).fill(0)); // 10 bars initially at height 0

  useEffect(() => {
    const interval = setInterval(() => {
      // Simulating the "dancing" of the spectrum by randomizing the heights
      setBars(bars => bars.map(() => Math.random() * 100)); // Randomize heights between 0 and 100
    }, 100); // Update every 100ms to make the effect smooth

    // Cleanup interval when component unmounts
    return () => clearInterval(interval);
  }, []);

  return (
    <div className="spectrum-container">
      {bars.map((height, index) => (
        <div
          key={index}
          className="spectrum-bar"
          style={{ height: `${height}%` }}
        />
      ))}
    </div>
  );
};

export default SpectrumAnalyzer;
