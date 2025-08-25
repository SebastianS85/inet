import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react-swc'

// https://vite.dev/config/
export default defineConfig({
  plugins: [react()],
  server: {
    proxy: {
      '/stations': 'http://radio.local',
      '/set-station': 'http://radio.local',
      '/start': 'http://radio.local',
      '/pause': 'http://radio.local',
      '/current-station': 'http://radio.local',
      '/wifi_scan': 'http://radio.local',
      '/save_wifi': 'http://radio.local',
    }
  }
})
