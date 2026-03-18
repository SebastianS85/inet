# OPIS PROGRAMU inetRadio (ESP32)

Ten dokument opisuje, co aktualnie robi firmware w projekcie inetRadio.

## 1. Cel programu

Program uruchamia internetowe radio na ESP32:
- odtwarza stacje z listy zapisanej w pliku,
- udostepnia API HTTP do sterowania (start, pause, zmiana stacji, zarzadzanie lista),
- obsluguje konfiguracje Wi-Fi,
- pokazuje status na OLED,
- sam probuje odzyskac odtwarzanie po problemach ze strumieniem.

## 2. Start systemu (glowny przeplyw)

Punkt startowy to app_main w main/main.c.

Kroki startu:
1. Inicjalizacja I2C i OLED.
2. Inicjalizacja NVS.
3. Montowanie storage pod /store.
4. Odczyt zapisanych danych Wi-Fi z NVS.
5. Decyzja o trybie pracy:
   - jesli sa poprawne dane Wi-Fi: startuje tryb STA i laczenie z routerem,
   - jesli brak danych: startuje SoftAP do konfiguracji.

Po poprawnym polaczeniu w trybie STA:
1. Startuje serwer HTTP.
2. Ladowana jest lista stacji z /store/stations.txt.
3. Inicjalizowany i uruchamiany jest pipeline audio.
4. Startuje task stream_task (obsluga eventow i watchdog audio).
5. Startuje task rssi_display_task (co 2s aktualizuje dolna linie OLED).

## 3. Tryby Wi-Fi

### 3.1 Tryb STA

W trybie STA urzadzenie laczy sie do zapisanej sieci i dziala jako radio + serwer API.

Dodatkowo:
- wywolywane jest print_ip_address(...),
- na OLED pokazywane sa informacje o polaczeniu,
- wlaczony jest odczyt RSSI na OLED.

### 3.2 Tryb SoftAP (konfiguracja)

Gdy brak danych Wi-Fi:
- urzadzenie uruchamia AP (SSID i haslo z common_wifi.h),
- serwer HTTP udostepnia strone konfiguracji,
- endpointy /wifi_scan i /save_wifi pozwalaja wybrac siec i zapisac dane,
- po zapisie danych urzadzenie wykonuje restart.

## 4. Audio: pipeline i odtwarzanie

Pipeline audio jest zbudowany jako:
http -> mp3 -> i2s

Elementy i funkcje:
- http_stream_reader: pobieranie danych z URL stacji,
- mp3_decoder: dekodowanie MP3,
- i2s_stream_writer: wyjscie audio do kodeka przez I2S.

Na starcie:
1. Rejestrowane i linkowane sa elementy pipeline.
2. Ustawiany jest URI aktualnej stacji.
3. Odpalany jest listener eventow.
4. Pipeline startuje.

Podczas pracy:
- po AEL_MSG_CMD_REPORT_MUSIC_INFO ustawiany jest zegar I2S (sample rate, bits, channels),
- zmiana stacji przestawia URI i wykonuje restart pipeline z resetem stanow.

## 5. Watchdog i odzyskiwanie audio

Kod audio ma mechanizmy auto-recovery, zeby samemu wracac po problemach streamu.

Wykrywane przypadki:
1. Elementy weszly w ERROR.
2. Elementy zeszly do STOPPED/FINISHED nieoczekiwanie.
3. Brak aktywnosci HTTP przez okreslony czas (http idle timeout).
4. Pusty bufor wejsciowy I2S przez okreslony czas.

Przed restartem logowana jest diagnostyka:
- reason i suspect,
- stany http/mp3/i2s,
- czasy bez aktywnosci HTTP/PCM,
- zapelnienie i rozmiar bufora I2S,
- index stacji.

Restart jest zabezpieczony:
- cooldown miedzy restartami,
- startup grace period po starcie pipeline,
- fallback stop/terminate z timeoutami,
- czyszczenie kolejki eventow po restarcie.

## 6. API HTTP (serwer sterowania)

Serwer rejestruje m.in. endpointy:
- GET /stations
- POST /set-station
- GET /current-station
- GET /start
- GET /pause
- POST /add-station
- POST /delete-station
- GET /rssi
- GET /wifi-mode
- GET /wifi_scan
- POST /save_wifi
- GET /* (serwowanie plikow UI ze storage)

Uwagi:
- API jest wykorzystywane przez frontend web/mobile.
- Jest filtr ochronny przy niskiej ilosci wolnego heap.

## 7. Pliki danych i storage

Glowne pliki w /store:
- stations.txt: lista stacji,
- pliki frontendu (index.html, js, css, assets) serwowane przez endpoint domyslny.

NVS przechowuje m.in.:
- SSID i haslo Wi-Fi,
- index ostatnio wybranej stacji.

## 8. OLED i UI urzadzenia

OLED pokazuje m.in.:
- status laczenia Wi-Fi,
- IP lub informacje o trybie konfiguracji,
- nazwe aktualnej stacji,
- dolna linie z sila sygnalu Wi-Fi (ikona + usredniony RSSI).

## 9. Dodatkowe zachowania

- Przycisk na GPIO38 (ISR + timer debounce) usuwa dane Wi-Fi z NVS i restartuje urzadzenie.
- Program obsluguje dodawanie/usuwanie stacji do pliku stations.txt w trakcie pracy.

## 10. Podsumowanie praktyczne

W praktyce firmware robi trzy rzeczy jednoczesnie:
1. Utrzymuje polaczenie i konfiguracje sieci.
2. Odtwarza strumien internetowego radia przez pipeline ADF.
3. Udostepnia API + interfejs web do zdalnego sterowania.

Jesli stream "zawiesi sie" logicznie (bez twardego bledu), watchdog powinien wykryc problem i wykonac kontrolowany restart pipeline.
