# Audio.c - proste wyjasnienie funkcji i watchdogow

Ten opis tlumaczy dzialanie [components/audio/audio.c](components/audio/audio.c) krok po kroku, tak zeby dalo sie to czytac jak instrukcje.

## Co to jest watchdog w tym pliku

Tutaj watchdog to nie osobny hardware timer. To logika programowa, ktora cyklicznie sprawdza, czy stream audio nadal zyje.

Glowna funkcja podejmujaca decyzje:

- [stream_watchdog_restart_reason](components/audio/audio.c#L709)

Jesli wykryje problem, zwraca tekst z powodem restartu. Jesli wszystko jest ok, zwraca NULL.

## Szybka mapa funkcji

- inicjalizacja i start: [audio_init](components/audio/audio.c#L779), [audio_start](components/audio/audio.c#L834)
- petla eventow audio: [stream_task](components/audio/audio.c#L966)
- wykrywanie problemow: [stream_watchdog_restart_reason](components/audio/audio.c#L709)
- zlecanie restartu: [restart_current_stream](components/audio/audio.c#L680)
- wykonywanie restartu: [audio_restart_task](components/audio/audio.c#L598)
- twarda procedura restartu pipeline: [restart_stream_for_station_locked](components/audio/audio.c#L519)
- zmiana stacji z kolejka: [change_radio_station](components/audio/audio.c#L889)
- diagnostyka przed restartem: [log_stream_diagnostics](components/audio/audio.c#L452)

## Najwazniejsze stale czasowe

- [STREAM_EVENT_WAIT_MS](components/audio/audio.c#L57): 1000 ms
- [STREAM_RESTART_COOLDOWN_MS](components/audio/audio.c#L58): 3000 ms
- [STREAM_EMPTY_BUFFER_TIMEOUT_MS](components/audio/audio.c#L59): 5000 ms
- [STREAM_HTTP_IDLE_TIMEOUT_MS](components/audio/audio.c#L60): 15000 ms
- [STREAM_STARTUP_GRACE_MS](components/audio/audio.c#L61): 8000 ms

Znaczenie praktyczne:

- cooldown nie pozwala restartowac co chwile
- grace period nie reaguje nerwowo tuz po starcie
- osobne timeouty rozrozniaja problem sieci od problemu z danymi PCM

## 1) Funkcja stream_task - serce petli audio

Lokalizacja: [stream_task](components/audio/audio.c#L966)

Co robi:

1. czeka na event z ADF
2. timeout traktuje jako normalny cykl (nie awarie)
3. czyta statusy HTTP/MP3/I2S
4. odpala watchdog check i ewentualnie restart

Przyklad fragmentu kodu:

	esp_err_t ret = audio_event_iface_listen(evt, &msg,
											 pdMS_TO_TICKS(STREAM_EVENT_WAIT_MS));

	if (ret == ESP_ERR_TIMEOUT || ret == ESP_FAIL)
	{
		const char *watchdog_reason = stream_watchdog_restart_reason();
		if (watchdog_reason)
			restart_current_stream(watchdog_reason);
		continue;
	}

Jak to rozumiec:

- brak eventu przez 1 sekunde to normalne
- wtedy robi sie tylko kontrola zdrowia streamu

## 2) Funkcja stream_watchdog_restart_reason - decyzja czy restartowac

Lokalizacja: [stream_watchdog_restart_reason](components/audio/audio.c#L709)

Co sprawdza po kolei:

1. czy restart w ogole jest dozwolony
2. czy nie trwa grace period
3. czy elementy nie sa w ERROR/STOPPED/FINISHED
4. czy HTTP nie jest zbyt dlugo bez aktywnosci
5. czy bufor wejsciowy I2S nie jest pusty za dlugo

Przyklad fragmentu kodu:

	if (audio_user_paused || restart_cooldown_active() || s_restart_in_progress)
		return NULL;

	if (http_state == AEL_STATE_ERROR || mp3_state == AEL_STATE_ERROR ||
		i2s_state == AEL_STATE_ERROR)
	{
		return "watchdog: element entered error state";
	}

	if ((now - last_i2s_data_tick) >= pdMS_TO_TICKS(STREAM_EMPTY_BUFFER_TIMEOUT_MS))
	{
		return "watchdog: i2s input ringbuffer empty timeout";
	}

Jak to rozumiec:

- watchdog zwraca konkretny powod, zeby logi mowily co wykryto

## 3) Funkcja restart_current_stream - zlecenie restartu

Lokalizacja: [restart_current_stream](components/audio/audio.c#L680)

Co robi:

1. nie restartuje od razu
2. tworzy request restartu typu watchdog
3. wrzuca request do kolejki s_restart_queue

Przyklad fragmentu kodu:

	restart_req_t req = {
		.type = RESTART_REQ_WATCHDOG,
		.station_index = current_station_index,
	};
	strncpy(req.reason, reason, sizeof(req.reason) - 1);
	req.reason[sizeof(req.reason) - 1] = '\0';

	return (xQueueOverwrite(s_restart_queue, &req) == pdTRUE) ? ESP_OK : ESP_FAIL;

Jak to rozumiec:

- xQueueOverwrite przy kolejce 1-elementowej zostawia zawsze ostatnie zlecenie
- nie ma lawiny zaleglych restartow

## 4) Funkcja change_radio_station - kolejka zmian stacji

Lokalizacja: [change_radio_station](components/audio/audio.c#L889)

Co robi teraz:

1. waliduje index stacji
2. tworzy request typu RESTART_REQ_STATION_CHANGE
3. wrzuca go przez xQueueOverwrite
4. od razu odswieza nazwe na ekranie

Przyklad fragmentu kodu:

	restart_req_t req = {
		.type = RESTART_REQ_STATION_CHANGE,
		.station_index = station_index,
	};
	strncpy(req.reason, "station change", sizeof(req.reason) - 1);
	req.reason[sizeof(req.reason) - 1] = '\0';

	if (xQueueOverwrite(s_restart_queue, &req) != pdTRUE)
	{
		ESP_LOGW(TAG, "Failed to enqueue station change to index %d", station_index);
		return;
	}

Jak to rozumiec:

- szybkie klikanie stacji nie odpala 20 restartow
- wykonuje sie ostatnia zmiana, ktora wygrala wyscig

## 5) Funkcja audio_restart_task - jedyny wykonawca restartu

Lokalizacja: [audio_restart_task](components/audio/audio.c#L598)

Co robi:

1. czeka na request z kolejki
2. oznacza restart in progress
3. scala burst zmian stacji do ostatniej
4. bierze mutex i odpala restart
5. na koncu czysci flage restartu

Przyklad fragmentu kodu:

	if (req.type == RESTART_REQ_STATION_CHANGE)
	{
		restart_req_t queued = {0};
		while (xQueueReceive(s_restart_queue, &queued, 0) == pdTRUE)
		{
			if (queued.type == RESTART_REQ_STATION_CHANGE)
				req = queued;
		}
	}

Jak to rozumiec:

- to jest anty-spam dla zmian stacji
- worker wykonuje restart sekwencyjnie, bez nakladania stop/run

## 6) Funkcja restart_stream_for_station_locked - procedura stop/reset/run

Lokalizacja: [restart_stream_for_station_locked](components/audio/audio.c#L519)

To najwazniejsza funkcja techniczna. Tu powstaja logi z timeoutami.

Przyklad fragmentu kodu:

	audio_pipeline_stop(pipeline);
	esp_err_t stop_ret = audio_pipeline_wait_for_stop_with_ticks(pipeline, pdMS_TO_TICKS(3000));
	if (stop_ret != ESP_OK)
	{
		stop_ret = audio_pipeline_terminate_with_ticks(pipeline, pdMS_TO_TICKS(2000));
		if (stop_ret != ESP_OK)
		{
			audio_pipeline_terminate(pipeline);
		}
	}

	audio_pipeline_reset_elements(pipeline);
	audio_pipeline_reset_ringbuffer(pipeline);
	audio_pipeline_reset_items_state(pipeline);

	audio_element_set_uri(http_stream_reader, uri);
	err = audio_pipeline_run(pipeline);

Jak to rozumiec:

- najpierw proba ladnego stop
- jak nie wychodzi, idzie mocniejsza sciezka terminate
- potem reset i start od nowa

## 7) Funkcja log_stream_diagnostics - co czytac w logach

Lokalizacja: [log_stream_diagnostics](components/audio/audio.c#L452)

Zbiera:

- stany elementow HTTP/MP3/I2S
- czasy bez aktywnosci
- zapelnienie ringbuffera I2S
- pole suspect z najbardziej prawdopodobna przyczyna

To pomaga odpowiedziec na pytanie: czy problem jest w sieci, dekoderze, czy na wyjsciu PCM.

## 8) Jak czytac Twoje bledy z monitora

Gdy widzisz:

- Without stop, st:7
- Pipeline already started, state:7

to znaczy, ze restart trafil na stan, w ktorym pipeline nie zakonczyl poprzedniej fazy tak, jak oczekiwano.

Gdy widzisz:

- i2s input ringbuffer empty timeout

to znaczy, ze przez dluzej niz STREAM_EMPTY_BUFFER_TIMEOUT_MS nie doplynely probki PCM do wejscia I2S.

## 9) Kolejnosc czytania kodu, zeby zrozumiec calosc

Najlatwiej tak:

1. [stream_task](components/audio/audio.c#L966)
2. [stream_watchdog_restart_reason](components/audio/audio.c#L709)
3. [restart_current_stream](components/audio/audio.c#L680)
4. [audio_restart_task](components/audio/audio.c#L598)
5. [restart_stream_for_station_locked](components/audio/audio.c#L519)
6. [change_radio_station](components/audio/audio.c#L889)

Po tej kolejnosci zrozumiesz pelny przeplyw: detekcja -> zlecenie -> kolejka -> wykonanie restartu.

## 10) Mini slownik (prosto)

- cooldown: krotka przerwa po restarcie, zeby nie restartowac non stop
- grace period: okres po starcie, gdy ignorujemy czesc alarmow
- ringbuffer: bufor danych pomiedzy elementami pipeline
- coalescing: laczenie wielu szybkich requestow do jednego finalnego

## 11) Co moge dopisac w kolejnym kroku

Jesli chcesz, moge od razu dodac drugi plik: AUDIO_WATCHDOGS_PL_DEBUG_CHECKLIST.md z gotowymi regułami:

- co sprawdzic po kolei
- jakie logi sa ok
- jakie logi sa krytyczne
- co zmienic w timeoutach i o ile
